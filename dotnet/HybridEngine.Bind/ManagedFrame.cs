using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// P1-a：托管每帧驱动（引擎帧钩子 ms_engine_set_frame_hook 的托管端）。
//
// 为什么需要它：托管侧的每帧服务（Time 同步、Invoke、协程）原本只挂在 GameEngine.RunFrame 上。
//   一旦引擎由别处驱动（编辑器 Play 直接调 ms_engine_tick），RunFrame 永不执行——Invoke/协程会静默不跑。
//   把驱动点下沉到引擎帧，两条驱动路径（托管 RunFrame / 编辑器 Play）行为一致。
//
// 相位语义（与 ms_bind.h 一致）：
//   phase=0 帧首：同步 Time（必须早于组件回调——否则组件 Update 内读到上一帧的 deltaTime）
//   phase=1 帧尾：推进 Invoke 与协程（Lifecycle 全序之后——与 RunFrame 原「Update 后」语义一致）
//
// 兼容：旧 DLL 无此入口（EntryPointNotFoundException）→ Registered=false → RunFrame 自驱（行为不变）。
internal static class ManagedFrame
{
    private static readonly MsCbFrame _hook = OnFrame;   // 静态委托保活（GetFunctionPointerForDelegate 不持有引用）
    private static readonly HashSet<IntPtr> _engines = new();

    /// <summary>引擎帧钩子是否生效（false=旧 DLL，托管侧回退 RunFrame 自驱）。</summary>
    public static bool Registered => _engines.Count > 0;

    public static void Register(IntPtr engine)
    {
        if (engine == IntPtr.Zero || _engines.Contains(engine)) return;
        try
        {
            int rc = Native.ms_engine_set_frame_hook(engine, Marshal.GetFunctionPointerForDelegate(_hook), IntPtr.Zero);
            if (rc == BindError.OK) _engines.Add(engine);
            // 失败不静默：没钩子 → 托管 Time 恒 0、Invoke/协程不推进（脚本「跑了但时间不动」）
            else Debug.LogWarning("引擎帧钩子注册失败 rc=" + rc + "（托管 Time/Invoke/协程不会推进）");
        }
        catch (EntryPointNotFoundException)
        {
            Debug.LogWarning("引擎帧钩子入口不存在（旧 DLL）——托管 Time/Invoke/协程不会推进，已回退 RunFrame 自驱");
        }
    }

    public static void Unregister(IntPtr engine)
    {
        if (engine == IntPtr.Zero || !_engines.Remove(engine)) return;
        try { Native.ms_engine_set_frame_hook(engine, IntPtr.Zero, IntPtr.Zero); }
        catch (EntryPointNotFoundException) { }
    }

    /// <summary>托管每帧推进（钩子回调与旧 DLL 回退路径共用——单一实现，无双驱动风险）。</summary>
    public static void Tick(double dt, int phase)
    {
        if (phase == 0)
        {
            Time.deltaTime = dt;
            Time.unscaledDeltaTime = dt;
            Time.time += dt;
            Time.frameCount++;
            return;
        }
        InvokeScheduler.Tick(dt);
        RoutineRunner.Tick(dt);
    }

    private static void OnFrame(IntPtr userData, double dt, int phase) => Tick(dt, phase);
}
