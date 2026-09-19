#pragma once
#include "hybridengine/editor/csharp_lexer.hpp"
#include "hybridengine/editor/python_lexer.hpp"
#include "hybridengine/editor/script_text.hpp"
#include "hybridengine/editor/uikit.hpp"
#include "hybridengine/platform/renderer.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// P3：脚本编辑面板（应用内写脚本）。
//
// 职责边界：
//  · 只管「文本 + 高亮 + 光标/选区 + 滚动 + 输入 + 存盘」；
//  · **不**碰引擎场景（与 ScenePanel 平级，占中心区，由 EditorApp 决定显示哪一个）；
//  · 高亮交给 CSharpLexer（纯函数），本类只负责把 token 画成颜色。
//
// 字体与度量：编辑器统一用 GDI（Microsoft YaHei，比例字体），因此**光标与选区一律按测量定位**
// （逐 token 累加 Measure），不做「等宽假设」——否则比例字体下光标会漂。
// 已知取舍：非等宽字体下同一列的字符不严格对齐（等宽字体变体列为后续项，见编辑指南）。
class ScriptEditorPanel {
public:
    // 一行的像素高度（与字号 12pt≈16px 匹配；比 UI 行距 25 紧凑）
    static double LineHeight();
    static double GlyphHeight();
    static double GutterWidth(int lineCount);

    // —— 文件 ——
    // assetPath=虚拟资产路径（Assets/x.cs，用于标题/重编译关联）；absPath=真实磁盘路径。
    bool Open(const std::string& assetPath, const std::string& absPath);
    bool Save();                                  // 写回 absPath；成功后清 dirty
    bool SaveAs(const std::string& absPath);
    void Close();                                 // 清空并回到「无文件」态
    bool HasFile() const { return !absPath_.empty(); }
    bool Dirty() const { return buf_.Dirty(); }
    const std::string& AssetPath() const { return assetPath_; }
    const std::string& AbsPath() const { return absPath_; }
    const std::string& Status() const { return status_; }   // 最近一次打开/保存结果（控制台/状态栏用）
    bool IsCSharp() const;                                  // 依据扩展名（.cs）
    // t-py：语言判定（高亮按语言选词法器）。Python **只做编辑/高亮，不做执行**——
    // 引擎内没有内嵌 CPython 解释器（零第三方红线），这一点在 UI/文档里都明说，不假装能跑。
    enum class Lang { Plain = 0, CSharp, Python };
    Lang Language() const;
    bool IsPython() const { return Language() == Lang::Python; }
    bool Highlighted() const { return Language() != Lang::Plain; }

    // —— 文本（测试/宿主直驱）——
    ScriptBuffer& Buffer() { return buf_; }
    const ScriptBuffer& Buffer() const { return buf_; }

    // —— 几何/滚动 ——
    void SetBounds(const UiKit::Rect& rc);        // EditorApp 每帧告知绘制区
    UiKit::Rect Bounds() const { return bounds_; }
    int FirstLine() const { return firstLine_; }
    int FirstCol() const { return firstCol_; }
    int VisibleLines() const;
    double LineTop(int line) const;               // 该行的屏幕 y（可能不可见）
    double TextOriginX() const;                   // 代码区左边界（=bounds.x+行号槽宽）
    void EnsureCaretVisible();
    void ScrollBy(int dLines, int dCols);

    // —— 绘制 ——
    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s);

    // —— 输入（返回 true=已消费，调用方不要再当全局快捷键处理）——
    bool OnKey(int vk, bool ctrl, bool shift);    // VK 码（与 EditorApp::HandleKey 同源）
    void OnChar(uint32_t cp);                     // WM_CHAR 路径（含中文/IME 结果）
    bool OnWheel(double x, double y, int delta, bool shift = false);   // shift=水平滚动（长行）
    bool OnMouseDown(double x, double y, bool shift);   // 命中代码区=定位光标并起选
    bool OnMouseMove(double x, double y);               // 拖选
    bool SelectWordAt(double x, double y);              // t-ux：双击选词（词/空白/符号连续段）
    void OnMouseUp() { dragging_ = false; }
    bool ClickToCaret(double x, double y);        // 屏幕坐标→光标（列按测量取最近边界）
    // t-vs：括号匹配——找到与光标处（或**其左邻**）括号配对的那一个。
    // 返回 false = 光标两侧都不是括号，或找不到配对（**不配对时不高亮、不报错**，静默）。
    // 公开理由（与 ClickToCaret/SelectWordAt 同类）：匹配逻辑是纯查询，跨行扫描的正确性
    //   只能靠用例逐条钉（同类嵌套/跨行/不配对），藏在 private 里就只能靠"画出来的像素"间接测。
    bool FindMatchingBracket(int& al, int& ac, int& bl, int& bc) const;

    // —— 测试/宿主钩子 ——
    // 存盘前的回调（EditorApp 用它触发项目脚本重编译；返回 false=不改状态）
    std::function<void(const std::string& absPath)> OnSaved;
    // 内容被编辑（用于标脏标题/状态栏刷新）
    std::function<void()> OnEdited;

private:
    void HandleEdit(bool changed);
    uint32_t PeekAt(int line, int col) const;   // 取某行列的码点（越界=0）
    void GotoMatchingBracket();                 // Ctrl+]：跳到配对括号
    // t-undo：编辑**前**存一步历史（typing=true 表示可与其他连续输入合并）。见实现处说明。
    void SnapshotForEdit(bool typing);
    void BreakTypingCoalesce();
    bool coalescingTyping_ = false;   // t-undo：上一步是否为"可合并的连续输入"
    // t-perf-lex：高亮行缓存——Draw 只在 Buffer 修订号/语言变化时增量重词法。
    // 病史（实测 5000 行脚本滚到底）：旧实现每帧从文件头 LexLine 到可见末行=3.42ms/帧；
    // 现在滚动/光标移动=0 词法，编辑=只从最早变更行重词法。
    void EnsureLexed(int lastLine);
    void ResetLexCache();
    // t-ux-line：鼠标命中缓存——按行+revision 缓存每码点宽度前缀和；长行拖动不再 O(n²) 反复量整段前缀。
    void EnsureLineMetrics(int line);
    int CaretFromX(int line, double x);
    std::vector<std::vector<Token>> lineTokens_;
    std::vector<CSharpLexer::State> csStates_;   // csStates_[i]=第 i 行后的跨行状态
    std::vector<PythonLexer::State> pyStates_;
    uint64_t lexRevision_ = ~0ull;
    int lexedUpTo_ = -1;                          // 已词法到的最后一行（-1=无）
    Lang lexLang_ = Lang::Plain;
    int lexLineCount_ = 0;
    int metricLine_ = -1;                         // 命中度量缓存的行
    uint64_t metricRevision_ = ~0ull;
    int metricPt_ = -1;                           // 缓存建立时的字号/缩放（DPI 变化必须失效）
    double metricScale_ = 0.0;
    std::vector<double> metricPrefix_;            // [c]=前 c 个码点的显示宽（比例字逐字符累加）

    ScriptBuffer buf_;
    std::string assetPath_;
    std::string absPath_;
    std::string status_;
    UiKit::Rect bounds_{};
    int firstLine_ = 0;
    int firstCol_ = 0;        // 水平滚动起点（码点）
    bool dragging_ = false;
};
} // namespace HybridEngine::Editor
