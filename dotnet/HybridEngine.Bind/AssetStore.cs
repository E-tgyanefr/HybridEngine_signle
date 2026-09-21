using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// 资产句柄包装（ms_assets_* —— 句柄=绑定层包装；unref=引用计数-1）
public sealed class Asset : IDisposable
{
    internal IntPtr Handle { get; }
    internal IntPtr Engine { get; }
    // 幂等标志：C 侧 ms_assets_unref 会 `delete h`，**无法自检句柄是否已释放**，
    //   故重复 Dispose 会二次 free → 实测 STATUS_HEAP_CORRUPTION（0xC0000374，进程级）。
    private bool _disposed;
    internal Asset(IntPtr engine, IntPtr handle) { Engine = engine; Handle = handle; }
    public string Type { get { byte[] b = new byte[32]; Native.ms_assets_type(Engine, Handle, b, b.Length); return Strip(b); } }
    public string Guid { get { byte[] b = new byte[32]; Native.ms_assets_guid(Engine, Handle, b, b.Length); return Strip(b); } }
    public bool IsDisposed => _disposed;
    public void Dispose()
    {
        if (_disposed) return;   // ← 二次释放必须被挡住（否则堆损坏）
        _disposed = true;
        Native.ms_assets_unref(Engine, Handle);
        GC.SuppressFinalize(this);
    }

    // t7：纹理像素（借出指针快照——句柄存活期间有效；零拷贝直读后 Marshal 拷贝）
    public (int Width, int Height, int Stride, uint[] Pixels) TexturePixels()
    {
        if (Native.ms_assets_texture_pixels(Engine, Handle, out int w, out int h, out int stride, out IntPtr p) != BindError.OK)
            throw new InvalidOperationException("ms_assets_texture_pixels failed (non-Texture? unref'd?)");
        uint[] px = new uint[w * h];
        if (w * h > 0)
        {
            int[] raw = new int[w * h];
            Marshal.Copy(p, raw, 0, w * h);   // Marshal.Copy 无 uint[] 重载——int[] 再转换
            for (int i = 0; i < raw.Length; ++i) px[i] = (uint)raw[i];
        }
        return (w, h, stride, px);
    }
    public (int Width, int Height) TextureSize()
    {
        if (Native.ms_assets_texture_pixels(Engine, Handle, out int w, out int h, out _, out _) != BindError.OK)
            throw new InvalidOperationException("ms_assets_texture_pixels failed (non-Texture?)");
        return (w, h);
    }

    private static string Strip(byte[] b) { int n = Array.IndexOf(b, (byte)0); if (n < 0) n = b.Length; return Encoding.UTF8.GetString(b, 0, n); }
}

public sealed class AssetStore
{
    private IntPtr _engine;
    internal void Attach(IntPtr engine) { _engine = engine; }
    public Asset Load(string assetPath)
    {
        int err = 0;
        var h = Native.ms_assets_load(_engine, assetPath, out err);
        if (h == IntPtr.Zero) throw new InvalidOperationException("ms_assets_load err=" + err);
        return new Asset(_engine, h);
    }
    public bool SaveBinary(string assetPath, byte[] data, string type) => Native.ms_assets_save(_engine, assetPath, data, data.Length, type) == BindError.OK;
    public bool SaveText(string assetPath, string payload, string type) => Native.ms_assets_save_text(_engine, assetPath, payload, type) == BindError.OK;

    // t7：递归资产列表（类型表过滤——JSON 字符串数组；cap 不足自动扩容重试）
    public string[] List(string dir)
    {
        int cap = 4096;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            byte[] buf = new byte[cap];
            int rc = Native.ms_assets_list(_engine, dir, buf, buf.Length);
            if (rc == BindError.OK)
            {
                string s = Encoding.UTF8.GetString(buf, 0, Array.IndexOf(buf, (byte)0));
                return JsonSerializer.Deserialize<string[]>(s) ?? Array.Empty<string>();
            }
            if (rc != BindError.ErrBadArg) throw new InvalidOperationException("ms_assets_list err=" + rc);
            cap *= 2;
        }
        throw new InvalidOperationException("ms_assets_list too large (>1MB)");
    }
}
