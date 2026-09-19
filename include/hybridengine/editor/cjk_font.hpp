#pragma once
#include "hybridengine/platform/renderer.hpp"
#include <string>

namespace HybridEngine::Editor {

// t-text-mono（2026-09-18）：字体族。**为什么需要这个维度**——
//   编辑器 UI 一直是"比例字体贯穿一切"。正文/标题用它没问题，但**代码区**用比例字体做布局是错的：
//   'i' 与 'W' 宽度不同 ⇒ 列对不齐（`:` 对齐的成员声明看着歪）、光标 x 只能靠逐段量宽累加、
//   Tab 停止位失去意义。VS 看起来"专业"的第一步就是**代码区等宽**。
//   做法：引入族维度，而**不是**再造一套字体系统——同一个 GDI 光栅器、同一套缓存与度量，
//   只换 CreateFontW 的字面/字符集/字距。绘制与布局仍同源（同一份 Metrics），不可能漂移。
enum class Face {
    Ui,     // 界面正文（微软雅黑 / GB2312 / 变宽）——**默认；全部旧调用点走它，像素逐位不变**
    Mono,   // 代码等宽（Consolas / DEFAULT_CHARSET / 定宽）——脚本编辑器专用
};

// t125：中文光栅字体（Win32 GDI 系统字体——CreateFontW(GB2312)+DrawTextW→32bpp 位图缓存；系统库（gdi32）=零第三方红线内；v2 EngineText 同法）
class CjkFont {
public:
    // t-text：文本度量（与绘制同源）。**垂直居中的唯一正确依据**——
    // 历史 bug：布局用硬编码 16px 行高居中，而实际光栅按 15pt×dpi(=23pt≈31px) 绘制
    // → 每行文字整体下沉约 7px、下缘出格（用户实测「部分文字显示出格」）。
    // inkTop/inkBottom = 该串掩码中首个/末个有墨迹的行（相对绘制原点 y 的偏移）。
    struct Metrics {
        int w = 0;            // 量宽（与 Measure/MeasurePt 完全一致，含 GDI 右/下 padding）
        int lineH = 0;        // 行盒高（掩码高）
        int inkTop = 0;       // 首个墨迹行
        int inkBottom = -1;   // 末个墨迹行（含）；< inkTop = 无墨迹
        int InkH() const { return inkBottom >= inkTop ? inkBottom - inkTop + 1 : 0; }
    };

    // UTF-8 含 CJK 判定（0x4E00-0x9FFF 等）
    static bool ContainsCjk(const std::string& s);
    // 绘制：utf8 文本→位图缓存→IRenderer 像素输出（透明背景；color 着色；scale=整数倍）
    static void Draw(HybridEngine::Platform::IRenderer& r, const std::string& utf8, double x, double y, double scale, uint32_t color);
    // 量宽（缓存命中；未命中=近似 16*len*scale）
    static double Measure(const std::string& utf8, double scale);
    // t-ui：指定点径绘制/量宽（与全局 SetFontPoint 独立缓存——面板头 18px=14pt / 视图 chips 13px=10pt 等小字）
    static void DrawPt(HybridEngine::Platform::IRenderer& r, const std::string& utf8, double x, double y, int pt, uint32_t color);
    static double MeasurePt(const std::string& utf8, int pt);

    // —— t-text-mono：指定字体族的重载（Face::Ui 版本与上面**完全等价**——内部就是转调）——
    static void DrawFace(HybridEngine::Platform::IRenderer& r, const std::string& utf8, double x, double y, double scale, uint32_t color, Face face);
    static double MeasureFace(const std::string& utf8, double scale, Face face);
    static void DrawPtFace(HybridEngine::Platform::IRenderer& r, const std::string& utf8, double x, double y, int pt, uint32_t color, Face face);
    static double MeasurePtFace(const std::string& utf8, int pt, Face face);
    static Metrics MeasureExFace(const std::string& utf8, int pt, Face face);
    // 族名（诊断/测试用）
    static const char* FaceName(Face face);

    static void SetFontPoint(int pt);   // 默认 12pt（≈16px——与 VGA 2x 同基线）
    static int  FontPoint();            // t-text：当前默认点径（布局与绘制同源的唯一真值）
    static void ClearCache();           // 测试/主题切换
    // t-perf：绘制路径计数——span 快路径 vs **逐像素回退**（非整数坐标时）。
    // 为什么要有：逐像素回退每个亮点一次 FillRect，文字一多就是灾难；必须能测出来而不是猜。
    struct EmitStats { uint64_t spanDraws = 0, pixelDraws = 0, rectsEmitted = 0; };
    static EmitStats& Emit();           // 可写引用（ClearEmit 复位）
    static void ClearEmit() { Emit() = EmitStats{}; }

    static Metrics MeasureEx(const std::string& utf8, int pt);
    // t5：缓存命中统计（mask=L1 GDI 掩码；span=L2 展开）
    static void Stats(uint64_t& maskHits, uint64_t& maskMisses, uint64_t& spanHits, uint64_t& spanMisses);
};

} // namespace HybridEngine::Editor
