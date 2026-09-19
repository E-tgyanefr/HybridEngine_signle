namespace HybridEngine.Engine;

// 托管风格 Time（GameEngine.RunFrame 每帧写入；double 精度）。
public static class Time
{
    /// <summary>本帧缩放后时间（秒）。</summary>
    public static double deltaTime { get; internal set; }

    /// <summary>本帧未缩放时间（秒）。</summary>
    public static double unscaledDeltaTime { get; internal set; }

    /// <summary>自引擎启动累计时间（秒）。</summary>
    public static double time { get; internal set; }

    /// <summary>已运行帧数。</summary>
    public static int frameCount { get; internal set; }

    /// <summary>固定步长（与 C++ LifecycleDriver 1/60s 对齐）。</summary>
    public static double fixedDeltaTime => 1.0 / 60.0;
}