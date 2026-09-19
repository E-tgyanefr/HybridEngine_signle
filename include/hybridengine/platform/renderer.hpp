#pragma once
#include <vector>
#include <cstdint>

namespace HybridEngine::Platform {

// M1：渲染接口——10 原语 + BlitRectAlpha（v1 语义/命名保留；确定性输出=黄金帧断言用）
// 颜色语义（t1 alpha）：填充原语（Fill*/DrawLine）Rgba.a 生效——straight-alpha 合成 out=(src*a+dst*(255-a)+127)/255；
//   a=255=直写快路径（既有输出逐位不变）；a=0=跳过；Clear=整帧覆盖（alpha 忽略）；BlitRect=不透明拷贝（背靠背）。
// 帧格式：uint32_t ARGB（A=FF 不透明——输出恒 0xFFRRGGBB；v1 ToArgb 口径）
struct Rgba { uint8_t r, g, b, a; };

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual void Clear(Rgba) = 0;
    virtual void ClearRect(double x, double y, double w, double h, Rgba) = 0;   // v1：与 FillRect 同语义
    virtual void FillRect(double x, double y, double w, double h, Rgba) = 0;
    // t-perf-text：批量矩形（文本掩码 span 专用——每 4 double 一个矩形 x,y,w,h）。
    // 输出语义=按序逐个 FillRect（后画覆盖先画）；默认实现即逐个 FillRect，其它后端零改动；
    // 软件渲染器覆写为内部非虚调用，省掉每 span 一次虚分发与重复的 alpha/裁剪入口检查。
    virtual void FillRects(const double* xywh, int count, Rgba c) {
        for (int i = 0; i < count; ++i)
            FillRect(xywh[i * 4], xywh[i * 4 + 1], xywh[i * 4 + 2], xywh[i * 4 + 3], c);
    }
    virtual void FillRoundedRect(double x, double y, double w, double h, double radius, Rgba) = 0;
    virtual void DrawLine(double x1, double y1, double x2, double y2, Rgba, double thickness = 1) = 0;
    virtual void FillCircle(double cx, double cy, double r, Rgba) = 0;
    // t-perf：**批量圆**（弹幕/粒子负载）。xyR 每 3 个 double = 一个圆 (x,y,r)。
    // 为什么放在接口层：逐颗调用时"每颗一个虚调用"只是小头，真正的成本是**缓存未命中**——
    //   每颗子弹要碰十几行、散布在整块帧缓冲里；批量进来后实现方可以按行重排，顺序访问。
    // 默认实现=逐个 FillCircle（其它实现者无需改动）。
    virtual void FillCircles(const double* xyR, int count, Rgba c) {
        for (int i = 0; i < count; ++i) FillCircle(xyR[i * 3], xyR[i * 3 + 1], xyR[i * 3 + 2], c);
    }
    virtual void DrawCircles(const double* xyR, int count, double thickness, Rgba c) {
        for (int i = 0; i < count; ++i) DrawCircle(xyR[i * 3], xyR[i * 3 + 1], xyR[i * 3 + 2], c, thickness);
    }
    virtual void DrawCircle(double cx, double cy, double r, Rgba, double thickness = 1) = 0;
    // t-sprite：**弹幕子弹批绘**（核心圆 + 外圈，一次调用画 N 颗同款子弹）。
    // 为什么单开一个原语：一颗子弹 = 盘 + 环两次填充 → 约 3×行数 次 span 写；而**同款子弹**
    //   （半径/线宽/两色固定）的逐行像素是完全固定的 → 可以烘焙成精灵，绘制退化为"每行一次连续 memcpy"。
    // 语义：与「先 FillCircles(core) 再 DrawCircles(rim)」逐位等价（后画的外圈覆盖重叠处）。
    // 默认实现就是这两步（其它实现者零改动）；软件渲染器覆写为烘焙精灵路径。
    // xy = 每颗 2 个 double（x,y）——r/thickness/两色对整批相同。
    virtual void DrawBullets(const double* xy, int count, double r, double thickness, Rgba core, Rgba rim) {
        std::vector<double> xyR((size_t)count * 3);
        for (int i = 0; i < count; ++i) {
            xyR[(size_t)i * 3] = xy[i * 2];
            xyR[(size_t)i * 3 + 1] = xy[i * 2 + 1];
            xyR[(size_t)i * 3 + 2] = r;
        }
        FillCircles(xyR.data(), count, core);
        DrawCircles(xyR.data(), count, thickness, rim);
    }
    virtual void FillTriangle(double x1, double y1, double x2, double y2, double x3, double y3, Rgba) = 0;
    virtual void FillQuad(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4, Rgba) = 0;
    virtual void BlitRect(double x, double y, double w, double h, const uint32_t* srcPixels) = 0;   // M3D-5：图像 blit（3D 目标合成——同一 XRGB 格式；不透明拷贝）
    virtual void BlitRectAlpha(double x, double y, double w, double h, const uint32_t* srcPixels) = 0;   // t1：blit 逐像素 straight-alpha 合成（src=0xAARRGGBB——a=255 直写/a=0 跳过；输出恒 0xFFRRGGBB）
    // t-perf：**带缩放的** blit（最近邻，不透明）。src = srcW×srcH 像素，缩放到目标矩形 (x,y,w,h)。
    // 为什么要有：调用方自己"先缩放到中间缓冲、再 BlitRect"要付两趟内存往返
    //   （写中间 ~4.8MB + 再读回 ~4.8MB）；软件渲染器覆写为直接写自己的帧缓冲，只走一趟。
    // 默认实现=缩放到临时缓冲再 BlitRect（对所有实现者都成立，不覆写也能用）；
    // 裁剪语义同 BlitRect（按裁剪栈顶；旋转裁剪取 AABB）。
    virtual void BlitRectScaled(double x, double y, double w, double h,
                                const uint32_t* srcPixels, int srcW, int srcH);
    // t6：矩形裁剪栈（≤8 深；栈空=全屏）。push=入栈（栈顶=当前裁剪——嵌套仅栈顶生效：栈=保存/恢复语义）；
    //   所有填充/绘制原语（含 Blit* / 文本掩码 span）写入前按栈顶矩形裁剪（floor/ceil 同既有矩形覆盖语义；完全不相交=跳过）。
    //   pop=恢复上一级（栈空 pop=无操作）；坐标=渲染器设计坐标（命令回放经视图映射——同其它原语）。
    virtual void PushClip(double x, double y, double w, double h) = 0;
    virtual void PopClip() = 0;
    // t-rot-clip：旋转矩形裁剪（同栈 ≤8 深；栈顶=当前；angle=弧度（逆时针）——中心 (cx,cy)、w×h 旋转角绕中心）。
    //   精确斜劈/分离位移用（矩形栈无法沿非轴对齐线切分）。默认=无操作（其它后端未实现时安全降级）。
    virtual void PushClipRotated(double cx, double cy, double w, double h, double angleRad) { (void)cx; (void)cy; (void)w; (void)h; (void)angleRad; }
    virtual void Resize(int w, int h) = 0;      // 重建缓冲（w/h<1→1）
    virtual const uint32_t* Frame() const = 0;  // 当前绘制缓冲（窗口 Present/黄金帧哈希用）
};

IRenderer* CreateSoftwareRenderer(int w, int h);
// （D3D11 后端=二期接口——V3-4 声明；接口已就绪=可插拔）

} // namespace HybridEngine::Platform