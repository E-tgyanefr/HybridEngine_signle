// 节点图面板（连线式编程的可视化编辑）——t-graph
//
// 定位：**编辑引擎运行时的图本身**（图由 GraphRuntime 每帧解释），所以改完立即生效、无编译。
// 三种操作：① 左侧调色板点一下加节点 ② 拖标题移动节点 ③ 从输出针拖到输入针连线
//   （点输入针=断开；Delete=删除选中节点；空白处拖动=平移画布）。
//
// 为什么面板直接编辑 runtime 的图：用户的选型是"运行时解释"——面板改的就是那份数据，
//   不必走"存盘→重载"才生效（存盘只是持久化）。
#pragma once

#include "hybridengine/editor/uikit.hpp"
#include "hybridengine/editor/editor_text.hpp"
#include "hybridengine/app/graph_asset.hpp"

#include <string>
#include <vector>

namespace HybridEngine::Platform { class IRenderer; }

namespace HybridEngine::Editor {

constexpr double kPaletteW = 190.0;   // 左侧调色板宽（画布原点与命中都以它为准）

class GraphPanel {
public:
    struct Rect { double x = 0, y = 0, w = 0, h = 0; };

    void SetGraph(HybridEngine::App::GraphAsset* g, const std::string& assetPath);
    const std::string& AssetPath() const { return path_; }
    bool HasGraph() const { return graph_ != nullptr; }

    void SetBounds(const Rect& rc) { bounds_ = rc; }
    const Rect& Bounds() const { return bounds_; }

    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& style, const Rect& rc);

    // 交互（编辑器把鼠标/键盘转发进来；坐标=渲染器设计坐标）
    bool OnClick(double x, double y);                 // 返回 true=已消费
    bool OnDrag(double x, double y);                  // 按住移动
    bool OnRelease(double x, double y);               // 松开（完成连线/结束拖动）
    bool OnDeleteKey();                               // Delete：删选中节点
    int SelectedNode() const { return selNode_; }
    int NodeCount() const { return graph_ ? (int)graph_->nodes.size() : 0; }
    int LinkCount() const { return graph_ ? (int)graph_->links.size() : 0; }
    double Zoom() const { return zoom_; }
    void ZoomBy(double factor);
    // t-graph：以 (x,y) 为锚点缩放——光标下的画布点保持不动（主流节点编辑器的行为）
    void ZoomAt(double x, double y, double factor);
    // 框选（rubber band）：空白处按住拖动即框选，命中的节点全部选中
    bool BoxSelecting() const { return boxSel_; }
    Rect BoxRect() const;                     // 框选矩形（绘制/测试用）
    const std::vector<int>& SelectedNodes() const { return selNodes_; }

    // t-graph-member：**脚本成员下拉**——工程脚本的类型/方法/字段直接列在调色板里，点一下加节点。
    //   （否则 script.call 的 target/method 只能靠手填，图很难真正用起来。）
    struct ScriptMember {
        std::string type;      // 脚本类型全名（如 Ns.Widget）
        std::string name;      // 方法名或字段名
        bool isMethod = true;
        bool takesArg = false; // 方法是否带一个 double 参数（决定节点上要不要接 arg）
        bool isWrite = false;  // 字段/属性：true=写（script.set），false=读（script.get）
    };
    // t-graph-pick：**已装载图列表**（面板顶部可点切换）。没有它就只能开"第一张"，工程里多张图时没法选。
    void SetGraphList(std::vector<std::string> g) { graphs_ = std::move(g); }
    const std::vector<std::string>& GraphList() const { return graphs_; }
    int ConsumeGraphRequest() { const int r = pendingGraph_; pendingGraph_ = -1; return r; }   // 编辑器每帧取一次
    double GraphRowY(int index) const {
        for (const auto& gr : graphRows_) if (gr.first == index) return gr.second + 4.0;
        return -1.0;
    }
    void SetScriptMembers(std::vector<ScriptMember> m) { members_ = std::move(m); }
    const std::vector<ScriptMember>& ScriptMembers() const { return members_; }
    // 成员行命中区（绘制时记录；测试与自动化按它精确点击，不猜坐标）
    double MemberRowY(int index) const {
        for (const auto& mr : memberRows_) if (mr.first == index) return mr.second + 4.0;
        return -1.0;
    }
    int AddScriptMemberNode(int index);   // 点第 index 项 → 加一个预填好参数的节点（返回节点 id）
    // t-graph-edit：**节点参数编辑**——选中节点后，参数列在调色板下方，点一下即可改
    //   （否则 script.call 的 target/method、draw.text 的文本只能靠手写 .hgraph，图就没法真正用起来）
    bool EditingParam() const { return !editParam_.empty(); }
    const std::string& EditParamName() const { return editParam_; }
    bool OnKey(int vk, bool ctrl, bool shift);      // 编辑中接管按键（Enter 提交 / Esc 取消）
    bool OnChar(uint32_t cp);                       // 中文也能输入（WM_CHAR 通路）
    void CommitParamEdit();
    void CancelParamEdit();
    // 参数行命中区（绘制时记录；测试用同一份）
    struct ParamRow { std::string name; bool isText; double y0 = 0, y1 = 0; };
    const std::vector<ParamRow>& ParamRows() const { return paramRows_; }
    // 供测试/自动化：按类型加节点（返回 id）——与调色板点击同一条路径
    int AddNodeOfType(const std::string& type);
    const Rect& NodeRect(int id) const;               // 节点框（命中/测试用）
    // t-wire：某条连线的光栅像素（列 → y）。给"连线是否平滑/连续"提供**可直接断言**的数据，
    // 不必去猜帧缓冲里哪些像素是线（第一版测试把白色端口圆点误判成线，结论就错了）。
    const std::vector<std::pair<int, int>>& WirePixels(int linkIndex) const {
        static const std::vector<std::pair<int, int>> kEmpty;
        return (linkIndex >= 0 && linkIndex < (int)wirePx_.size()) ? wirePx_[(size_t)linkIndex] : kEmpty;
    }
    void EnsureWires() const { if (wiresDirty_) const_cast<GraphPanel*>(this)->RebuildWires(); }
    bool PinAt(double x, double y, int& nodeId, std::string& port, bool& isInput) const;

private:
    struct Hit { int node = 0; std::string port; bool isInput = true; bool valid = false; };

    // 画布原点 = 面板左边 + **调色板宽度**（病史：第一版漏了这一项 → 画布 x=0 落在调色板底下，
    //   左侧节点被调色板盖住看不见，但连线还画着——截图一眼抓出来）。
    double CanvasX(double screenX) const { return (screenX - bounds_.x - kPaletteW + panX_) / zoom_; }
    double CanvasY(double screenY) const { return (screenY - bounds_.y + panY_) / zoom_; }
    double ScreenX(double canvasX) const { return bounds_.x + kPaletteW - panX_ + canvasX * zoom_; }
    double ScreenY(double canvasY) const { return bounds_.y - panY_ + canvasY * zoom_; }
    Rect NodeScreenRect(const HybridEngine::App::GraphNode& n) const;
    Hit HitTest(double x, double y) const;            // 先针后节点
    void Layout();                                    // 计算节点尺寸（按端口数/标题宽度）

    HybridEngine::App::GraphAsset* graph_ = nullptr;
    std::string path_;
    Rect bounds_{};
    // 视图
    double panX_ = 0, panY_ = 0, zoom_ = 1.0;
    // 交互状态
    int selNode_ = 0, dragNode_ = 0;         // selNode_=单选（主选中；面板高亮用）
    std::vector<int> selNodes_;              // t-graph：多选集合（框选/Ctrl 点选）
    bool boxSel_ = false;                    // 正在框选
    double boxX0_ = 0, boxY0_ = 0, boxX1_ = 0, boxY1_ = 0;
    double dragDX_ = 0, dragDY_ = 0;
    bool wireDrag_ = false;
    int wireFromNode_ = 0;
    std::string wireFromPort_;
    double wireX_ = 0, wireY_ = 0;
    bool panning_ = false;
    double panStartX_ = 0, panStartY_ = 0, panOrigX_ = 0, panOrigY_ = 0;
    // 节点框缓存（绘制与命中同源）
    mutable std::vector<std::pair<int, Rect>> nodeRects_;   // 绘制/命中加速用（几何真值仍是节点坐标）
    mutable Rect tmpRect_{};                                // NodeRect 的返回值载体（实时计算）
    // t-wire：连线**逐列贝塞尔**的像素缓存。原来用 16 段粗矩形拼"平滑"，看上去是台阶（用户反馈）。
    //   现在按列精确求 y（水平控制点 → x 对 t 线性），并把结果缓存：只在编辑/平移/缩放时重算，
    //   每帧只做填充（编辑器每帧重绘，不能每帧重算贝塞尔）。
    bool wiresDirty_ = true;
    std::vector<std::vector<std::pair<int, int>>> wirePx_;   // 每条连线：若干 (x,y) 像素
    void RebuildWires();
    void InvalidateWires() { wiresDirty_ = true; }
    double paletteW_ = 0;
    // t-graph-edit：参数编辑状态
    std::string editParam_;                   // 正在编辑的参数名（空=没在编辑）
    HybridEngine::Editor::UiKit::TextEdit editBox_;   // 复用 UI 控件（含中文/退格/Home/End）
    std::vector<std::string> graphs_;                 // t-graph-pick：已装载图（可点切换）
    int pendingGraph_ = -1;                           // 请求切换的下标（编辑器取走）
    std::vector<std::pair<int, double>> graphRows_;   // (下标, y0) 命中区
    std::vector<ParamRow> paramRows_;
    std::vector<ScriptMember> members_;          // t-graph-member：脚本成员（下拉项）
    // t-graph-pal：调色板**分类筛选**——节点类型一多，一列列不下（成员区/参数区被挤出面板）。
    //   点分类标题=只看该类（再点一次=全部）。这让"通用节点 + 脚本成员 + 参数"三者能共存。
    std::string paletteCat_;
    std::vector<std::pair<std::string, double>> catRows_;   // (分类名, y0) 命中区
    std::vector<std::pair<int, double>> memberRows_;   // (成员下标, 行 y0) —— 绘制时记录，点击时用
    void BeginParamEdit(const std::string& name, bool isText, double y0, double y1);
};

} // namespace HybridEngine::Editor
