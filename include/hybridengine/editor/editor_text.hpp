#pragma once
#include "hybridengine/platform/renderer.hpp"
#include "hybridengine/editor/uikit.hpp"
#include "hybridengine/editor/cjk_font.hpp"
#include "hybridengine/editor/utf8_text.hpp"
#include <string>
#include <algorithm>
#include <cmath>

namespace HybridEngine::Editor {

// t133：UI 文本基准 2x（VGA 8×8×2=16px；CJK GDI 12pt≈16px——两者行距同步）
inline constexpr double kTextScale = 2.0;   // ASCII VGA 缩放（CJK 用 1.0——GDI 12pt 原生≈16px）

// t142：高分辨率自适应——UI 缩放（高分屏文字清晰不挤；=min(w/1280,h/800) 1..3 钳制）
// t-ui-dpi：有效值=clamp(windowScale * dpiScale, 1.0, 3.0)——dpiScale=GetDpiForWindow/96（离屏/无窗口=1.0 → 测试像素恒等）
inline double& UiScaleRef() { static double s = 1.0; return s; }
inline double UiScale() { return UiScaleRef(); }
inline void SetUiScale(double v) { UiScaleRef() = std::clamp(v, 1.0, 3.0); }
inline double UiLineH() { return 25.0 * UiScale(); }   // t143：20px 文本行距（t-ui-dpi：按 UI 缩放——离屏恒等 25.0）
inline double Dpi(double v) { return v * UiScale(); }  // t-ui-dpi：布局数值按 DPI 缩放（离屏=恒等）

// ============ t-text：文字尺寸单一来源（「文字不统一」的根因治理） ============
// 病史：编辑器里同时存在 4 套字号来源——
//   · uikit::Font::Default()  = 内置 8×8 位图（仅 ASCII，中文必乱码）
//   · DrawUiText()            = CjkFont 全局点径 ceil(15*dpi)  ← 正文
//   · 面板标题                = ceil(12*UiScale)              ← 比正文还小（层级倒挂）
//   · 工具栏小字/chips/pills  = ceil(10*dpi)
// 治理：**只留两级**——正文（UiPt）＋副文本（UiSmallPt），全部走 GDI（CjkFont）。
// 位图字体不再参与编辑器 UI（Font 类保留给历史/测试，编辑器代码禁止新用）。
inline int& UiPtRef() { static int s = 15; return s; }

// 设置正文点径（唯一入口：EditorApp::RefreshDpi 调一次/DPI 变化时）
inline void SetUiPt(int pt) {
    pt = std::clamp(pt, 8, 72);
    UiPtRef() = pt;
    CjkFont::SetFontPoint(pt);   // 绘制默认点径与布局真值同步（同一来源，不可能漂移）
}
inline int UiPt() { return UiPtRef(); }                                        // 正文（面板标题/正文/菜单/状态栏）
inline int UiSmallPt() { return std::max(8, (int)std::lround(UiPt() * 0.78)); } // 副文本（chips/徽标/行号/提示）
// 兼容旧调用点的别名（语义已统一到上面两级）
inline int UiTitlePt() { return UiPt(); }

// 文本路由（t141：全量 GDI——VGA 位图对纯 ASCII 渲染镜像乱码（用户截图铁证）；GDI=系统库零第三方——ASCII+CJK 全清晰）
// t-ui-dpi：光栅恒 1:1——DPI 由 SetUiPt(ceil(15*dpi)) 原生承载（近邻放大虚糊撤销沿用 t143 决议）
inline void DrawUiText(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double y, uint32_t color) {
    CjkFont::Draw(r, s, x, y, 1.0, color);
}
inline void DrawUiTextPt(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double y, int pt, uint32_t color) {
    CjkFont::DrawPt(r, s, x, y, pt, color);
}

// ============ t-text：垂直居中（不再依赖写死的「文本高」常数） ============
// 说明（实测数据，别照抄旧注释里的猜测）：CjkFont 的点径是**像素高**（GDI CreateFontW(-pt)），
// 所以 pt=15 → "Ag中" 墨迹 16px/行盒 24px；pt=23（1.5 屏）→ 墨迹 25px/行盒 34px。
// 旧代码用硬编码 `16.0*UiScale()` 当文本高，实际误差只有 1~2px（不是灾难），但它是**与字号脱钩的死数**：
// 一旦改字号/换 DPI，居中就悄悄偏掉。现在改成与绘制同源的墨迹度量，任何字号/DPI 下都精确，
// 且用固定探针串 → 同一行里不同文字基线一致（不会因字符串内容不同而上下跳）。
inline const char* UiTextProbe() { return "Ag中"; }   // 固定探针：含升部/降部/全宽字——代表字体级墨迹盒
inline CjkFont::Metrics UiProbeMetrics(int pt) {
    // t-perf-text：探针("Ag中")度量每帧被 UiCenterY/UiTextInkH 调很多次——按点径缓存，避免每次查 GDI 掩码表。
    static int cachedPt = -1;
    static CjkFont::Metrics cached{};
    if (pt != cachedPt) { cached = CjkFont::MeasureEx(UiTextProbe(), pt); cachedPt = pt; }
    return cached;
}
inline int UiTextInkH(int pt = -1) {
    CjkFont::Metrics m = UiProbeMetrics(pt < 0 ? UiPt() : pt);
    return m.InkH() > 0 ? m.InkH() : (int)std::lround(16.0 * UiScale());
}
// 把文本墨迹**垂直居中**在 [y, y+h) 带内 → 返回绘制 y
inline double UiCenterY(double y, double h, int pt = -1) {
    pt = (pt < 0) ? UiPt() : pt;
    CjkFont::Metrics m = UiProbeMetrics(pt);
    if (m.InkH() <= 0) return y + (h - 16.0 * UiScale()) * 0.5;   // 兜底（无 GDI/空掩码）
    return y + (h - (double)m.InkH()) * 0.5 - (double)m.inkTop;
}
// 兼容旧名（=墨迹居中；旧实现是硬编码 16px——所有调用点自动获得修正）
inline double TextCenteredY(double cy, double h) { return UiCenterY(cy, h); }

// ============ t-text-mono：代码区等宽入口（2026-09-18） ============
// 为什么单独一组：代码区必须**等宽**（见 cjk_font.hpp 的 Face 说明——比例字体在代码区会让列对不齐、
//   光标 x 只能逐段累加、Tab 停止位失去意义）。这里**只做路由**：同一个 GDI 光栅器、同一套度量，
//   只换字体族。字号与 UI 侧同源（MonoPt==UiPt）——避免"绘制在 Mono、度量在 Ui"这类不一致
//   （那正是历史「文字出格」的成因之一）。
inline int MonoPt() { return UiPt(); }
inline int MonoSmallPt() { return UiSmallPt(); }
inline void DrawMonoText(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double y, uint32_t color) {
    CjkFont::DrawPtFace(r, s, x, y, MonoPt(), color, Face::Mono);
}
inline void DrawMonoTextPt(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double y, int pt, uint32_t color) {
    CjkFont::DrawPtFace(r, s, x, y, pt, color, Face::Mono);
}
// 等宽量宽——**代码区布局禁用比例字体的 Measure()**
inline double MonoMeasure(const std::string& s) { return CjkFont::MeasurePtFace(s, MonoPt(), Face::Mono); }
inline double MonoMeasurePt(const std::string& s, int pt) { return CjkFont::MeasurePtFace(s, pt, Face::Mono); }
// 等宽墨迹高（行高/基线由它派生——仍与绘制同源）
inline int MonoInkH(int pt = -1) {
    CjkFont::Metrics m = CjkFont::MeasureExFace(UiTextProbe(), pt < 0 ? MonoPt() : pt, Face::Mono);
    return m.InkH() > 0 ? m.InkH() : (int)std::lround(16.0 * UiScale());
}
// 等宽单字符推进宽（等宽字体里所有 ASCII 同宽——缩进参考线/Tab 停止位/列对齐都基于它）。
// 取 '0'..'9' 的**最小**宽度：若字面实际不是等宽（回退字体），列对齐退化为近似而不是溢出。
inline double MonoAdvance(int pt = -1) {
    const int p = (pt < 0) ? MonoPt() : pt;
    double w = 0.0;
    for (char c = '0'; c <= '9'; ++c) {
        const double d = CjkFont::MeasurePtFace(std::string(1, c), p, Face::Mono);
        if (w <= 0.0 || d < w) w = d;
    }
    return w > 0.0 ? w : 8.0;
}

// ============ t-text：宽度约束（「文字出格」的第二类：横向溢出） ============
// 按码点安全截断到 maxW，超出加省略号（…）。返回可直接绘制的串。
// 为什么按码点而不按字节：中文 3 字节，按字节切会留下半个字符 → 乱码。
inline std::string TruncateToWidth(const std::string& s, double maxW, int pt = -1) {
    pt = (pt < 0) ? UiPt() : pt;
    if (s.empty() || maxW <= 0) return std::string();
    if (CjkFont::MeasurePt(s, pt) <= maxW) return s;
    const std::string ell = "\xE2\x80\xA6";   // U+2026 …
    const double ew = CjkFont::MeasurePt(ell, pt);
    if (ew > maxW) return std::string();      // 连省略号都放不下 → 不画
    std::string out = s;
    while (!out.empty()) {
        const int cp = Utf8CpCount(out);
        if (cp <= 0) break;
        out.resize((size_t)Utf8ByteOffset(out, cp - 1));   // 去掉最后一个码点
        if (CjkFont::MeasurePt(out, pt) + ew <= maxW) return out + ell;
    }
    return ell;
}
// 带宽度约束的绘制（自动省略号）：x 处起、最多 maxW 宽
inline void DrawUiTextClipped(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double y, double maxW, uint32_t color, int pt = -1) {
    const std::string t = TruncateToWidth(s, maxW, pt);
    if (t.empty()) return;
    if (pt < 0) DrawUiText(r, t, x, y, color);
    else DrawUiTextPt(r, t, x, y, pt, color);
}
// 居中 + 宽度约束（按钮/页签/chips 通用）
inline void DrawUiTextCenteredClipped(HybridEngine::Platform::IRenderer& r, const std::string& s, double x, double w, double y, uint32_t color, int pt = -1) {
    const std::string t = TruncateToWidth(s, w, pt);
    if (t.empty()) return;
    const double tw = (pt < 0) ? CjkFont::Measure(t, 1.0) : CjkFont::MeasurePt(t, pt);
    if (pt < 0) DrawUiText(r, t, x + (w - tw) * 0.5, y, color);
    else DrawUiTextPt(r, t, x + (w - tw) * 0.5, y, pt, color);
}
// 文本在给定带宽内的量宽（便捷）
inline double UiTextW(const std::string& s, int pt = -1) {
    return (pt < 0) ? CjkFont::Measure(s, 1.0) : CjkFont::MeasurePt(s, pt);
}

} // namespace HybridEngine::Editor
