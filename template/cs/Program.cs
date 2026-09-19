// ============================================================================
// HybridEngine EngineSDK — 最小消费模板（C# / HybridEngine.Bind 接线）
//
// 【怎么接一个项目】三步：
//   1. 复制本目录到你的工程（或直接原地开发）。
//   2. dotnet run -- --frames 120（原生 dll 由 csproj 从 EngineSDK\bin 自动复制到输出目录）。
//      期望输出 drawHash=... stable=ok nonempty=ok，退出码 0。
//
// 【模板内容】空壳=空白窗口 + 一帧矩形 + 一行文本。不含任何游戏内容。
//   自检：静态内容 → frame60 与 frame120 哈希一致（确定性）+ 非空（≠无绘制黄金帧 4634E387E024BE90）。
//
// 【绑定层说明】（HybridEngine.Bind——net8.0 纯 BCL，P/Invoke 同一 ms_bind.h C ABI）：
//   - GameEngine：OnRender(IRenderer) 回调 = 客户端绘制钩子；RunFrame(dt) 自动帧序
//     （清列表→回调录制→render 回放+哈希）；单线程（创建线程=主线程）；
//   - 颜色 = Rgba(r,g,b,a)（A=alpha；默认=255 不透明）；文本 = DrawText UTF-8（CJK 直支持）；
//   - 确定性 = 同命令序列 → 同 RenderHash（黄金帧 4634E387E024BE90 = 无绘制基线）。
// ============================================================================
using System;
using HybridEngine.Engine;

namespace HybridEngine.SdkTemplate;

public static class Program
{
    private const ulong Golden = 0x4634E387E024BE90UL;   // 无绘制基线（黄金帧）

    private static int Main(string[] args)
    {
        int frames = 120;
        for (int i = 0; i + 1 < args.Length; ++i)
            if (args[i] == "--frames") frames = int.Parse(args[i + 1]);

        ulong hMid = 0, hLast = 0;
        using (var engine = new GameEngine("EngineSDK template cs", 1280, 800))
        {
            engine.OnRender += r =>
            {
                r.Clear(new Rgba(18, 22, 30));                                   // 整帧清色（空白窗口底）
                r.FillRect(80, 60, 320, 180, new Rgba(52, 120, 220));            // 一帧矩形
                r.DrawText("HybridEngine SDK Template", 80, 300, 40, new Rgba(255, 255, 255));   // 一行文本
            };
            for (int i = 0; i < frames; ++i)
            {
                engine.RunFrame(1.0 / 60.0);
                if (i == frames / 2) hMid = engine.RenderHash;
                if (i == frames - 1) hLast = engine.RenderHash;
            }
        }

        bool stable = hMid == hLast;                 // 静态内容 → 帧间确定性
        bool nonempty = hLast != Golden;             // ≠ 无绘制基线
        Console.WriteLine($"drawHash={hLast:X16} stable={(stable ? "ok" : "FAIL")} nonempty={(nonempty ? "ok" : "FAIL")}");
        return stable && nonempty ? 0 : 1;
    }
}
