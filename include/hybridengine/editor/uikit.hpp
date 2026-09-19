#pragma once
#include "hybridengine/platform/renderer.hpp"
#include <functional>
#include <string>
#include <vector>

namespace HybridEngine::Editor::UiKit {

// M3.1：EditorUiKit（自绘控件——经 IRenderer 9 原语；内嵌 VGA 8x8 位图字体；暗色主题）
// ui-v3：Slate 分层配色（结构化分层细节——base→panel→group→strip 四层递进；accent=teal 非默认蓝）
struct Rect { double x = 0, y = 0, w = 0, h = 0; };

struct Style {
    uint32_t bg = 0xFF0B0F17;      // base：深石墨黑（专业 DCC 暗色底）
    uint32_t panel = 0xFF121826;   // 面板体：冷调深蓝灰
    uint32_t group = 0xFF1A2233;   // 分组：略亮面板（组件/字段组）
    uint32_t strip = 0xFF161D2C;   // 条带：菜单/面板头/状态栏
    uint32_t hover = 0xFF24314A;   // hover：蓝灰高亮
    uint32_t border = 0xFF2A3852;  // 边框：1px 冷灰分隔
    uint32_t text = 0xFFF2F6FF;    // 主文本：近白
    uint32_t dim = 0xFF93A4C0;     // 次级文本：冷灰蓝
    uint32_t accent = 0xFF2DD4BF;  // 主强调：teal / cyan
    uint32_t warn = 0xFFFFB84D;    // 警告：琥珀
    uint32_t error = 0xFFFF6B6B;   // 错误：珊瑚红
    uint32_t selected = 0xFF1A3A44; // 选中：teal 暗底
    double padding = 8.0;          // 专业 DCC 更舒展的组件内边距
    double fontScale = 2.0;   // t133：UI 文本 2x（VGA 8×8×2=16px）
};

class Font {
public:
    static const Font& Default();
    double Measure(const std::string& text, double scale = 1.0) const;   // 像素宽（8px/字符+1 间距）
    void Draw(HybridEngine::Platform::IRenderer& r, const char* text, double x, double y, double scale, uint32_t color) const;
    static double Ascent() { return 8.0; }
};

// t-text：列表/树/PropertyRow 的统一行距（由当前字号**派生**——不是死数）。
// 为什么必须是函数：绘制与命中测试共用同一行距，否则改字号后点击位置会错位（历史上是硬编码 22）。
double ListLineH();

class Widget {
public:
    virtual ~Widget() = default;
    Rect Bounds() const { return bounds_; }
    void SetBounds(const Rect& b) { bounds_ = b; }
    bool Hit(double x, double y) const { return x >= bounds_.x && x < bounds_.x + bounds_.w && y >= bounds_.y && y < bounds_.y + bounds_.h; }
    virtual void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) = 0;
    virtual bool OnClick(double x, double y) { (void)x; (void)y; return false; }
    virtual bool OnHover(double x, double y) { hovered_ = Hit(x, y); return hovered_; }
    bool Hovered() const { return hovered_; }
protected:
    Rect bounds_{};
    bool hovered_ = false;
};

class Label : public Widget {
public:
    explicit Label(std::string text = "") : text_(std::move(text)) {}
    void SetText(const std::string& t) { text_ = t; }
    const std::string& Text() const { return text_; }
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
private:
    std::string text_;
};

class Button : public Widget {
public:
    explicit Button(std::string text = "") : text_(std::move(text)) {}
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double, double) override { if (click_) { click_(); return true; } return false; }
    void SetClick(std::function<void()> f) { click_ = std::move(f); }
private:
    std::string text_;
    std::function<void()> click_;
};

class CheckBox : public Widget {
public:
    bool Value = false;
    std::function<void(bool)> OnChanged;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double, double) override { Value = !Value; if (OnChanged) OnChanged(Value); return true; }
};

class List : public Widget {
public:
    std::vector<std::string> Items;
    int Selected = -1;
    std::function<void(int)> OnSelect;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double x, double y) override;   // 行命中→Selected
};

class TextEdit : public Widget {
public:
    std::string Value;
    bool Focused = false;
    std::function<void(const std::string&)> OnChange;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double, double) override { Focused = true; return true; }
    bool OnKey(int key, bool down);              // 键码路径（VK：退格/回车/Delete/Home/End）
    bool OnChar(uint32_t cp);                    // t-text：字符路径（WM_CHAR 码点——中文/emoji 的唯一正确入口）
    void Clear();                                // 清空（Esc 取消编辑等）
    const char* Cursor() const { return "|"; }
};

class TreeView : public Widget {
public:
    struct Node { std::string name; long id = 0; bool expanded = true; std::vector<Node> children; };
    std::vector<Node> Nodes;
    long Selected = 0;   // 节点 id（0=无）
    std::function<void(long)> OnSelect;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double x, double y) override;
private:
    void DrawNode(HybridEngine::Platform::IRenderer& r, const Style& s, const Node& n, double& yy, int depth);
    bool HitNode(const Node& n, double x, double y, double& yy, int depth);
};

class TabBar : public Widget {
public:
    std::vector<std::string> Tabs;
    int Selected = 0;
    std::function<void(int)> OnSelect;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double x, double y) override;
private:
    double pad_ = 6.0;   // P3-2：Draw 记录的 padding（OnClick 命中公式与绘制一致）
};

// M3.5/t129：虚线矩形（拖拽目标高亮——DR-4 视觉）
void DrawDashedRect(HybridEngine::Platform::IRenderer& r, double x, double y, double w, double h, uint32_t color, int dash = 6, int gap = 3);

// M3.5/t129：可折叠分组头（▲▼ 图标+标题——点击=折叠切换；折叠=ContentHeight 0）
class FoldableSection : public Widget {
public:
    explicit FoldableSection(std::string title = "") : title_(std::move(title)) {}
    void SetTitle(const std::string& t) { title_ = t; }
    const std::string& Title() const { return title_; }
    bool Open() const { return open_; }
    void SetOpen(bool v) { open_ = v; }
    void Toggle() { open_ = !open_; }
    double HeaderH() const { return headerH_; }               // 头高（命中区 = 头行±4px）
    void SetHeaderH(double h) { headerH_ = h; }
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;   // 仅头部（content=自行实现）
    bool OnClick(double x, double y) override;                // 头命中→toggle（IN-1）
private:
    std::string title_;
    bool open_ = true;
    double headerH_ = 22.0;
};

// M3.5/t129：属性字段行（label+编辑控件+类型——未映射=只读灰显+类型名 IN-3）
class PropertyRow : public Widget {
public:
    enum class Kind { Float, Int, Bool, String, Vec3, Enum, AssetRef, Unmapped };
    std::string Label;
    std::string Value;              // 当前值（字符串——编辑时见 TextEdit）
    Kind K = Kind::Unmapped;
    bool ReadOnly = false;          // Play/未映射=灰显（PS-1）
    bool RuntimeHot = false;        // Play 中运行时值=琥珀着色（PS-1）
    std::string TypeHint;           // Unmapped：类型名
    std::function<void(const std::string&)> OnCommit;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;
    bool OnClick(double x, double y) override;
    bool OnKey(int key, bool down);   // 聚焦输入（TextEdit 语义）
    bool Focused = false;
private:
    UiKit::TextEdit edit_;          // 提用（值编辑）
};

class ScrollView : public Widget {
public:
    double Offset = 0;
    double ContentHeight = 0;
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;   // 裁剪区（内容由子绘制）
    void Begin(HybridEngine::Platform::IRenderer& r, const Style& s);           // push clip
    void End(HybridEngine::Platform::IRenderer& r);                            // pop clip
    virtual bool OnMouseWheel(double dy) { Offset -= dy * 12; return true; }
    double ContentY() const { return bounds_.y - Offset; }
};

class Row : public Widget {   // 简单水平行（布局辅助）
public:
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;   // 不绘制（容器）
};

class Layout : public Widget {   // 最小布局（垂直堆叠）
public:
    void Add(Widget* w, double h) { children_.push_back({w, h}); }
    void Draw(HybridEngine::Platform::IRenderer& r, const Style& s) override;   // 分配子 bounds
private:
    struct Item { Widget* w; double h; };
    std::vector<Item> children_;
};

} // namespace HybridEngine::Editor::UiKit