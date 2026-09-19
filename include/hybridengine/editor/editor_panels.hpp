#pragma once
#include "hybridengine/editor/editor_app.hpp"
#include "hybridengine/editor/uikit.hpp"
#include "hybridengine/core/scene.hpp"
#include "hybridengine/core/assets/asset_library.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// M3.2：六区面板（自绘——无窗口控件；经 IRenderer 9 原语+位图字体）

// Hierarchy：场景层级树（行=对象；选择=单选）
class HierarchyPanel {
public:
    void SetScene(Core::Scene* s) { scene_ = s; Refresh(); }
    void Refresh();
    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc);
    bool OnClick(double x, double y);
    long SelectedId = 0;
    std::function<void(long)> OnSelect;
    double BoundsY() const { return lastRc_.y; }        // M3.5/t129：面板顶 y（拖放空白区判定）
    const UiKit::Rect& LastRc() const { return lastRc_; }
    // 行快照（测试——t141 New 路径断言；仅可见行——折叠过滤后）
    struct RowInfo { long id; std::string name; int depth; };
    std::vector<RowInfo> RowSnapshot() const;
    // ===== UX-shell（t7）：行命中/拖放/折叠/重命名 =====
    bool Contains(double x, double y) const;                    // 面板矩形命中（拖放空白=设为根判定）
    long RowAt(double x, double y) const;                       // 可见行命中→行 id（0=未命中）
    int RowIndexAt(double x, double y) const;                   // -1=未命中（可见序）
    UiKit::Rect RowRect(int visibleIdx) const;                  // 行布局矩形（拖放高亮/菜单锚点）
    int VisibleCount() const { return (int)visible_.size(); }
    long VisibleId(int i) const;
    // 折叠（展开/折叠菜单——Draw/命中只走可见行）
    bool IsCollapsed(long id) const { return collapsed_.count(id) != 0; }
    void SetCollapsed(long id, bool v);
    void ToggleCollapsed(long id);
    // ui-v3：行 hover（RenderFrame 传入鼠标位置——行 hover 底色；未设置=无 hover）
    void SetHoverPoint(double x, double y) { hoverX_ = x; hoverY_ = y; }
    // 重命名（F2 内联编辑——Enter 提交/Esc 取消）
    bool RenameActive() const { return renameId_ != 0; }
    long RenameId() const { return renameId_; }
    void BeginRename(long id);
    void EndRename(bool commit);
    bool HierarchyKey(int key, bool down);                      // 重命名期间按键（EditorApp 路由）
    bool RenameChar(uint32_t cp);                               // t-text：重命名期间字符（码点——中文可输入）
    std::function<void(long, const std::string&)> OnRename;     // 提交回调（EditorApp→SetName+日志）
private:
    UiKit::Rect lastRc_{0, 0, 0, 0};
    void Collect(Core::SceneObject* go, int depth);
    void RefreshVisible();                                       // 折叠过滤（visible_ 重建）
    Core::SceneObject* SceneFind(long id) const;                  // 场景树查 GO（重命名提交）
    Core::Scene* scene_ = nullptr;
    struct Row { long id; std::string name; int depth; double y; };
    std::vector<Row> rows_;
    std::vector<int> visible_;                                   // 可见行索引（DFS 序）
    std::map<long, bool> collapsed_;                             // id→已折叠
    long renameId_ = 0;
    UiKit::TextEdit renameEdit_;
    double hoverX_ = -1e9, hoverY_ = -1e9;                       // ui-v3：行 hover 点（RenderFrame 驱动）
};

// Inspector：选中对象组件字段（原生反射——M3.2；M3.5/t129 折叠分组+只读/热值着色+AssetRef）
// t2/审计映射：标签页（T1——组件/对象/资产/空提示）+ AssetRef 引用选择器（T3——spec §3/§5）
class InspectorPanel {
public:
    void SetSelection(const Selection& sel);
    void Refresh(Core::Scene* scene);
    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc);
    bool OnClick(double x, double y);
    bool OnKey(int key, bool down);
    // t-text：字符（码点）路由——聚焦行的编辑框收中文/emoji（原 OnKey 只吃 ASCII）
    bool CharToFocusedField(uint32_t cp);
    Selection Sel() const { return sel_; }
    // M3.5/t129：Play 模式=只读+运行时值着色（PS-1）
    void SetPlayMode(bool playing) { playMode_ = playing; }
    bool PlayMode() const { return playMode_; }
    std::function<void(const std::string& type, const std::string& field, const std::string& json)> OnCommit;   // Enter 提交（SetField 语义）
    // M3.5/t129：AssetRef 拖放（Project/Hierarchy→字段行赋值——DR-1/DR-3）
    std::function<bool(const std::string& fieldType, const std::string& value)> OnDropAssetRef;   // 返回 true=已处理
    // 折叠态持久化（EditorConfig JSON——IN-2：保存-重开一致）
    std::function<void(const std::string& key, bool open)> OnPersistFold;
    std::vector<std::pair<std::string, bool>> PersistedFolds() const { return persistedFolds_; }
    void SetPersistedFolds(std::vector<std::pair<std::string, bool>> v) { persistedFolds_ = std::move(v); }
    // 公开行快照（测试/拖放联动——IN-1/DR-3；value/runtimeHot=t2 增量——既有 7 字段断言不受影响）
    struct RowSnapshot { std::string type; std::string field; std::string kind; bool isGroup; bool open; double y; double h; std::string value; bool runtimeHot = false; std::string annotation; };
    std::vector<RowSnapshot> Rows() const;
    bool ToggleGroupAt(double x, double y);   // 组头命中→折叠切换（IN-1）
    // t1：组头「移除(Remove)」按钮（组件页每组件组头行尾；Play 灰禁用）
    std::function<void(const std::string& type)> OnRemoveComponent;
    const UiKit::Rect& RemoveButtonRect() const { return removeBtnRc_; }   // 最近 Draw 的按钮命中区（测试）
    std::string RemoveButtonType() const { return removeBtnType_; }
    // 行命中区（拖放高亮——DR-4）
    struct DropTarget { UiKit::Rect rc; std::string fieldType; };
    const std::vector<DropTarget>& DropTargets() const { return dropTargets_; }

    // ===== t2/审计映射 T1：Inspector 标签页（spec §3） =====
    enum class InspectorPage { Components = 0, SceneObject = 1, Asset = 2, Empty = 3 };
    InspectorPage Page() const { return page_; }
    void SetPage(InspectorPage p);      // 换页=激活行缓冲（越界/无效页钳制——spec §3.3）
    static constexpr double kInspTitleH = 24.0;   // 既有标题区（panelBg 标题——行起点=rc.y+24）
    static constexpr double kInspTabH = 22.0;     // 标签栏高（spec §3.2）
    static constexpr double kInspRowsTop = 46.0;  // 行起点（24+22）
    // 资产选中数据源（「资产(Asset)」页——路径/类型/GUID 只读；选择器列表兜底源）
    void SetAssetLibrary(Core::Assets::AssetLibrary* db) { db_ = db; }
    void SelectAsset(const std::string& path);    // 选中资产→仅「资产(Asset)」标签
    void ClearAssetSelection();

    // ===== t2/审计映射 T3：AssetRef 引用选择器（spec §5.2） =====
    bool RefPickerOpen() const { return refPickerOpen_; }
    void OpenRefPicker(const std::string& fieldType = "");   // 目标=组件类型（OnDropAssetRef 首参语义）
    void CloseRefPicker() { refPickerOpen_ = false; refPickerField_.clear(); refPickerRow_ = -1; }
    int RefPickerIndex() const { return refPickerSel_; }
    std::vector<std::string> RefPickerItems() const { return refPickerItems_; }
    const std::string& RefPickerField() const { return refPickerField_; }
    bool RefPickerChoose(int idx);    // Enter/点击提交（走 OnPickRefAsset→OnDropAssetRef 链——不绕过）
    std::function<std::vector<std::string>(const std::string& fieldType)> OnGetRefItems;   // 列表源（EditorApp 接线=project_->ItemPaths()；缺省=db_ 兜底）
    std::function<void(const std::string& fieldType, const std::string& path)> OnPickRefAsset;  // 赋值落点（缺省=OnDropAssetRef 直通——既有链）

    // ===== t2/审计映射 A4：拖放未命中友好提示（不静默——HandleDrop/SimulateDropAsset miss 接线） =====
    void ShowDropHint(const std::string& assetPath);   // 底部提示条（自清理 ~4s/240 帧）
    void ClearDropHint();
    bool DropHintActive() const { return dropHintFrames_ > 0 && !dropHint_.empty(); }
    const std::string& DropHintPath() const { return dropHint_; }
private:
    struct Row {
        std::string type;            // 组件类型名（""=Transform 组）/"SceneObject"/"Asset"
        std::string field;           // 字段名
        std::string value;           // 当前值（字符串显示）
        std::string kind;            // Int/Float/Bool/String/Vec3/Enum/AssetRef/Unmapped
        bool isGroup = false;        // 组头行
        bool open = true;            // 组折叠态
        std::string groupKey;        // 持久化键（instanceId+type）
        std::string typeHint;        // Unmapped 类型名
        std::string annotation;      // t1：组头行为标注（原生/脚本——HasFactory 判定）
        bool runtimeHot = false;     // Play 中运行时值（着色）
        double y = 0, h = 0;
        bool readOnly = false;       // t2：对象/资产页=只读显示行
    };
    // t2：逐页行缓冲（Refresh(scene) 一次性重建；SetPage 切指针——不缓存场景指针→Play 场景替换不悬垂）
    std::vector<Row> compRows_, goRows_, assetRows_;
    std::vector<Row>* rows_ = &compRows_;   // 激活页缓冲
    std::vector<DropTarget> dropTargets_;
    Selection sel_;
    InspectorPage page_ = InspectorPage::Empty;
    int focusRow_ = -1;
    UiKit::TextEdit edits_[64];   // M3 简化：每行一个编辑框
    bool playMode_ = false;
    std::vector<std::pair<std::string, bool>> persistedFolds_;
    UiKit::TabBar tabBar_;                 // 顶部标签栏（Draw 布局/OnClick 命中）
    Core::Assets::AssetLibrary* db_ = nullptr;   // 资产页元数据+选择器列表兜底（T3——spec §3.2/§5.2）
    bool FoldOpen(const std::string& key, bool def = true) const;
    void BuildAssetRows();
    void SetTabIndex(int i);               // TabBar 选择→页（spec §3.3）
    void SyncPage();                       // 激活缓冲+焦点/选择器复位+热值标注
    // —— 引用选择器状态 ——
    UiKit::Rect refPickerRc_{0, 0, 0, 0};
    bool refPickerOpen_ = false;
    int refPickerRow_ = -1;                // 目标行（激活缓冲索引——锚点）
    int refPickerSel_ = -1;
    std::vector<std::string> refPickerItems_;
    std::string refPickerField_;
    std::vector<std::string> RefItemsFromDb() const;   // db_ 兜底列表源
    void DrawRefPicker(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc);
    bool RefPickerHit(double x, double y); // 弹层内命中（含行选中即提交）
    // —— 拖放未命中提示状态（A4）——
    std::string dropHint_;
    int dropHintFrames_ = 0;
    // —— t1：组头「移除」按钮（Draw 记录——OnClick 命中优先）——
    UiKit::Rect removeBtnRc_{0, 0, 0, 0};
    std::string removeBtnType_;
};

// Project：资产浏览（M2 AssetLibrary List/类型过滤/双击加载入口）
// C：顶部分类条（全部(All)/编程文件(Scripts)/模型类(Models)/渲染类(Rendering)/场景(Scenes)——行过滤+计数徽标）
class ProjectPanel {
public:
    enum class Category { All = 0, Scripts, Models, Rendering, Scenes, Count };
    static constexpr double kCatBarH = 22.0;   // 分类条高（面板标题 24px 之下——行起点=24+22）
    void SetDatabase(Core::Assets::AssetLibrary* db) { db_ = db; Refresh(); }
    void Refresh();
    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc);
    bool OnClick(double x, double y, bool doubleClick);   // 分类条命中→切换；行命中→选中；双击=OnOpen 回调
    std::string SelectedAsset() const { return sel_; }
    std::optional<std::string> ItemAt(double x, double y) const;   // M3.5/t129：行命中→资产路径（拖放源）
    std::function<void(const std::string&)> OnOpen;
    const std::vector<std::string>& ItemPaths() const { return items_; }   // t2/T3：只读列表源（选择器/测试——spec §5.2；全量不过滤）
    // —— C：分类（公开测试 API——SetCategory/CurrentCategory/CategoryCounts/分类命中=行过滤）——
    void SetCategory(Category c);
    Category CurrentCategory() const { return cat_; }
    struct Counts { int all = 0, scripts = 0, models = 0, rendering = 0, scenes = 0; };
    Counts CategoryCounts() const { return counts_; }   // 最近 Refresh 的分类计数（徽标/测试）
    const std::vector<std::string>& CategoryItems() const { return visible_; }   // 当前分类过滤后行
    bool SelectAsset(const std::string& path);          // 选中+分类切入（可见性保证——新建脚本菜单用）
    const std::array<UiKit::Rect, 5>& CategoryRects() const { return catRc_; }   // 最近 Draw 分类条命中区
    // 行支持标记（无工厂/无法解析=置灰+说明——Models/Rendering 元数据视图）
    bool RowUnsupported(int visibleIdx) const;
    int VisibleCount() const { return (int)visible_.size(); }
private:
    Core::Assets::AssetLibrary* db_ = nullptr;
    std::vector<std::string> items_;
    std::vector<std::string> visible_;      // 分类过滤后行（Draw/ItemAt/CategoryItems）
    std::vector<bool> unsupported_;         // 与 visible_ 对齐（LoadGeneric 失败=置灰）
    std::string sel_;
    Category cat_ = Category::All;
    Counts counts_{};
    UiKit::Rect lastRc_{0, 0, 0, 0};
    std::array<UiKit::Rect, 5> catRc_{{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
    void RefreshVisible();                              // 分类过滤（visible_/unsupported_ 重建）
    bool CategoryHit(double x, double y, Category& out) const;
};

// ScenePanel：正交俯视场景渲染（网格 25%+对象色块+选中高亮）；t136：2D/3D 切换+orbit/pan/dolly+相机预览（SC-1..4）
// t6：直接操纵——视口选择（HitTest）/2D 拖动/3D 平移 Gizmo（W）+旋转 Gizmo（E）+缩放 Gizmo（R）/网格吸附（Snap）
class ScenePanel {
public:
    void Draw(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc, Core::Scene& scene, long selectedId, bool editable = true);
    void WorldToScreen(const UiKit::Rect& rc, double wx, double wy, double& sx, double& sy) const;
    // t136：3D 视图交互（M3.5 §5——D1 自由 orbit/无锁定；焦点=选中中心或原点）
    void Set3D(bool v) { is3D_ = v; }
    bool Is3D() const { return is3D_; }
    void SetShowBounds(bool v) { showBounds_ = v; }
    bool ShowBounds() const { return showBounds_; }
    void Orbit(double dx, double dy);           // 中键拖（自由 orbit——yaw/pitch）
    void Pan(double dx, double dy);             // 右键拖（画面平移——深度平面）
    void Zoom(double delta);                    // 滚轮（perspective：视野距离；ortho：正交缩放）
    void ResetCamera();                         // 焦点复位（原点/选中）
    void FocusSelected(Core::Scene& scene, long id);   // F 键/双击对象
    // SC-4：相机预览（选中 CameraComponent 对象——预览该相机视角）
    void SetCameraPreview(Core::SceneObject* camObj) { preview_ = camObj; }
    void ExitCameraPreview() { preview_ = nullptr; }
    bool InCameraPreview() const { return preview_ != nullptr; }
    // 片1：正交/透视视图（dolly=透视距离/正交尺寸）
    void SetOrthoView(bool v) { ortho_ = v; }
    bool IsOrthoView() const { return ortho_; }
    double OrthoSize() const { return orthoSize_; }
    // 测试观感（SC-1/2 数值断言）
    double Yaw() const { return yaw_; }
    double Pitch() const { return pitch_; }
    double Dist() const { return dist_; }
    double PanX() const { return panX_; }
    double PanY() const { return panY_; }
    const Core::Vec3& Focus() const { return focus_; }

    // —— t6：直接操纵 ——
    enum class GizmoMode { Move, Rotate, Scale };      // W/E/R（ergo：Scale=完整实现——三轴增量缩放+中心等比）
    enum class GizmoAxis { None, X, Y, Z, Center };    // 三轴 + 中心面（相机朝向平面移动/等比缩放）
    void SetGizmoMode(GizmoMode m) { gizmoMode_ = m; }
    GizmoMode Mode() const { return gizmoMode_; }
    void SetSnap(double v) { snap_ = v > 0.0 ? v : 0.5; }
    double Snap() const { return snap_; }
    // ===== ergo：Gizmo 局部/全局轴切换（L 键+视图 chips——W/E/R 一致） =====
    void SetLocalAxes(bool v) { localAxes_ = v; }
    bool LocalAxes() const { return localAxes_; }
    // ===== ergo：2D 视图缩放/平移（P1——中键拖 pan 世界量随 zoom；滚轮 zoom 0.2..8 钳制） =====
    double ViewScale() const { return scale_; }
    void SetViewScale(double s) { scale_ = std::clamp(s, 0.2, 8.0); }
    void Pan2D(double dxpx, double dypx);            // 中键拖（世界量=px/scale——随 zoom 缩放）
    double Pan2DX() const { return pan2DX_; }
    double Pan2DY() const { return pan2DY_; }
    // ===== t1：视图工具栏 chips（左上——Draw 时计算存储；EditorApp HandleMouse 点击路由；测试直驱） =====
    struct ViewChip { std::string label; UiKit::Rect rc; bool active; };
    std::vector<ViewChip> ViewChips() const { return viewChips_; }
    void SetSnapEnabled(bool on) { snapEnabled_ = on; }   // chip「吸附0.5/吸附OFF」（EditorApp::SetSnapEnabled 同步）
    bool SnapEnabled() const { return snapEnabled_; }
    // t2：MeshVisual.materialAsset 材质解析数据库（EditorApp 接线=engine_->Assets()；null=不解析=组件 color）
    void SetMaterialDatabase(Core::Assets::AssetLibrary* db) { matDb_ = db; }
    // 2D：屏幕→世界 XZ（WorldToScreen 逆；y=0）
    Core::Vec3 ScreenToWorld2D(const UiKit::Rect& rc, double sx, double sy) const;
    // 视口拾取：屏幕距离最近者（阈值 12px；树遍历与渲染同序）；无命中=0；相机预览中=0
    long HitTest(const UiKit::Rect& rc, Core::Scene& scene, double x, double y) const;
    // Gizmo 命中（3D；几何=Draw 同式——Draw 之后 Hit 即“所见即所点”）
    struct GizmoHandle { GizmoAxis axis; UiKit::Rect rc; };   // rc：命中采样区（轴=线段包络±8；旋转=弧首采样点±10；中心=±10）
    GizmoAxis HitGizmo(const UiKit::Rect& rc, const Core::Transform& tf, double x, double y) const;
    const std::vector<GizmoHandle>& GizmoHandleRects() const { return gizmoHandles_; }
    bool Unproject3D(const UiKit::Rect& rc, double sx, double sy, Core::Vec3& origin, Core::Vec3& dir) const;   // 3D 屏幕→射线（t6 拾取/拖放落点）
    // 拖动（EditorApp 驱动）
    void BeginGizmoDrag(GizmoAxis ax, const UiKit::Rect& rc, const Core::Transform& tf, double sx, double sy);
    void BeginMove2D(const UiKit::Rect& rc, const Core::Vec3& worldPos, double sx, double sy);
    Core::Vec3 DragGizmo(const UiKit::Rect& rc, double sx, double sy, bool snap) const;       // Move：新世界位置
    double DragGizmoRotate(const UiKit::Rect& rc, double sx, double sy, bool snap);           // Rotate：累计世界轴旋转角（rad）
    Core::Vec3 DragGizmoScale(const UiKit::Rect& rc, double sx, double sy, bool snap) const;  // ergo：Scale——新局部缩放（沿轴分量/中心等比）
    Core::Vec3 DragMove2D(const UiKit::Rect& rc, double sx, double sy, bool snap) const;      // 2D：新世界位置
    void EndDrag();
    bool Dragging() const { return gizmoAxis_ != GizmoAxis::None || move2D_; }
    GizmoAxis DragAxis() const { return gizmoAxis_; }   // 拖动中当前轴（旋转/平移/缩放——EditorApp 消费）
    Core::Vec3 DragAxisWorldDir() const { return gizmoAxisWorld_; }   // ergo：当前拖动轴世界方向（local=对象旋转基投影）
    const UiKit::Rect& LastRc() const { return lastRc_; }
private:
    void DrawObject(HybridEngine::Platform::IRenderer& r, const UiKit::Rect& rc, Core::SceneObject* go, long selectedId);
    void Draw3D(HybridEngine::Platform::IRenderer& r, const UiKit::Rect& rc, Core::Scene& scene, long selectedId, bool editable);
    void DrawViewChips(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc, bool editable);   // t1：视图 chips（左上——t-ui 紧凑图标条）
    bool Project3D(const UiKit::Rect& rc, const Core::Vec3& w, double& sx, double& sy) const;
    // t6：相机基/Gizmo 几何（Hit 与 Draw 共用同一式——命中=所见）
    void CameraBasis(Core::Vec3& eye, Core::Vec3& fwd, Core::Vec3& right, Core::Vec3& upv, double& fovDeg) const;
    static Core::Vec3 WorldPosOf(const Core::Transform& tf);
    void DrawGizmo(HybridEngine::Platform::IRenderer& r, const UiKit::Rect& rc, const Core::Transform& tf);
    GizmoAxis HitGizmoGeom(const UiKit::Rect& rc, const Core::Transform& tf, double x, double y, bool storeHandles) const;
    void StoreHandle(GizmoAxis ax, double cx, double cy, double pad) const;
    void GizmoWorldAxes(const Core::Transform& tf, Core::Vec3 out[3]) const;   // ergo：local=世界矩阵旋转部分投影（W/E/R 一致）；global=单位轴
    double scale_ = 1.0;
    // t136 3D 相机状态
    bool is3D_ = true;      // t1：默认 3D（首屏 3D 观感——产品决策 2026-08-29；G 键切换+EditorConfig 持久化不变）
    bool showBounds_ = false;   // 专业视图：显示所有对象包围盒线框（Gizmo 线框风格）
    double yaw_ = 0.9, pitch_ = 0.55, dist_ = 8.0;
    double panX_ = 0, panY_ = 0;
    double orthoSize_ = 5.0;
    bool ortho_ = false;                    // false=透视（默认）；true=正交
    Core::Vec3 focus_{0, 0, 0};
    Core::SceneObject* preview_ = nullptr;   // 相机预览对象（CameraComponent）
    // t6 状态
    GizmoMode gizmoMode_ = GizmoMode::Move;
    double snap_ = 0.5;                     // 网格吸附步长（世界单位；默认 0.5——t9 审计 §10.1）
    bool snapEnabled_ = true;               // t1：chip「吸附0.5/吸附OFF」状态（EditorApp::SetSnapEnabled 同步）
    std::vector<ViewChip> viewChips_;       // t1：最近 Draw 的视图 chips 快照（命中/测试）
    Core::Assets::AssetLibrary* matDb_ = nullptr;   // t2：材质资产解析库（MeshVisual.materialAsset→有效色）
    GizmoAxis gizmoAxis_ = GizmoAxis::None;
    bool move2D_ = false;
    Core::Vec3 gizmoOrigin_{};              // 拖动起点世界位置（选中对象）
    double gizmoTotalAngle_ = 0;            // Rotate 累计角（rad；snap 后）
    double gizmoLastAngle_ = 0;             // Rotate 上一帧角（增量累计）
    Core::Vec3 gizmoProj0_{};               // 起点屏幕投影（Rotate 参考中心）
    UiKit::Rect lastRc_{0, 0, 0, 0};        // 最近绘制场景矩形（测试）
    mutable std::vector<GizmoHandle> gizmoHandles_;   // 最近 Draw/HitGizmo 的几何快照
    // —— ergo 状态 ——
    double pan2DX_ = 0, pan2DY_ = 0;        // 2D 视图平移（世界单位——随 zoom 比例换算）
    bool localAxes_ = false;                // Gizmo 轴系（false=全局 World；true=局部 Local——L 键/chip）
    Core::Vec3 gizmoAxisWorld_{1, 0, 0};    // BeginGizmoDrag 记录拖动轴世界方向（local=旋转基投影）
    Core::Vec3 gizmoStartScale_{1, 1, 1};   // Scale 拖动起点局部缩放
    double gizmoStartT_ = 0;                // Scale 轴拖：起点线参数（t 比值→因子）
    double gizmoStartD_ = 0;                // Scale 中心拖：起点屏幕距离（d 比值→因子）
};

} // namespace HybridEngine::Editor