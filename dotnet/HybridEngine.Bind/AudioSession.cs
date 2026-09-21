using System;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

/// <summary>Handle-based ms_audio_* API surface for callers that already own an engine handle.</summary>
public static class AudioApi
{
    public static int Open(IntPtr engine, string path) => Native.ms_audio_open(engine, path);
    public static int Close(IntPtr engine, long handle) => Native.ms_audio_close(engine, handle);
    public static int Play(IntPtr engine, long handle) => Native.ms_audio_play(engine, handle);
    public static int Pause(IntPtr engine, long handle) => Native.ms_audio_pause(engine, handle);
    public static int Seek(IntPtr engine, long handle, double ms) => Native.ms_audio_seek(engine, handle, ms);
    public static int Position(IntPtr engine, long handle, out double ms) => Native.ms_audio_position(engine, handle, out ms);
    public static int IsOpen(IntPtr engine, long handle) => Native.ms_audio_is_open(engine, handle);
    public static int Volume(IntPtr engine, long handle, double gain) => Native.ms_audio_volume(engine, handle, gain);
}


/// <summary>Managed audio session bound to a HybridEngine engine instance.</summary>
public sealed class AudioSession : IDisposable
{
    private readonly IntPtr _engine;
    private readonly long _handle;
    private bool _disposed;

    internal AudioSession(IntPtr engine, long handle)
    {
        _engine = engine;
        _handle = handle;
    }

    public long Handle => _handle;
    public bool IsOpen => !_disposed && Native.ms_audio_is_open(_engine, _handle) != 0;
    public bool Playing { get; private set; }

    public int Play()
    {
        int rc = Native.ms_audio_play(_engine, _handle);
        if (rc == BindError.OK) Playing = true;
        return rc;
    }

    public int Pause()
    {
        int rc = Native.ms_audio_pause(_engine, _handle);
        if (rc == BindError.OK) Playing = false;
        return rc;
    }

    public int Seek(double ms) => Native.ms_audio_seek(_engine, _handle, ms);

    /// <summary>当前播放位置（毫秒）。
    /// ⚠ 失败时**抛异常**（M1）：原生对已关闭/无效句柄返回 NOT_FOUND **且不写出参**，
    /// 直接返回 out 值等于把未初始化的 double 当播放位置。</summary>
    public double PositionMs
    {
        get
        {
            int rc = Native.ms_audio_position(_engine, _handle, out double ms);
            if (rc != BindError.OK)
                throw new InvalidOperationException("ms_audio_position rc=" + rc + "（音频句柄已关闭/失效）");
            return ms;
        }
    }

    public int Volume(double gain) => Native.ms_audio_volume(_engine, _handle, gain);

    public int Close()
    {
        Playing = false;
        int rc = Native.ms_audio_close(_engine, _handle);
        _disposed = true;
        return rc;
    }

    public void Dispose() => Close();
}