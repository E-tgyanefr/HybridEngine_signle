using System;
using HybridEngine.Engine.Internal;

namespace HybridEngine.Engine;

// t1：GUI 平面渲染代理（真实 IRenderer——构造携带引擎句柄；方法 = C ABI ms_rnd_*/ms_text_*。
// 坐标 double、颜色 Rgba(uint)——与 C++ IRenderer（platform/renderer.hpp）同语义；确定性输出=黄金帧断言基础）
// t1 alpha：Rgba.A 生效——ToArgb 打包 0xAARRGGBB（A 直通；默认 a=255=既有输出逐位不变）
public interface IRenderer
{
    // —— 10 绘制原语（ms_rnd_*）——
    void Clear(Rgba color);                                                        // 整帧清色（alpha 忽略=整帧覆盖）
    void ClearRect(double x, double y, double w, double h, Rgba color);            // 清矩形（与 FillRect 同语义）
    void FillRect(double x, double y, double w, double h, Rgba color);
    void FillRoundedRect(double x, double y, double w, double h, double radius, Rgba color);
    void DrawLine(double x1, double y1, double x2, double y2, Rgba color, double thickness = 1.0);
    void FillCircle(double cx, double cy, double r, Rgba color);
    /// <summary>批量实心圆（弹幕/粒子）：xyR 每 3 个 double = 一个圆 (x,y,r)；同色合成 1 条命令。
    /// 默认实现=逐个 FillCircle（其它实现者无需改动）；RendererProxy 走 ms_rnd_fill_circles 批量路径。</summary>
    void FillCircles(double[] xyR, int count, Rgba color)
    {
        for (int i = 0; i < count; i++) FillCircle(xyR[i * 3], xyR[i * 3 + 1], xyR[i * 3 + 2], color);
    }
    /// <summary>批量子弹（核心圆 + 外圈，整批同款）——xy 每颗 2 个 double；走引擎的烘焙精灵路径。</summary>
    void DrawBullets(double[] xy, int count, double r, double thickness, Rgba core, Rgba rim)
    {
        for (int i = 0; i < count; i++) { FillCircle(xy[i * 2], xy[i * 2 + 1], r, core); DrawCircle(xy[i * 2], xy[i * 2 + 1], r + thickness * 0.5, rim, thickness); }
    }
    /// <summary>批量圆环（同上，另加线宽）。</summary>
    void DrawCircles(double[] xyR, int count, double thickness, Rgba color)
    {
        for (int i = 0; i < count; i++) DrawCircle(xyR[i * 3], xyR[i * 3 + 1], xyR[i * 3 + 2], color, thickness);
    }
    void DrawCircle(double cx, double cy, double r, Rgba color, double thickness = 1.0);
    void FillTriangle(double x1, double y1, double x2, double y2, double x3, double y3, Rgba color);
    void FillQuad(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4, Rgba color);
    void BlitRect(double x, double y, double w, double h, uint[] srcPixels);       // 图像 blit（0xAARRGGBB 像素；不透明拷贝=背靠背）
    void BlitRectAlpha(double x, double y, double w, double h, uint[] srcPixels);  // t1：src=0xAARRGGBB 逐像素 straight-alpha 合成

    // —— 矩形裁剪（t6：Clip/Scissor——push 入栈 ≤8 深（栈顶生效）；pop 恢复；越界内容不画出）——
    void ClipPush(double x, double y, double w, double h);
    // t-rot-clip：旋转矩形裁剪（中心+尺寸+角度弧度——斜劈/分离位移精确切分；同栈：pop 恢复；angle=0≡ClipPush）
    void ClipPushRotated(double cx, double cy, double w, double h, double angleRad);
    void ClipPop();

    // —— 文本（ms_text_*——UTF-8；录制 Text 命令——回放绘制双面，参与哈希）——
    void DrawText(string utf8, double x, double y, double size, Rgba color);
    (double Width, double Height) MeasureText(string utf8, double size);
}

// 颜色：与 C++ Rgba{r,g,b,a} 同构；ABI 传 uint32 0xAARRGGBB（A=alpha 直通——填充原语 straight-alpha 合成；帧输出恒 0xFFRRGGBB）
public readonly struct Rgba
{
    public readonly byte R, G, B, A;

    public Rgba(byte r, byte g, byte b, byte a = 255)
    {
        R = r; G = g; B = b; A = a;
    }

    public static Rgba From(uint argb) => new(
        (byte)(argb >> 16), (byte)(argb >> 8), (byte)argb, (byte)(argb >> 24));

    /// <summary>打包为 ABI uint32（0xAARRGGBB——与 C++ ToArgb 同口径；A 直通；默认 a=255=0xFFRRGGBB</summary>
    public uint ToArgb() => ((uint)A << 24) | ((uint)R << 16) | ((uint)G << 8) | B;

    public override string ToString() => $"Rgba({R},{G},{B},{A})";
}

// 真实代理：每个方法 1 次 P/Invoke（引擎当前帧缓冲绘制；C++ 帧序内联 clear=自动空帧）
internal sealed class RendererProxy : IRenderer
{
    private readonly IntPtr _engine;

    public RendererProxy(IntPtr engine) => _engine = engine;

    public void Clear(Rgba color) => Native.ms_rnd_clear(_engine, color.ToArgb());
    public void ClearRect(double x, double y, double w, double h, Rgba color) => Native.ms_rnd_clear_rect(_engine, x, y, w, h, color.ToArgb());
    public void FillRect(double x, double y, double w, double h, Rgba color) => Native.ms_rnd_fill_rect(_engine, x, y, w, h, color.ToArgb());
    public void FillRoundedRect(double x, double y, double w, double h, double radius, Rgba color) => Native.ms_rnd_fill_rounded_rect(_engine, x, y, w, h, radius, color.ToArgb());
    public void DrawLine(double x1, double y1, double x2, double y2, Rgba color, double thickness = 1.0) => Native.ms_rnd_draw_line(_engine, x1, y1, x2, y2, color.ToArgb(), thickness);
    public void FillCircle(double cx, double cy, double r, Rgba color) => Native.ms_rnd_fill_circle(_engine, cx, cy, r, color.ToArgb());
    public void FillCircles(double[] xyR, int count, Rgba color) { if (count > 0) Native.ms_rnd_fill_circles(_engine, xyR, count, color.ToArgb()); }
    public void DrawCircles(double[] xyR, int count, double thickness, Rgba color) { if (count > 0) Native.ms_rnd_draw_circles(_engine, xyR, count, thickness, color.ToArgb()); }
    public void DrawBullets(double[] xy, int count, double r, double thickness, Rgba core, Rgba rim) { if (count > 0) Native.ms_rnd_bullets(_engine, xy, count, r, thickness, core.ToArgb(), rim.ToArgb()); }
    public void DrawCircle(double cx, double cy, double r, Rgba color, double thickness = 1.0) => Native.ms_rnd_draw_circle(_engine, cx, cy, r, color.ToArgb(), thickness);
    public void FillTriangle(double x1, double y1, double x2, double y2, double x3, double y3, Rgba color) => Native.ms_rnd_fill_triangle(_engine, x1, y1, x2, y2, x3, y3, color.ToArgb());
    public void FillQuad(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4, Rgba color) => Native.ms_rnd_fill_quad(_engine, x1, y1, x2, y2, x3, y3, x4, y4, color.ToArgb());
    public void BlitRect(double x, double y, double w, double h, uint[] srcPixels) => Native.ms_rnd_blit_rect(_engine, x, y, w, h, srcPixels);
    public void BlitRectAlpha(double x, double y, double w, double h, uint[] srcPixels) => Native.ms_rnd_blit_alpha(_engine, x, y, w, h, srcPixels);
    public void ClipPush(double x, double y, double w, double h) => Native.ms_rnd_clip_push(_engine, x, y, w, h);   // t6：矩形裁剪入栈
    public void ClipPushRotated(double cx, double cy, double w, double h, double angleRad) => Native.ms_rnd_clip_push_rotated(_engine, cx, cy, w, h, angleRad);   // t-rot-clip：旋转矩形裁剪入栈
    public void ClipPop() => Native.ms_rnd_clip_pop(_engine);                                                     // t6：弹栈

    public void DrawText(string utf8, double x, double y, double size, Rgba color) => Native.ms_text_draw(_engine, utf8, x, y, size, color.ToArgb());
    public (double Width, double Height) MeasureText(string utf8, double size)
    {
        Native.ms_text_measure(_engine, utf8, size, out double w, out double h);
        return (w, h);
    }
}
