// 节点图（连线式编程）——**资产模型 + 节点注册表 + 运行时解释器**
//
// 定位（与用户的选型一致）：**引擎内运行时解释**——不生成代码，引擎每帧自己走图求值。
//   好处：改完立即生效、零编译；代价：节点语义必须由引擎实现（就是本文件）。
//
// 与引擎其余部分的关系：
//   · 图资产 = `.hgraph`（JSON，走 core 的 msjson）——可进 Assets/、可版本管理、可手改。
//   · 求值结果：变量写在 GraphRuntime 的变量槽里；绘制节点**排入引擎的绘制命令队列**（RndOp），
//     所以图和脚本共用同一条渲染/回放/黄金帧路径（不另开旁路）。
//   · 事件节点按引擎帧相位触发：Start（一次）/ Update（每帧）/ Key（按键按下沿）。
//
// 求值模型：**exec 链 + 数据拉取**
//   · 节点有一个可选 exec 输入 / exec 输出（执行顺序），数据输入在需要时**递归拉取**（带每帧记忆）。
//   · 这样图既能表达"先算再画"的顺序，又能表达纯数据流（数学节点互相接线）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace HybridEngine::App {

// —— 端口类型（用于连线合法性检查与面板配色）——
enum class GraphPinType : uint8_t { Exec = 0, Number, Bool, Vec2, Color, Text };

struct GraphPin {
    int nodeId = 0;
    std::string name;      // 端口名（"in"/"a"/"b"/"out"/"exec"…）
    GraphPinType type = GraphPinType::Number;
    bool isInput = true;
};

struct GraphNode {
    int id = 0;
    std::string type;      // 注册表键，如 "event.update" / "math.add" / "draw.circle"
    double x = 0, y = 0;   // 画布坐标（面板持久化用）
    // 节点参数（无连线时作为常量；有连线则以连线为准）
    std::unordered_map<std::string, double> nums;
    std::unordered_map<std::string, std::string> strs;
};

struct GraphLink {
    int fromNode = 0, toNode = 0;
    std::string fromPort, toPort;
};

struct GraphAsset {
    std::string name = "Graph";
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;

    const GraphNode* Find(int id) const;
    GraphNode* Find(int id);
    int AddNode(const std::string& type, double x, double y);   // 返回新 id（自增）
    bool Connect(int fromNode, const std::string& fromPort, int toNode, const std::string& toPort);
    void Disconnect(int toNode, const std::string& toPort);     // 一个输入端口只接一条线
    bool RemoveNode(int id);                                    // 连带删除相关连线
    std::string ToJson() const;
    static bool FromJson(const std::string& text, GraphAsset& out, std::string& err);
    int NextId() const;
};

// —— 节点注册表：类型 → 端口定义 + 语义描述（面板调色板与校验共用同一份真值）——
struct GraphNodeDef {
    const char* type;
    const char* category;      // "事件" / "数学" / "逻辑" / "变量" / "绘制"
    const char* title;         // 面板显示名（中文主 + 英文括注）
    // 端口（顺序即面板上的排列顺序）
    std::vector<GraphPin> inputs;
    std::vector<GraphPin> outputs;
    const char* doc;           // 一行说明（悬停/文档用）
};

const std::vector<GraphNodeDef>& GraphNodeRegistry();
const GraphNodeDef* GraphNodeDefOf(const std::string& type);

// —— 运行时：装载若干图资产并按帧求值 ——
class GraphRuntime {
public:
    struct VarSlot { double v = 0; };

    // 装载/卸载（按资产路径；同名重复装载=替换）
    bool Load(const std::string& assetPath, const GraphAsset& g, std::string& err);
    bool Unload(const std::string& assetPath);
    void Clear();
    size_t Count() const { return graphs_.size(); }
    std::vector<std::string> Paths() const;
    GraphAsset* Find(const std::string& assetPath);

    // 按帧相位求值：phase 0=Start（仅首帧一次），1=Update（每帧）
    // pushOp 由调用方提供（引擎注入：把绘制节点排进 RndOp 队列）——保持本文件不依赖绑定层。
    struct DrawSink {
        // 与 IRenderer 语义一致的 4 个绘制原语（图节点只用到这些）
        void (*rect)(void* ctx, double x, double y, double w, double h, uint32_t color) = nullptr;
        void (*circle)(void* ctx, double cx, double cy, double r, uint32_t color) = nullptr;
        void (*ring)(void* ctx, double cx, double cy, double r, double th, uint32_t color) = nullptr;
        void (*text)(void* ctx, double x, double y, double size, const char* utf8, uint32_t color) = nullptr;
        void* ctx = nullptr;
    };
    void SetDrawSink(const DrawSink& s) { sink_ = s; }
    // t-graph-script：**脚本出口**——让图能读写别的脚本的字段、调用它们的方法（"把脚本之间的关系连起来"）。
    // 与绘制出口同样是**注入式**：app 层不依赖绑定层，绑定层用 ScriptRegistry 反射实现这三个钩子。
    //   getField/setField：按（脚本类型或实例键，字段名）读写；数值型（bool 用 0/1）。
    //   call：按（目标，方法名，一个 double 参数）调用；返回返回值（void → 0）。ok 用于回填是否成功。
    struct ScriptSink {
        void* ctx = nullptr;
        double (*getField)(void* ctx, const char* target, const char* field, int* ok) = nullptr;
        void (*setField)(void* ctx, const char* target, const char* field, double v, int* ok) = nullptr;
        double (*call)(void* ctx, const char* target, const char* method, double arg, int* ok) = nullptr;
        // 枚举工程脚本成员（面板调色板用）：写 JSON 到 out，返回字节数
        int (*members)(void* ctx, char* outJson, int cap) = nullptr;
    };
    void SetScriptSink(const ScriptSink& s) { scriptSink_ = s; }
    const ScriptSink& ScriptBridge() const { return scriptSink_; }
    // 供测试/诊断：某节点上一次执行的成败
    bool LastScriptOk() const { return lastScriptOk_; }
    const DrawSink& Sink() const { return sink_; }   // 宿主可据此增补（例如补文字出口）
    // 按键输入（Key 事件节点用）：vk → 本帧是否按下沿
    void SetKeyState(const std::unordered_map<int, bool>* keys) { keys_ = keys; }

    void Tick(double dt, int phase);

    // 变量访问（测试/调试用；也便于以后做"变量面板"）
    double GetVar(const std::string& name) const;
    void SetVar(const std::string& name, double v);
    // 上一帧每个节点的求值结果（诊断/可视化用）
    double NodeOut(const std::string& assetPath, int nodeId, const std::string& port) const;

private:
    struct Instance {
        std::string path;
        GraphAsset graph;
        bool started = false;
    };
    struct EvalCtx {
        Instance* inst = nullptr;
        double dt = 0;
        double time = 0;
        std::unordered_map<uint64_t, double> memo;   // (nodeId,port) → 值（每帧重置）
    };
    double EvalNode(EvalCtx& ctx, int nodeId, const std::string& port);
    void ExecNode(EvalCtx& ctx, int nodeId);
    double InputValue(EvalCtx& ctx, const GraphNode& n, const std::string& port, double fallback);
    bool InputBool(EvalCtx& ctx, const GraphNode& n, const std::string& port, bool fallback);
    const GraphLink* LinkInto(const GraphAsset& g, int nodeId, const std::string& port) const;

    std::vector<Instance> graphs_;
    std::unordered_map<std::string, double> vars_;
    DrawSink sink_;
    ScriptSink scriptSink_;               // t-graph-script：脚本成员读写/调用的注入出口
    bool lastScriptOk_ = true;            // 上次脚本节点执行是否成功（诊断/测试）
    const std::unordered_map<int, bool>* keys_ = nullptr;
    double time_ = 0;
};

} // namespace HybridEngine::App
