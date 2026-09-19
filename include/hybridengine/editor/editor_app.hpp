#pragma once
#include "hybridengine/app/engine.hpp"
#include "hybridengine/platform/window.hpp"
#include "hybridengine/platform/input.hpp"
#include "hybridengine/editor/editor_log.hpp"
#include "hybridengine/editor/uikit.hpp"
#include "hybridengine/editor/editor_text.hpp"   // t-ui-dpi：UiScale 有效值断言（离屏=1.0 恒等）
#include "hybridengine/editor/project.hpp"       // B：项目生命周期（.hproj）
#include "hybridengine/editor/script_editor_panel.hpp"   // P3：脚本编辑面板（应用内写脚本）
#include "hybridengine/editor/graph_panel.hpp"           // t-graph：节点图面板（成员需完整类型）
#include "hybridengine/core/transform.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// P2：bind C ABI 句柄前置声明（编辑器直挂项目脚本——ms_scripts_* / ms_script_fields）。
// 必须声明在**全局**作用域：ms_bind.h 里的 ms_engine/ms_scene 就是全局类型，
// 放进 namespace 会得到另一个类型（实测编译期 cannot convert）。
struct ms_engine;
struct ms_scene;

namespace HybridEngine::Editor {
class HierarchyPanel;
class InspectorPanel;
class ProjectPanel;
class ScenePanel;

// t6：工具模式（编辑器顶层状态——W/E/R 键绑定于 EditorApp；t7 菜单/快捷键消费）
enum class ToolMode { Move, Rotate, Scale };

// UX-shell（t7）：菜单动作枚举——菜单项/快捷键共用同一动作表（单一分派入口 RunMenuAction）
enum class MenuAction {
    None = 0,
    SaveScene,          // Ctrl+S
    NewScene,           // P1：清空场景
    Exit,               // 关闭窗口（无窗口=日志）
    Undo,               // Ctrl+Z——t8 钩子（OnUndoRedo；未接线=占位日志）
    Redo,               // Ctrl+Y——t8 钩子
    DeleteSelected,     // Delete
    DuplicateSelected,  // Ctrl+D
    AlignToGrid,        // Shift+G——选中对象位置吸附到网格
    MirrorSelectedX,    // Ctrl+Shift+X——复制并沿 X 镜像
    MirrorSelectedY,    // Ctrl+Shift+Y——复制并沿 Y 镜像
    MirrorSelectedZ,    // Ctrl+Shift+Z——复制并沿 Z 镜像
    ToggleBounds,       // Shift+B——显示/隐藏所有对象包围盒线框
    NewEmptyObject,     // Ctrl+Shift+N——映射 AddNewGo
    NewCamera,          // GO+CameraComponent（引擎现有工厂）
    NewCube,            // GO+MeshVisual（MeshEnabled=true）
    NewLight,           // 占位（光组件 P1）——创建空 GO+提示
    OpenAddComponent,   // 组件菜单（搜索列表启动）
    BuildScripts,       // P2：编译并载入项目内 .cs（外部 dotnet build + hostfxr 承载）
    ShowScriptEditor,   // P3：切到脚本编辑页（应用内写脚本；F9）
    ToggleHierarchy, ToggleScene, ToggleInspector, ToggleConsole, ToggleProject,   // Window 六区
    ResetTransform,     // Hierarchy 右键：重置变换（位置/旋转/缩放=默认）
    ToggleCollapse,     // Hierarchy 右键：展开/折叠（子树可见性）
    BeginRename,        // Hierarchy 右键：重命名（F2 内联编辑）
    ProjectRefresh,     // Project 右键：列表重扫
    ProjectDelete,      // Project 右键：删除资产（Editor 侧 fs::remove 降级——t8 前置 Core 增量后替换）
    ProjectOpenDir,     // Project 右键：打开目录（2026-09-17 实现——见 OpenProjectDir）
    ProjectRunPython,   // P3+：Project 右键——**用外部 Python 运行**（ctypes 绑定在 python/hybridengine；
                        //   编辑器进程内不嵌解释器——零第三方红线，所以走外部 python.exe）
    ProjectRunInExplorer,   // P3+：在资源管理器中显示（走系统 shell）
    ProjectNewPythonScript,   // C：Project 右键——新建 Python 脚本（PyComponentBase 模板）
    ProjectNewCsScript,       // C：Project 右键——新建 C# 脚本（ComponentBase 模板）
    SaveProject,        // B：文件菜单——保存项目（.hproj：lastScene/最近打开时间）
    About,              // 帮助-关于（弹层）
    Shortcuts,          // 帮助-快捷键表（弹层）
    Plugins             // 帮助-插件管理（弹层：已注册插件列表/版本/能力/装配）
};

// M3.1-3.3：EditorApp（六区壳——独立 exe 入口/离屏测试两态；Play 快照隔离）
struct EditorOptions {
    std::string projectRoot;
    std::string projectFile;    // B：.hproj 路径（项目生命周期——ctor 读 root/lastScene/EditorConfig）
    std::string title = "HybridEngine Editor";
    int width = 1280, height = 800;
    bool createWindow = true;   // false=离屏（测试）
};

struct Selection {
    bool isAsset = false;
    long goId = 0;              // 选中游戏对象（0=无）
    std::string assetPath;      // 选中资产
};

// M3.5/t129：拖放载荷（Editor 内部——不触 Core；设计 §2）
enum class DragKind { SceneObject, Asset, Field };
struct DragPayload {
    DragKind Kind = DragKind::SceneObject;
    long GoId = 0;                 // SceneObject 拖拽
    std::string AssetPath;         // Asset 拖拽
    std::string FieldType;         // Field 拖拽
};

// ===== t8：撤销/重做命令栈（审计 §10.3——闭包栈；64 深；编辑动作统一经 ApplyCommand 收口） =====
struct UndoStep {
    std::string name;                  // 描述（菜单/Console 显示）
    std::function<void()> redo;        // 执行/重做（闭包捕获前后值——不做增量 diff）
    std::function<void()> undo;
};
class UndoStack {
public:
    static constexpr size_t kLimit = 64;
    void Push(UndoStep s) {            // 先 s.redo() 再入栈；清 redo_（新编辑使重做失效）
        s.redo();
        redo_.clear();
        if (undo_.size() >= kLimit) undo_.erase(undo_.begin());   // 64 深：超限擦除最旧
        undo_.push_back(std::move(s));
    }
    bool Undo() {
        if (undo_.empty()) return false;
        UndoStep s = std::move(undo_.back());
        undo_.pop_back();
        s.undo();
        redo_.push_back(std::move(s));
        return true;
    }
    bool Redo() {
        if (redo_.empty()) return false;
        UndoStep s = std::move(redo_.back());
        redo_.pop_back();
        s.redo();
        undo_.push_back(std::move(s));
        return true;
    }
    void Clear() { undo_.clear(); redo_.clear(); }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    size_t Depth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }
    const std::string& TopName() const {
        static const std::string kNil;
        return undo_.empty() ? kNil : undo_.back().name;
    }
private:
    std::vector<UndoStep> undo_;
    std::vector<UndoStep> redo_;
};

class EditorApp {
public:
    explicit EditorApp(const EditorOptions& opts = {});
    ~EditorApp();

    int Run();
    void Tick(double dt);
    void RenderFrame(HybridEngine::Platform::IRenderer& r);   // 六区（离屏/窗口共用）

    HybridEngine::App::Engine& Engine() { return *engine_; }
    Core::Scene& EditorScene() { return engine_->Scene(); }
    Selection& Sel() { return sel_; }
    // 按 InstanceId 联动选择（sel_/Inspector/Hierarchy）——与已公开的 ScenePick 同族语义，
    // 供测试/宿主直驱（P2 编辑器脚本用例：先选中再 Add Component）
    void SelectGo(long id);
    EditorLog::Level ConsoleFilter() const { return consoleFilter_; }
    void SetConsoleFilter(EditorLog::Level l) { consoleFilter_ = l; }
    void LogInfo(const std::string& t) { EditorLog::Write(EditorLog::Level::Info, t); }
    void LogWarn(const std::string& t) { EditorLog::Write(EditorLog::Level::Warn, t); }
    void LogError(const std::string& t) { EditorLog::Write(EditorLog::Level::Error, t); }
    void Log(const std::string& t) { LogInfo(t); }

    // Play 状态机（M3.3：快照隔离——Play 序列化→运行副本；Stop 快照还原）
    void Play();
    void Pause();
    void Resume();
    void Stop();
    bool IsPlaying() const { return playing_; }
    bool IsPaused() const { return paused_; }
    const std::string& SnapshotJson() const { return snapshotJson_; }

    // 工具栏动作
    void SaveScene();
    // t-game：把「保存目标」定到指定资产路径（CLI --save-scene 用；避免依赖"当前场景名"推断）
    void SetSceneAssetPathForSave(const std::string& assetPath) { sceneSavePath_ = assetPath; }
    void SavePrefab();   // 首个层级根→.msprefab（模板 JSON——重解析实例化）
    HybridEngine::Core::SceneObject* EditorInstantiatePrefab(const std::string& assetPath, const Core::Vec3& at = Core::Vec3{});   // M2 InstantiatePrefab 语义→编辑场景（t6：落点带位置）
    void AddNewGo();
    void OpenAsset(const std::string& path);   // Project 双击
    // t1：启动默认场景（不改 ctor——测试计数语义不动；tools 入口 --empty 跳过）
    void CreateDefaultScene();
    // ===== B：项目生命周期（.hproj——主菜单/编辑器共用；ABI 红线：仅新增，不触 ms_*） =====
    bool SaveProject();                          // 写 .hproj（lastScene=当前场景名/最近打开时间——公开 API+测试断言）
    bool SaveProjectTo(const std::string& hprojPath);   // 指定路径（无项目期测试/迁移用）
    void StartProjectSession();                  // 会话启动：lastScene 非空→LoadScene 替换引擎场景；空/失败→默认 3D 场景
    bool OnWindowClose();                        // 窗口关闭钩子（OnClose 回调=自动保存项目——测试直驱同链）
    const Project::ProjectInfo& ProjectInfo() const { return projectInfo_; }
    const std::string& CurrentSceneName() const { return sceneName_; }
    std::string ProjectFile() const { return projectFile_; }
    // ===== C：Project 脚本编写（模板写入+刷新+选中——右键菜单直驱同链） =====
    bool NewScriptAsset(bool python);            // true=Python（.py） false=C#（.cs）
    // ===== P2：项目内 .cs 直挂（外部 dotnet build + 进程内 hostfxr 承载）=====
    // 编译并载入项目脚本（写 <root>/Library/ScriptAssemblies/*.csproj → dotnet build →
    // hostfxr 载入 → 注册脚本桥/场景重放回调 → 枚举可挂类型）。
    // managedDir 空 = 自动解析（见 Dotnet::DefaultManagedDir）。
    // 无 dotnet 运行时＝优雅降级：返回 false + 日志说明，既有功能不受影响。
    bool LoadProjectScripts(const std::string& projectRoot, const std::string& managedDir = "");
    // 重新编译并载入「当前项目」（编辑器已打开的项目根）——组件菜单项/Project 刷新共用
    bool RebuildProjectScripts();
    // 同上的公开别名（CLI `--load-scripts`/自动化用——脚本编译要起一次 dotnet build，不适合做默认行为）
    bool RebuildProjectScriptsPublic() { return RebuildProjectScripts(); }
    /// 项目脚本承载状态（含可挂类型清单——确定性排序）
    struct ScriptHostInfo {
        bool dotnetAvailable = false;        // hostfxr + dotnet CLI 就绪
        bool loaded = false;                 // 项目脚本已编译并载入
        std::string managedDir;              // 实际使用的托管层目录
        std::string info;                    // 人类可读诊断
        std::vector<std::string> types;      // 可挂的项目脚本类型全名
    };
    const ScriptHostInfo& ScriptHost() const { return scriptHost_; }
    const std::vector<std::string>& ProjectScriptTypes() const { return scriptHost_.types; }
    bool IsProjectScriptType(const std::string& typeName) const;
    //   ↑ 组件菜单/AddComponentToSelected 用：项目脚本类型走 ms_scripts_add_component（托管实例），
    //     不走进程内反射工厂（脚本类型在 C++ 侧没有工厂）
    // 引擎句柄 / 场景句柄（P2：测试与宿主直驱 ms_* —— 前置声明避免头文件耦合 bind）
    ms_engine* EditorEngineHandle();
    ms_scene* EditorScenePtr();
    // ===== P3：应用内写脚本（中心区「脚本」文档页）=====
    bool OpenScriptInEditor(const std::string& assetPath);   // 解析项目内路径→打开面板并切到脚本页
    void ShowScriptEditor(bool on);                          // 中心区页切换（场景 ⇄ 脚本）
    bool ScriptEditorActive() const { return centerScript_; }
    // t-game：游戏视图（把引擎帧缓冲贴到中心区——脚本绘制的画面在这里可见）
    void ShowGameView(bool on);
    void ShowGraphView(bool on);       // t-graph：切到节点图页（并装载该图到运行时——改完立即生效）
    bool GraphViewActive() const { return centerGraph_; }
    // t-clip：把按键转发给脚本面板（与窗口路径同一条 OnKey——测试/自动化用）
    bool ScriptEditorKey(int vk, bool ctrl, bool shift) { return scriptEditor_ ? scriptEditor_->OnKey(vk, ctrl, shift) : false; }
    void LoadProjectGraphs();                                  // 装载工程下所有 .hgraph（幂等）
    void RefreshScriptMembers();                               // t-graph-member：刷新脚本成员清单（调色板下拉）
    void OpenGraphAsset(const std::string& assetPath);         // t-graph-pick：切到指定图（装载+出口+成员）
    bool SaveGraphAsset();                                     // 把运行时那份图写回 .hgraph
    HybridEngine::Editor::GraphPanel* GraphPanelPtr() const { return graphPanel_.get(); }
    const std::string& GraphAssetPath() const { return graphAssetPath_; }
    bool GameViewActive() const { return centerGame_; }
    void DrawGameView(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc);
    // perf：帧内分段耗时（EMA，毫秒）——「为什么这么卡」要能拿数字说话，而不是猜
    struct FramePerf {
        double engineRenderMs = 0;   // 引擎回放脚本绘制命令（parity 面 1280x800）
        double gameScaleMs = 0;      // 游戏视图：帧缓冲 → 中心区尺寸的缩放
        double gameBlitMs = 0;       // 游戏视图：缩放结果贴到编辑器面
        double uiDrawMs = 0;         // 编辑器界面绘制（五面板+中心区+状态栏，全软渲）
        double uiCenterMs = 0;       // 界面里：中心区（场景视图/游戏视图）
        double uiPanelsMs = 0;       // 界面里：四侧面板（层级/项目/检查器/控制台）
        double uiBarsMs = 0;         // 界面里：菜单/工具栏/状态栏/浮层
        double presentMs = 0;        // 呈现（编辑器面 → 窗口，2304x1344）
        double Total() const { return engineRenderMs + gameScaleMs + gameBlitMs + uiDrawMs + presentMs; }
    };
    FramePerf Perf() const { return perf_; }
    // perf：引擎当前排入的绘制命令数（回归用——host 没清就会随时间无限增长）
    size_t EditorEngineOpCount() const;
    // perf：立即回放当前命令队列并返回耗时（毫秒）——图元成本剖析/回归用。
    // 注意语义：**不清队列**（调用方先自行清空），否则没法"排好一批命令再单独量回放"。
    double ReplayEngineNow();
    // 缩放实现可切换（用于实测对比：0=逐像素最近邻 1=列查找表）
    void SetGameScaleMode(int m) { gameScaleMode_ = m; }
    int GameScaleMode() const { return gameScaleMode_; }
    HybridEngine::Editor::ScriptEditorPanel* ScriptEditor() const { return scriptEditor_.get(); }
    void HandleChar(uint32_t cp);                            // WM_CHAR 路由（中文/IME 唯一入口）
    // 键盘注入（与窗口 OnKey **同一条链**——含脚本编辑页优先路由；测试/宿主直驱用。
    // 注：其余 Handle* 均私有，测试走 ClickToolbarButton/ApplyShortcut 这类公开包装——此处照此约定。）
    // t-text：**忠实模拟一次物理按键**——真窗口里 WM_KEYDOWN 之后 TranslateMessage 会再投一条 WM_CHAR，
    // 所以这里对可打印 ASCII 也补投一次字符。否则只用 InjectKey 的宿主/测试会完全打不出字。
    // 两条边界（与 Win32 一致）：
    //   · 按住 Ctrl/Alt 时**不产生字符**（真窗口给的是控制字符 0x01-0x1A，文本面一律过滤）
    //     —— 否则 Ctrl+S 会顺手往文档里插一个 "S"（实测踩坑）。
    //   · 真窗口路径不受影响：它直接调 HandleKey(WM_KEYDOWN)/HandleChar(WM_CHAR)，不会重复。
    void InjectKey(int vk, bool down) {
        HandleKey(vk, down);
        if (down && !ctrlDown_ && vk >= 0x20 && vk < 0x7F) HandleChar((uint32_t)vk);
    }
    // 字符注入（与窗口 OnChar 同链）
    void InjectChar(uint32_t cp) { HandleChar(cp); }
    // 滚轮注入（与窗口 OnWheel 同链；脚本页激活时按 Shift=水平滚动）
    void InjectWheel(double x, double y, int delta) { HandleWheel(x, y, delta); }
    // 左键注入（与窗口 OnMouse 同链；游戏视图内容区=转发引擎并消费，不落到隐藏 Scene 区）
    void InjectMouse(double x, double y, bool down) { HandleMouse(x, y, down, false); }
    // 左键双击注入（与窗口 OnMouseDoubleClick 同链——Project 面板双击打开 .cs 走这条）
    void InjectDoubleClick(double x, double y) { HandleMouse(x, y, true, true); }
    // Play 态只读访问（测试/宿主断言；状态机本体仍私有）
    bool Playing() const { return playing_; }
    bool Paused() const { return paused_; }
    // ===== t1：场景视图工具栏 chips（点击路由——HandleMouse 与测试同链） =====
    bool ClickViewChip(double x, double y);    // chips 命中=切换（2D/3D/正交/吸附/聚焦）；未命中=false
    // M3.5/t129：EditorConfig（折叠态持久化 IN-2）
    void SaveEditorConfig();
    void LoadEditorConfig();
    const std::map<std::string, bool>& FoldState() const { return foldState_; }
    bool SimulateDropAsset(const std::string& assetPath, double x, double y);   // 测试：Asset 拖放（DR 语义）
    HybridEngine::Core::SceneObject* SimulateDropToScene(const std::string& assetPath, double x, double y);   // t6：DR-2 测试/联动钩子（HandleDrop 同链——落点投射）
    // M3.5/t131：AI 徽章切换（B 档开关——持久化）
    void ToggleAiBadge();
    bool AiLocalOn() const;

    // —— t6：ScenePanel 直接操纵 ——
    void SetToolMode(ToolMode m);                    // W/E/R（镜像到 ScenePanel gizmo 态）
    ToolMode GetToolMode() const { return toolMode_; }   // 注：命名 GetToolMode——与类型名 ToolMode 避免同名遮蔽（t7 消费）
    bool ApplyTransformEdit(long goId, const std::string& field, const std::string& json);   // 事务点：CommitField 内核；t8 撤销栈唯一挂点
    long ScenePick(double x, double y);              // 视口单击选择（联动 Hierarchy/Inspector）；空白=取消选择（0）
    bool SceneDrag(double x0, double y0, double x1, double y1, bool clone = false);   // 测试/运行时：一次完整拾取/拖动手势；clone=Ctrl 克隆拖拽；返回是否实改
    void SetSnapEnabled(bool on);   // t1：同步 ScenePanel chip 状态（定义于 .cpp——需完整 ScenePanel）
    bool SnapEnabled() const { return snapEnabled_; }
    bool SnapActive() const { return snapEnabled_ || ctrlDown_; }   // Ctrl=临时强制开吸附（仅默认关时生效）
    bool SceneDragActive() const { return sceneDrag_ != SceneDragKind::None; }

    // ===== UX-shell（t7）：菜单/快捷键公开语义（测试直驱；UI 同路径） =====
    // 菜单（下拉弹层）
    bool OpenMenu(int index);                 // 打开顶部菜单（-1=关闭）；返回是否切换
    bool MenuOpen() const { return menuOpen_ >= 0; }
    int OpenMenuIndex() const { return menuOpen_; }
    int MenuCount() const;                    // 顶部菜单数（文件/编辑/游戏对象/组件/窗口/帮助）
    std::string MenuTitle(int index) const;
    int MenuItemCount(int index) const;
    std::string MenuItemLabel(int index, int item) const;
    bool RunMenuAction(MenuAction a);         // 单一动作分派（菜单点击/快捷键共用）
    bool ClickMenuItem(int index, int item);  // 模拟点击菜单项（=RunMenuAction+关闭）
    // 组件菜单（搜索列表）
    bool OpenComponentMenu();                 // 打开组件下拉（搜索框+列表）
    bool ComponentMenuOpen() const { return componentMenuOpen_; }
    std::vector<std::string> ComponentCatalog() const;   // 可用组件类型（ReflectionRegistry::Create 可得）
    void FilterComponentMenu(const std::string& q);      // 搜索过滤（重建 filtered 列表）
    int ComponentMenuCount() const;
    std::string ComponentMenuAt(int i) const;
    bool CommitComponentMenu(int i);          // 列表项选定→AddComponentToSelected
    bool AddComponentToSelected(const std::string& typeName);   // add_component 语义→Inspector 刷新
    // P2：项目脚本组件挂载（经命令栈：undo=移除；redo=托管实例化）——AddComponentToSelected 内部路由
    bool AddProjectScriptComponent(HybridEngine::Core::SceneObject* go, const std::string& typeName);
    bool RemoveComponentFromSelected(const std::string& typeName);   // t1：组头「移除」→命令栈（Play 禁用）
    // 快捷键（HandleKey 同路径；测试直驱）
    bool ApplyShortcut(int vk, bool ctrl, bool shift);
    // 撤销/重做钩子（t8 接线；未接线=占位日志）
    std::function<void(bool isUndo)> OnUndoRedo;

    // ===== t8：撤销/重做命令栈（审计 §10.3——Ctrl+Z/Y 经 ApplyShortcut→RunMenuAction→OnUndoRedo 链） =====
    bool Undo();                       // Play 中=无操作+提示（P1-P4 冻结）
    bool Redo();
    bool CanUndo() const { return undoStack_.CanUndo(); }
    bool CanRedo() const { return undoStack_.CanRedo(); }
    size_t UndoDepth() const { return undoStack_.Depth(); }
    size_t RedoDepth() const { return undoStack_.RedoDepth(); }
    const std::string& UndoTopName() const { return undoStack_.TopName(); }   // 测试：栈顶描述
    UndoStack& UndoLog() { return undoStack_; }                                // 测试：栈直驱（Push≤64 深断言）
    bool ApplyCommand(const std::string& name, std::function<void()> redo, std::function<void()> undo);
    //   ↑ 编辑动作唯一收口（a.k.a. ApplyCommand(cmd)）：redo() 执行→Push 记录→清 redo；Play 中=拒绝

    // 层级重父子（拖放/菜单共用语义）
    bool ReparentSceneObject(long goId, long targetGoId, bool keepWorld);   // 行→行=child
    bool ReparentSceneObjectToRoot(long goId, bool keepWorld);              // 面板空白=root
    // 对象级语义（Delete/Ctrl+D/重命名）
    bool DeleteSelected();
    bool DuplicateSelected();
    bool RenameSelected(const std::string& newName);
    // 资产（Project 右键——Editor 侧 fs::remove 降级；t8 前置 Core 增量后替换）
    bool DeleteAsset(const std::string& assetPath);
    // 窗口显示切换（Window 菜单——默认全部可见）
    bool PanelVisible(int i) const { return i >= 0 && i < 5 && panelVisible_[i]; }
    void SetPanelVisible(int i, bool v);
    // 撤销/重做后 UI 一致性（Hierarchy/Inspector 行刷新——审计 §10.3）
    void RefreshAfterUndoRedo();
    // 弹层状态（帮助）
    bool HelpPopupOpen() const { return helpPopupOpen_; }
    std::string HelpPopupText() const { return helpPopupText_; }
    void CloseHelpPopup() { helpPopupOpen_ = false; pluginPopup_ = false; pluginDllFocus_ = false; }
    // ===== t4：插件管理弹层（一键装配/计数 HUD/Dll 加载——测试直驱同链） =====
    bool PluginPopupOpen() const { return helpPopupOpen_ && pluginPopup_; }
    int PluginPopupCount() const;                      // 已注册插件数（计数 HUD）
    int PluginButtonCount() const;                     // 最近 Draw 的「一键装配」按钮数
    std::string PluginButtonLabel(int idx) const;      // "plugin|preset"（测试选择）
    bool ClickPluginPresetButton(int idx);             // 单击预设按钮→装配（HandleMenuMouse 同链）
    bool ClickPluginDllInput();                        // 单击 Dll 输入框=聚焦
    bool ClickPluginLoadDllButton();                   // 单击「加载」→LoadDll（同链）
    void SetPluginDllInput(const std::string& s) { pluginDllInput_ = s; }
    const std::string& PluginDllInput() const { return pluginDllInput_; }
    // 上下文菜单（Hierarchy/Project/场景 三分支）——状态读出（测试断言）
    int ContextMenuItems() const { return (int)ctxItems_.size(); }
    std::string ContextMenuLabel(int i) const;
    bool RunContextMenuAction(int i);         // 右键菜单动作（同 UI 路径）
    bool OpenContextMenuAt(double x, double y);   // 测试/外部直驱：在 (x,y) 弹右键菜单（复用 UI 路径）

    struct LayoutRects {
        UiKit::Rect menubar, toolbar, hierarchy, scene, inspector, console, project, statusbar;   // ergo：statusbar=底部 24px 保留区
    };
    LayoutRects Layout(int w, int h) const;

    // ===== ergo：编辑器人体工学（缩放 Gizmo/轴切换/2D 缩放平移/Ctrl 克隆/Ctrl+N/状态栏/工具提示） =====
    // —— L 键/视图 chips：Gizmo 局部/全局轴切换（W/E/R 一致；local=对象旋转基投影）——
    void ToggleLocalAxes();
    bool GizmoLocal() const;   // 定义于 .cpp（需完整 ScenePanel）
    // —— Ctrl+N 新建场景（菜单/快捷键共用：无修改=清空+清撤销栈；有修改=提示"将丢弃未保存更改？"并跳过）——
    bool NewScene();
    // P3+：Python 资产用**外部** python.exe 运行（编辑器不嵌解释器；走仓库 ctypes 绑定）
    bool RunPythonAsset();                                   // 用当前右键选中的资产
    bool RunPythonAsset(const std::string& assetPath);        // 显式指定资产（菜单/工具栏/测试共用）
    // P3+：在文件管理器中定位资产（Windows：explorer /select；非 Windows：打开所在目录）
    bool RevealInExplorer(const std::string& assetPath);
    // Project 右键「打开目录(Open Folder)」：在系统文件管理器里打开**项目根目录**。
    // 2026-09-17 由 P1 占位改为实现——平台能力早已具备（`Platform::ShellOpen` / `ShellReveal`），
    //   原先只打印「未实现（P1——系统 shell 调用）」，属"能力有了但没人接上"。
    // 返回 false 的情形：未打开项目、或系统没有可用的打开器（此时日志会带原因）。
    bool OpenProjectDir();
    bool AlignSelectedToGrid();
    bool MirrorSelectedX();
    bool MirrorSelectedY();
    bool MirrorSelectedZ();
    bool ToggleBounds();
    // —— Ctrl 克隆拖拽（clone=true 时拖动=克隆副本拖动（深拷贝子树+组件+原位置）；拖完=一次 CreateCommand）——
    HybridEngine::Core::SceneObject* CloneSceneObject(long id);   // 深拷贝子树（场景容器改写——DuplicateSelected 同法）；返回新实例（选中）
    // —— 状态栏（六区之外底部 24px——Layout 保留区；StatusBar 文本=绘制与测试同源）——
    // ui-v3：左=选中对象名+世界坐标；中=工具/吸附；右=版本+FPS
    struct StatusBarInfo { std::string left; std::string mid; std::string right; bool playing = false; };
    StatusBarInfo StatusBar();
    // —— 工具提示（hover 1.2s=72 帧→悬浮文字（白字黑底，鼠标右上方）；离开=30 帧自清）——
    bool TooltipVisible() const { return tooltipShown_; }
    const std::string& TooltipText() const { return tooltipText_; }
    void TooltipFrame(double x, double y);   // 一帧 hover 评估（RenderFrame 同链；测试直驱）
    void SetTooltipMouse(double x, double y) { tooltipMouseX_ = x; tooltipMouseY_ = y; }

    HybridEngine::Platform::IWindow* Window() const { return window_.get(); }
    HybridEngine::Platform::IRenderer* Renderer() const { return renderer_.get(); }
    // t-ui-dpi：DPI 缩放因子（GetDpiForWindow/96——离屏/无窗口=1.0 → 测试像素恒等；真窗口=物理像素原生清晰）
    double DpiScale() const { return dpiScale_; }
    double UiScaleValue() const { return UiScale(); }   // 有效 UI 缩放（=clamp(windowScale_*dpiScale_,1,3)）
    // M3.5/t129：测试/联动访问器（面板）
    HybridEngine::Editor::InspectorPanel* Inspector() const { return inspector_.get(); }
    HybridEngine::Editor::HierarchyPanel* Hierarchy() const { return hierarchy_.get(); }
    HybridEngine::Editor::ProjectPanel* Project() const { return project_.get(); }
    HybridEngine::Editor::ScenePanel* ActiveScenePanel() const { return scenePanel_.get(); }   // t136（SC-1/2 测试）

    // ===== t-ui：控制台折叠条（默认=底部细条 22px——最新 1 行日志+右侧 ▲/▼ toggle；状态栏保留在底） =====
    bool ConsoleCollapsed() const { return consoleCollapsed_; }
    void SetConsoleCollapsed(bool c);                 // 切换（持久化 EditorConfig: consoleCollapsed）
    void ToggleConsoleCollapsed() { SetConsoleCollapsed(!consoleCollapsed_); }
    bool ClickConsoleToggle(double x, double y);      // 命中 toggle=切换（HandleMouse 同链）；未命中=false
    const UiKit::Rect& ConsoleToggleRect() const { return consoleToggleRc_; }   // 最近 RenderFrame 命中区
    // ===== ui-v3：控制台过滤 pill（信息/警告/错误——选中=对应色下划线+浅底；命中=SetConsoleFilter 既有语义） =====
    const UiKit::Rect& ConsolePillRect(int idx) const {   // 最近 RenderFrame（收起/隐藏=空）
        static const UiKit::Rect kNil{0, 0, 0, 0};
        return idx >= 0 && idx < 3 ? consolePillRc_[idx] : kNil;
    }
    bool ClickConsoleFilterPill(double x, double y);  // pill 命中=SetConsoleFilter（HandleMouse 同链）；未命中=false
    const UiKit::Rect& ConsoleClearRect() const { return consoleClearRc_; }     // 「清空」按钮命中区
    bool ClickConsoleClear(double x, double y);       // 命中=EditorLog::Clear（Clear 命令）；未命中=false

    // ===== t-ui：左右可拖分隔条（0..1 比例；Layout 按比例计算；拖=钳制 0.12..0.38；持久化 splitLeft/splitRight） =====
    double SplitLeft() const { return splitL_; }
    double SplitRight() const { return splitR_; }
    void SetSplit(double left, double right);         // 钳制 0.12..0.38（拖动与持久化统一入口）
    bool BeginSplitDrag(double x, double y);          // 命中分隔带=开始拖（HandleMouse 同链）；未命中=false
    bool UpdateSplitDrag(double x, double y);         // 拖动中更新比例；返回=拖动中
    void EndSplitDrag();                              // 抬起=结束（比例持久化）
    bool SplitDragging() const { return splitDrag_ != 0; }

    // ===== t-ui：工具栏图标按钮（动作绑定不变——图标化仅改绘制+命中区；公开=测试命中语义同链） =====
    enum class ToolbarBtn { Play = 0, Stop, Pause, Save, New, Snap, Move, Rotate, Scale, Count };
    const UiKit::Rect& ToolbarButtonRect(int idx) const;   // 最近 RenderFrame 命中区（Count 越界=空矩形）
    bool ClickToolbarButton(int idx);                      // 中心点按→动作（HandleToolbarClick 同链）；越界/未命中=false

private:
    void HandleKey(int key, bool down);
    void HandleMouse(double x, double y, bool down, bool doubleClick);
    void HandleRightClick(double x, double y);
    void HandleButton(double x, double y, int button, bool down);   // t136：中键 orbit/右键 pan/右键点击=菜单
    void HandleWheel(double x, double y, int delta);              // t136：滚轮 dolly
    void HandleDrop(double x, double y);   // M3.5/t129：拖放释放（Asset→Hierarchy/Inspector AssetRef）
    bool HandleToolbarClick(double x, double y);   // t-ui：工具栏命中（HandleMouse 与 ClickToolbarButton 同链）
    void RefreshDpi();                         // t-ui-dpi：dpiScale_=GetDpiForWindow/96（离屏=1.0）+ 字体点径跟随（跨屏随动）
    void DrawContextMenu(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s);
    bool HoverContextMenu(double x, double y) const;
    UiKit::Rect ContextMenuRect() const;
    bool MirrorSelectedAxis(int axis);   // 0=X 1=Y 2=Z（复制+镜像）
    void CommitField(const std::string& type, const std::string& field, const std::string& json);
    // t2：材质资产字段提交（Inspector 资产页 Enter——.hmat 编辑→原子保存；CommitField Asset 分支）
    bool CommitMaterialField(const std::string& assetPath, const std::string& field, const std::string& json);
    // t8：Transform 纯写内核（无撤销记录——ApplyTransformEdit 的 redo/undo 闭包调用）
    bool ApplyTransformEditCore(long goId, const std::string& field, const std::string& json);
    // ===== UX-shell（t7）：菜单/弹层绘制与事件 =====
    void DrawMenuBar(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s);   // 顶栏+下拉
    void DrawComponentMenu(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s);
    void DrawHelpPopup(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s);
    bool HandleMenuMouse(double x, double y, bool down);   // 返回 true=已消费
    bool HandlePopupKeys(int key, bool down);              // 菜单/弹层按键（返回 true=已消费）
    // ===== t4：插件管理弹层内核（Draw/点击/测试同链） =====
    UiKit::Rect PluginPopupRect() const;                   // 弹层几何（draw=hit 同式）
    bool HandlePluginPopupClick(double x, double y);       // 弹层内点击路由（按钮/输入框——返回 true=已消费）
    bool AssemblePluginPreset(const std::string& plugin, const std::string& preset);   // 一键装配（撤销域——命令栈）
    void LoadPluginDll();                                  // Dll 加载（输入框路径→Plugin::LoadDll）
    void RefreshPluginPopup();                             // 重绘数据重建（打开/装配/加载后）
    void CloseAllMenus();                                  // 菜单/上下文/组件菜单全关
    void RebuildContextMenu(double x, double y);           // 三分支（Scene/Hierarchy/Project）
    // t7：场景容器改写（删除/复制共用——序列化→JSON 改写→重 FNV→反序列化→SetScene）
    std::string RewriteScene(const std::string& containerIn, const std::string& payloadOut) const;   // 空串=失败
    std::vector<HybridEngine::Core::SceneObject*> DfsOrder();   // 与序列化器同序（删除/复制索引）
    int DfsIndexOf(long id) const;                            // -1=未命中
    int DfsSubtreeSize(int idx) const;                        // idx 子树条目数（删除/复制连续段）
    bool IsDescendantOf(long maybeParent, long id) const;     // 子环守卫（t9：SetParent 无环检——编辑器侧）
    void RefreshAfterSceneReplace();                          // 场景整体替换后 UI 一致性（Hierarchy/Inspector/选择）
    // —— t6：Scene 区直接操纵（HandleMouse 路由 / ScenePick-SceneDrag 共用）——
    void HandleSceneMouse(double x, double y);                     // 场景区 LMB 按下（gizmo/拾取→选中/起拖）
    void SceneMouseMove(double x, double y);                       // 拖动中（LMB held）
    void SceneMouseUp(double x, double y);                         // 拖动结束（t8 收口点预留）
    void ApplySceneMoveToGo(long goId, const Core::Vec3& worldPos);      // 世界目标→局部→ApplyTransformEdit
    void ApplySceneRotateToGo(long goId, const Core::Vec3& worldAxis, double angle);
    void ApplySceneScaleToGo(long goId, const Core::Vec3& worldScale);   // ergo：R 缩放 Gizmo 落点（局部缩放直写）
    LayoutRects CurrentLayout() const;                             // renderer 优先；离屏=lastW/H
    // t-game 人类体验：按游戏视图**内容区**（页签下方）做 letterbox 逆映射。
    // 返回 true = 点在内容区内；gx/gy 始终为设计面坐标（拖动出界时仍可继续转发 move/up）。
    bool GameViewPointToEngine(double x, double y, double& gx, double& gy) const;
    std::unique_ptr<HybridEngine::App::Engine> engine_;
    std::unique_ptr<HybridEngine::Platform::IWindow> window_;
    std::unique_ptr<HybridEngine::Platform::IRenderer> renderer_;
    std::unique_ptr<HybridEngine::Platform::Input> input_;
    EditorOptions opts_;
    Selection sel_;
    EditorLog::Level consoleFilter_ = EditorLog::Level::Info;
    bool playing_ = false;
    bool paused_ = false;
    bool frameInput_ = false;   // t5：Run 帧节流——本帧窗口回调是否到来（Handle* 置位；Run 帧首复位）
    std::string snapshotJson_;   // M3.3 快照（容器文本）
    // P2：项目脚本承载状态（编译/载入/可挂类型——见 ScriptHostInfo）
    ScriptHostInfo scriptHost_;
    std::string scriptProjectRoot_;   // 已载入脚本的项目根（Rebuild 用）
    // P3：应用内写脚本
    std::unique_ptr<HybridEngine::Editor::ScriptEditorPanel> scriptEditor_;
    bool centerScript_ = false;        // 中心区页：false=场景视图 true=脚本编辑
    bool centerGame_ = false;          // t-game：中心区页=游戏视图（引擎帧缓冲呈现）
    bool centerGraph_ = false;         // t-graph：中心区页=节点图（连线式编程）
    std::vector<uint32_t> blitBuf_;    // t-game：游戏视图缩放缓冲（避免每帧分配）
    FramePerf perf_{};                 // perf：分段耗时（EMA）
    int gameScaleMode_ = 1;            // 0=逐像素除法（旧）1=列查找表（默认）
    std::vector<int> scaleCols_;       // 缩放列查找表（目标列 → 源列；尺寸变化时重建）
    // t-perf：缩放的"同源列连续区段"表（每段：目标列区间 + 源列）——内层变成顺序读+顺序写，
    //   避免逐像素 gather（原来每像素一次表查 + 一次跨行随机读）。像素结果与逐像素查表**完全一致**。
    struct ScaleRun { int beg, end, src; };
    std::vector<ScaleRun> scaleRuns_;
    int scaleRunsDw_ = 0, scaleRunsSw_ = 0;
    bool scriptMouseDown_ = false;     // 脚本页鼠标「本次按下」自跟踪（窗口层不区分按下/移动）
    HybridEngine::Editor::UiKit::Rect centerTabScene_{}, centerTabGame_{}, centerTabScript_{}, centerTabGraph_{};   // 页签命中区（RenderFrame 记录）
    // 面板
    std::unique_ptr<HierarchyPanel> hierarchy_;
    std::unique_ptr<InspectorPanel> inspector_;
    std::unique_ptr<ProjectPanel> project_;
    std::unique_ptr<ScenePanel> scenePanel_;
    // 工具栏按钮区（命中检测）
    UiKit::Rect btnPlay_, btnStop_, btnPause_, btnSave_, btnNew_;
    UiKit::Rect btnAiBadge_;   // M3.5/t131：AI 徽章（点击=切换 B 档）
    UiKit::Rect aiBadge_{-1, -1, -1, -1};   // 命中区（RenderFrame 更新）
    // Hierarchy 右键菜单（P2 对象创建入口——New SceneObject）
    bool ctxMenuOpen_ = false;
    int ctxItemHover_ = -1;                    // 右键菜单鼠标悬停项
    double ctxX_ = 0, ctxY_ = 0;
    // t136：3 键拖动状态（中键 orbit/右键 pan——区别与拖放 dragging_）
    int btnDrag_ = 0;
    double btnDragX_ = 0, btnDragY_ = 0;
    bool btnDragMoved_ = false;
    // M3.5/t129：拖放状态（§2 DragPipeline）
    bool dragging_ = false;
    DragPayload drag_{};
    double dragX_ = 0, dragY_ = 0;
    // M3.5/t129：折叠态持久化（IN-2——EditorConfig JSON）
    std::map<std::string, bool> foldState_;
    // —— t6 状态 ——
    ToolMode toolMode_ = ToolMode::Move;       // 工具态（W/E/R；t7 消费）
    bool snapEnabled_ = true;                  // 工具栏「吸附(Snap)」开关：全局默认开（步长 0.5——ScenePanel::Snap）
    bool ctrlDown_ = false;                    // Ctrl 按住（临时强制开吸附）
    UiKit::Rect btnSnap_{};                    // 工具栏 Snap 按钮命中区
    int lastW_ = 1280, lastH_ = 800;           // 最近渲染尺寸（离屏 ScenePick/SceneDrag）
    enum class SceneDragKind { None, Move2D, GizmoMove, GizmoRotate, GizmoScale };
    SceneDragKind sceneDrag_ = SceneDragKind::None;
    long sceneDragGoId_ = 0;
    double sceneDragX0_ = 0, sceneDragY0_ = 0;
    bool sceneDragDirty_ = false;              // 本次拖动是否产生过 ApplyTransformEdit 实改（SceneDrag 返回口）
    // t8：拖动命令收口（起点快照/字段——SceneMouseUp 一次 Push；风险 C：中间帧不入栈）
    std::string sceneDragField_;
    std::string sceneDragStartJson_;
    // ergo：Ctrl 克隆拖拽状态（clone=深拷贝已建；mouse-up=一次 CreateCommand——undo 一步还原整场景）
    bool sceneDragClone_ = false;
    std::string sceneDragCloneBefore_;   // 克隆前场景容器（undo=整体还原）
    std::string sceneDragCloneName_;     // 原对象名（命令描述）
    // ===== UX-shell（t7）：菜单状态 =====
    int menuOpen_ = -1;                        // 打开的顶部菜单索引（-1=关）
    int menuHover_ = -1;                       // 顶栏 hover（点击切换）
    int menuItemHover_ = -1;                   // 下拉项 hover
    UiKit::Rect menuDropRc_;                   // 下拉弹层矩形（命中）
    UiKit::Rect menuTitleRc_[8];               // 顶栏标题区（命中）
    // 组件菜单（搜索）
    bool componentMenuOpen_ = false;
    std::string componentFilter_;
    int componentSel_ = 0;
    std::vector<std::string> componentFiltered_;
    UiKit::Rect componentMenuRc_;
    // 上下文菜单（Scene/Hierarchy/Project 三分支——统一项表）
    struct CtxItem { std::string label; MenuAction action; bool enabled; bool checked; };
    std::vector<CtxItem> ctxItems_;
    long ctxGoId_ = 0;                         // Hierarchy 右键目标（动作作用对象）
    std::string ctxProjectPath_;               // Project 右键目标资产
    // 工具态按钮（W/E/R 工具栏高亮——t6 消费同枚举；t7 绘制）
    UiKit::Rect btnMove_, btnRotate_, btnScale_;
    // 窗口显示切换（Window 菜单）
    bool panelVisible_[5] = {true, true, true, true, true};   // 0=Hierarchy 1=Scene 2=Inspector 3=Console 4=Project
    // ===== t-ui-dpi：DPI 缩放（GetDpiForWindow/96——离屏=1.0；windowScale_=用户级缩放预留=1.0） =====
    double dpiScale_ = 0.0;    // 0=未初始化（ctor RefreshDpi 校正：离屏→1.0；真窗口→GetDpiForWindow/96）
    double windowScale_ = 1.0;   // t-ui-dpi：未来用户缩放（有效 UiScale=clamp(windowScale_*dpiScale_,1,3)）
    // ===== t-ui：控制台折叠条 / 左右分隔条 / 工具栏图标命中 =====
    bool consoleCollapsed_ = true;             // 默认=底部细条（22px——状态栏保留在底）
    UiKit::Rect consoleToggleRc_{0, 0, 0, 0};  // 折叠条 ▲/▼ 命中区（RenderFrame 更新）
    // ui-v3：过滤 pill（信息/警告/错误）与「清空」按钮命中区（RenderFrame 更新；收起/隐藏=空）
    UiKit::Rect consolePillRc_[3] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    UiKit::Rect consoleClearRc_{0, 0, 0, 0};
    // ui-v3：FPS 帧率（RenderFrame 末尾 1s 滑动窗口平滑采样——状态栏右侧显示）
    int fpsShown_ = 0;                          // 显示值（0=未采样——显示 --）
    double fpsAccum_ = 0.0;
    int fpsFrames_ = 0;
    std::chrono::steady_clock::time_point fpsLast_{};   // 需 <chrono>（Run 已用——此处引用）
    bool fpsInit_ = false;
    double splitL_ = 0.21;                     // 左侧面板宽比例（默认=既有 sideW/w——Layout 按比例计算）
    double splitR_ = 0.26;                     // 右侧检查器宽比例（默认=既有 inspW/w）
    int splitDrag_ = 0;                        // 0=无 1=左 2=右（命中分隔带开始/移动更新/抬起结束）
    // 帮助弹层
    bool helpPopupOpen_ = false;
    std::string helpPopupText_;
    // ===== t4：插件管理弹层（一键装配/计数 HUD/Dll 加载） =====
    bool pluginPopup_ = false;                          // 当前弹层=插件管理（交互式——按钮/输入）
    std::vector<UiKit::Rect> pluginBtnRects_;           // 预设按钮命中区（Draw 重建——与 pluginBtnData_ 对齐）
    std::vector<std::pair<std::string, std::string>> pluginBtnData_;   // (插件名, preset)
    UiKit::Rect pluginDllInputRc_{0, 0, 0, 0};          // Dll 输入框命中区
    UiKit::Rect pluginDllBtnRc_{0, 0, 0, 0};            // 「加载」按钮命中区
    std::string pluginDllInput_;                        // Dll 路径输入（TextEdit）
    bool pluginDllFocus_ = false;                       // 输入框聚焦（HandlePopupKeys 字符路由）
    std::string pluginPopupMsg_;                        // 最近操作结果（装配/加载——弹层内提示）
    bool shiftDown_ = false;                   // Shift 修饰（VK 自跟踪——t6 ctrlDown_ 同模式）
    // ergo：工具提示状态（hover 1.2s=72f；离开 30f 自清——绘制于 RenderFrame 尾部）
    std::string tooltipText_;
    bool tooltipShown_ = false;
    int tooltipFrames_ = 0;
    int tooltipClearFrames_ = 0;
    double tooltipMouseX_ = 0, tooltipMouseY_ = 0;
    std::string TooltipTargetAt(double x, double y) const;   // 命中矩形→提示文本（""=无）
    // t8：撤销栈（编辑态域；Play() 前 Clear——audit §10.3 生命周期）
    UndoStack undoStack_;
    // ===== B：项目生命周期状态（.hproj——主菜单/编辑器共用） =====
    Project::ProjectInfo projectInfo_;   // 最近一次读/写 .hproj 的信息（name/guid/root/createdAt/lastScene/lastOpenedAt）
    std::string projectFile_;            // .hproj 路径（""=无项目——SaveProject 无操作）
    std::string sceneName_ = "未命名";    // 当前场景名（.mscene 文件名 或 "未命名" 标记——lastScene 落点）
    std::string sceneSavePath_ ;          // t-game：保存目标覆盖（CLI --save-scene；空=Assets/EditScene.mscene）
    // t-graph：节点图面板（编辑**运行时那份图**——用户选型是运行时解释，所以改完立即生效）
    std::unique_ptr<HybridEngine::Editor::GraphPanel> graphPanel_;
    std::string graphAssetPath_;          // 当前打开的 .hgraph（空=未打开）
    bool graphMouseDown_ = false;
    bool gameMouseDown_ = false;    // t-game：Play 时鼠标转发给引擎的按钮沿跟踪
};

} // namespace HybridEngine::Editor
