using System;
using System.Text;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// M2.5：GameEngine 门面（设计 §2.1——一场景一引擎模型；Run=块式循环）
public sealed class GameEngine : IDisposable
{
    /// <summary>最近创建的引擎（静态 托管风格 API 的宿主）。</summary>
    public static GameEngine? Current { get; private set; }

    public IntPtr Handle { get; private set; }   // 脚本桥/测试访问（M3.4）
    private readonly bool _ownsEngine = true;    // false=Attach 到宿主引擎（Dispose 不销毁）
    // 活动场景句柄。**每次查询**（不再在 Create 时缓存）：多场景下活动场景会切换
    // （SceneManager.SetActiveScene / ms_scene_set_active_index），缓存值会指向旧场景——
    // 实测症状：切活动场景后 SaveScene 存的是原场景、按场景读取全部错位。
    internal IntPtr ScenePtr => Handle == IntPtr.Zero ? IntPtr.Zero : Native.ms_engine_scene(Handle);

    public string Title { get; set; } = "HybridEngine";
    public int Width { get; set; } = 1280;
    public int Height { get; set; } = 720;

    public event Action<double>? OnFrame;
    public event Action<IRenderer>? OnRender;
    public AssetStore Assets { get; } = new();
    /// <summary>默认根对象。P1-a：场景整体载入后自动重指向新场景的首个根（原代理随旧场景销毁而失效）。</summary>
    public SceneObject Root { get; private set; } = null!;

    // t1：GUI 平面代理（OnRender 回调参数=真实 IRenderer——C ABI 绘制进引擎当前帧缓冲）
    public IRenderer Renderer { get; private set; } = null!;
    public bool ShouldQuit { get; private set; }
    // 幂等 Dispose：重复释放会二次 ms_engine_destroy / 二次拆桥（实测堆损坏）。
    private bool _disposed;

    public GameEngine()
    {
        Create();
        Root = AddRoot("Root");
    }

    // 注：Title/Width/Height 属性经对象初始化器赋值会晚于 Create()——尺寸/标题直调坑（窗口实测 1280×720 + 默认标题）
    public GameEngine(string title, int width, int height)
    {
        Title = title; Width = width; Height = height;
        Create();
        Root = AddRoot("Root");
    }

    public GameEngine(IScene scene) : this() => scene.Build(this);

    /// <summary>
    /// 附加到**已存在**的原生引擎（不创建、不销毁）。
    ///
    /// 为什么需要它（这是个真缺口）：引擎句柄原本只有 <see cref="GameEngine()"/> 一条路能拿到——
    /// 也就是说「引擎必须由托管侧 new 出来」。但**编辑器进程里的引擎是 C++ 侧创建的**
    /// （EditorApp 持有 Engine，脚本经 hostfxr 在里面跑）。结果：编辑器里跑的脚本拿不到
    /// <see cref="Current"/> → <c>Renderer</c>/<c>Key</c>/<c>FindSceneObject</c>/<c>Instantiate</c>
    /// 全部不可用，用 Renderer 画出来的游戏在编辑器里根本无法运行。
    /// 本方法把这条通路接上：脚本宿主在装配项目脚本时附加一次，<see cref="Current"/> 即指向宿主引擎。
    ///
    /// 所有权：附加得到的门面 <b>Dispose 不会销毁引擎</b>（引擎归宿主所有）。
    /// </summary>
    public static GameEngine Attach(IntPtr engineHandle)
    {
        if (engineHandle == IntPtr.Zero) throw new ArgumentException("engineHandle 不能为 0", nameof(engineHandle));
        if (Current != null && Current.Handle == engineHandle) return Current;   // 幂等（重复装配同引擎）
        return new GameEngine(engineHandle, ownsEngine: false);
    }

    // 附加路径的私有构造：不 Create、不建 Root，只把已有句柄包成门面。
    private GameEngine(IntPtr existing, bool ownsEngine)
    {
        Handle = existing;
        _ownsEngine = ownsEngine;
        Assets.Attach(Handle);
        Renderer = new RendererProxy(Handle);
        ManagedFrame.Register(Handle);   // HashSet 去重——与 Scripts.Load 的注册不会双驱动
        // 已存在场景的首个根（无根对象时给一个空代理，避免 null 传播到脚本）
        Root = RootCount > 0 ? GetRoot(0) : null!;
        Current = this;
    }

    private void Create()
    {
        int err = 0;
        Handle = Native.ms_engine_create(Title, Width, Height, out err);
        if (Handle == IntPtr.Zero) throw new InvalidOperationException("ms_engine_create err=" + err);
        Assets.Attach(Handle);
        Renderer = new RendererProxy(Handle);
        ManagedFrame.Register(Handle);   // P1-a：托管每帧服务（Time/Invoke/协程）挂到引擎帧上
        Current = this;
    }

    private SceneObject AddRoot(string name)
    {
        if (Native.ms_scene_add_root(Handle, ScenePtr, name, out long id) != BindError.OK)
            throw new InvalidOperationException("ms_scene_add_root failed");
        if (Native.ms_scene_go_get(Handle, ScenePtr, id, out var go) != BindError.OK)
            throw new InvalidOperationException("ms_scene_go_get failed");
        return new SceneObject(Handle, go, id, name);
    }

    public SceneObject CreateRoot(string name) => AddRoot(name);

    /// <summary>托管风格创建对象：parent=null 建根对象，否则建子对象。</summary>
    /// <summary>按名查找场景对象（DFS；未找到=null）。</summary>
    public SceneObject? FindSceneObject(string name)
    {
        if (string.IsNullOrEmpty(name)) return null;
        int rc = Native.ms_scene_find(Handle, ScenePtr, name, out var go);
        if (rc != BindError.OK || go == IntPtr.Zero) return null;
        long id = Native.ms_go_instance_id(Handle, go);
        return new SceneObject(Handle, go, id, name);
    }

    /// <summary>预制体实例化到当前场景（.msprefab）。</summary>
    public SceneObject InstantiatePrefab(string prefabAssetPath)
    {
        int rc = Native.ms_assets_instantiate_prefab(Handle, prefabAssetPath, out var go);
        if (rc != BindError.OK || go == IntPtr.Zero) throw new InvalidOperationException("ms_assets_instantiate_prefab rc=" + rc);
        long id = Native.ms_go_instance_id(Handle, go);
        return new SceneObject(Handle, go, id, "prefab");
    }
    public SceneObject CreateSceneObject(string name, SceneObject? parent = null)
        => parent == null ? CreateRoot(name) : parent.AddChild(name);

    // —— 场景 ——
    public GameEngine LoadScene(IScene scene) { scene.Build(this); return this; }
    public void SaveScene(string assetPath)
    {
        int rc = Native.ms_scene_save(Handle, ScenePtr, assetPath);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_scene_save rc=" + rc);
    }
    public void LoadScene(string assetPath)
    {
        int rc = Native.ms_scene_load(Handle, ScenePtr, assetPath);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_scene_load rc=" + rc);
        // P1-a：场景被整体替换——旧根代理指向已销毁对象；重指向载入场景的首个根。
        // 脚本组件实例由引擎内的重放回调（SceneReplay）按 scriptFields 重建，此处无需额外处理。
        if (RootCount > 0) Root = GetRoot(0);
    }
    public int RootCount
    {
        get { Native.ms_scene_root_count(Handle, ScenePtr, out int n); return n; }
    }
    public SceneObject GetRoot(int index)
    {
        if (Native.ms_scene_root_get(Handle, ScenePtr, index, out var go) != BindError.OK)
            throw new ArgumentOutOfRangeException(nameof(index));
        long id = Native.ms_go_instance_id(Handle, go);
        return new SceneObject(Handle, go, id, "go" + id);
    }

    // —— 循环 ——
    public int RunFrame(double dt = 0.016)
    {
        // P1-a：托管每帧服务（Time 同步 / Invoke / 协程）正常由引擎帧钩子驱（ManagedFrame）——
        //   这样编辑器 Play 直驱 ms_engine_tick 的路径也能跑 Invoke/协程。
        //   旧 DLL 无帧钩子入口（Registered=false）→ 在 RunFrame 内按原位置自驱，行为与改动前逐位一致。
        //   两条路径互斥（Registered 决定），不会双驱动。
        bool selfDrive = !ManagedFrame.Registered;
        if (selfDrive) ManagedFrame.Tick(dt, 0);   // 帧首：Time 同步（必须早于组件回调）

        int tick = Native.ms_engine_tick(Handle, dt);
        int pump = Native.ms_engine_pump(Handle);
        // 录播式帧序（t1 GUI 平面定稿）：帧首清列表 → OnFrame/OnRender 录制本帧命令（绘制原语+文字=Text 命令）
        // → ms_engine_render 先清黑→按序回放+哈希（同帧可见；空命令=黄金路径逐位不变=零回归）
        TryClearRndList(Handle);
        OnFrame?.Invoke(dt);
        if (selfDrive) ManagedFrame.Tick(dt, 1);   // 帧尾：Invoke/协程（Update 后、渲染前——原 RoutineRunner.Tick 位置）
        OnRender?.Invoke(Renderer);
        int render = Native.ms_engine_render(Handle, IntPtr.Zero);
        if (pump != 0) ShouldQuit = true;
        return tick != 0 ? tick : render != 0 ? render : 0;
    }

    private static bool? _hasRndClearList;   // 旧 DLL 兼容：首次调用探测（EntryPointNotFound → 恒跳过）
    private static void TryClearRndList(IntPtr h)
    {
        if (_hasRndClearList == false) return;
        try { Native.ms_rnd_clear_list(h); _hasRndClearList = true; }
        catch (System.EntryPointNotFoundException) { _hasRndClearList = false; }
    }

    public int Run(int frames = -1, double dt = 0.016)
    {
        int i = 0;
        while (!ShouldQuit && (frames < 0 || i < frames))
        {
            int rc = RunFrame(dt);
            if (rc != 0 && rc != 1) return rc;
            ++i;
        }
        return 0;
    }

    public ulong RenderHash => Native.ms_engine_render_hash(Handle);
    public double LastFrameMs => Native.ms_engine_last_frame_ms(Handle);
    public static int LiveEngineCount => Native.ms_bind_live_engine_count();

    // —— G2/t137：输入（vk=KeyCode 值——Win32 VK 直通；窗口键泵由 RunFrame/Pump 驱动）——
    public bool KeyDown(int vk) => Native.ms_input_key_down(Handle, vk) != 0;
    public bool KeyUp(int vk) => Native.ms_input_key_up(Handle, vk) != 0;
    public bool Key(int vk) => Native.ms_input_get_key(Handle, vk) != 0;
    public double Axis(string name) => Native.ms_input_axis(Handle, name);
    public bool Button(string action) => Native.ms_input_get_button(Handle, action) != 0;

    // —— t1 GUI：鼠标（ms_input_mouse_*——button 0=左 1=中 2=右；wheel delta 本帧累计 ±1）——
    public double MouseX => Native.ms_input_mouse_x(Handle);
    public double MouseY => Native.ms_input_mouse_y(Handle);
    public bool GetMouseButton(int button) => Native.ms_input_mouse_button(Handle, button) != 0;      // 当前按住
    public bool GetMouseButtonDown(int button) => Native.ms_input_mouse_button_down(Handle, button) != 0; // 本帧按下沿
    public bool GetMouseButtonUp(int button) => Native.ms_input_mouse_button_up(Handle, button) != 0;     // 本帧抬起沿
    public double MouseWheelDelta => Native.ms_input_mouse_wheel_delta(Handle);

    // —— t-auto：自动化输入注入 ——
    // 用途：录制/回归/演示脚本"无人值守地驱动游戏"。注入直接写 engine->Input()，与窗口回调
    // **同一个对象、同一套语义**（按住/边沿/滚轮一致），且不受窗口焦点、最小化、编辑器页签影响
    //（靠 PostMessage 注入窗口消息实测不可靠：编辑器不在游戏视图时整条转发链都不生效）。
    public int InjectKey(int vk, bool down) => Native.ms_input_inject_key(Handle, vk, down ? 1 : 0);
    public int InjectMouse(double x, double y, bool down, int button = 0) => Native.ms_input_inject_mouse(Handle, x, y, button, down ? 1 : 0);
    public int InjectMove(double x, double y) => Native.ms_input_inject_move(Handle, x, y);
    public int InjectWheel(double x, double y, int delta) => Native.ms_input_inject_wheel(Handle, x, y, delta);

    // —— t1.1：设计面鼠标（ViewportPolicy ToVirtual——Letterbox/DPI 换算；旧 DLL=回退原始坐标）——
    private static bool? _hasDesignMouse;
    private static bool DesignMouseOk()
    {
        if (_hasDesignMouse == false) return false;
        try { _ = Native.ms_input_mouse_design_x(IntPtr.Zero); _hasDesignMouse = true; return true; }
        catch (System.EntryPointNotFoundException) { _hasDesignMouse = false; return false; }
    }
    public double MouseDesignX => DesignMouseOk() ? Native.ms_input_mouse_design_x(Handle) : MouseX;
    public double MouseDesignY => DesignMouseOk() ? Native.ms_input_mouse_design_y(Handle) : MouseY;

    // —— 反射属性（M2 ReflectionRegistry 经 ABI——面向原生组件；C# 脚本字段走托管侧=P1）——
    public string PropertyGet(SceneObject go, string type, string field)
    {
        byte[] buf = new byte[256];
        if (Native.ms_property_get(Handle, go.GoPtr, type, field, buf, buf.Length) != BindError.OK)
            throw new InvalidOperationException("ms_property_get failed");
        return StripUtf8(buf);
    }
    public void PropertySet(SceneObject go, string type, string field, string json)
    {
        int rc = Native.ms_property_set(Handle, go.GoPtr, type, field, json);
        if (rc != BindError.OK) throw new InvalidOperationException("ms_property_set rc=" + rc);
    }
    public string PropertyFieldsJson(string type)
    {
        byte[] buf = new byte[2048];
        if (Native.ms_property_fields(Handle, type, buf, buf.Length) != BindError.OK)
            return string.Empty;
        return StripUtf8(buf);
    }

    private static string StripUtf8(byte[] buf)
    {
        int n = Array.IndexOf(buf, (byte)0);
        if (n < 0) n = buf.Length;
        return Encoding.UTF8.GetString(buf, 0, n);
    }

    public bool SetAssetRoot(string absRoot) => Native.ms_assets_set_root(Handle, absRoot) == BindError.OK;

    /// <summary>
    /// Opens an audio file through the engine's WinMM audio backend.
    /// Returns null when the native open fails; handles are owned by the engine (>=10).
    /// </summary>
    public AudioSession? OpenAudio(string path)
    {
        if (Handle == IntPtr.Zero || string.IsNullOrEmpty(path)) return null;
        int handle = Native.ms_audio_open(Handle, path);
        if (handle < 10) return null;
        return new AudioSession(Handle, handle);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        if (ReferenceEquals(Current, this)) Current = null;
        if (Handle == IntPtr.Zero) { GC.SuppressFinalize(this); return; }
        // ⚠ 只有**拥有**引擎的门面才拆桥：
        //   Attach() 出来的门面（_ownsEngine=false，引擎归宿主所有，见 :60）若在这里
        //   ReleaseAll/Unregister，就会拆掉仍在运行的宿主引擎——宿主的组件回调变悬垂、
        //   帧钩子被摘（Time 冻结、Invoke/协程停摆），下一次 ms_engine_tick 直接崩。
        //   编辑器路径正是这么用的：ScriptLoader/Scripts.cs:70 `GameEngine.Attach(engine)`。
        //   宿主自行 Unload/销毁引擎时，桥由该宿主负责释放。
        if (_ownsEngine)
        {
            ComponentBridge.ReleaseAll(Handle);
            ManagedFrame.Unregister(Handle);
            Native.ms_engine_destroy(Handle);
        }
        Handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}
