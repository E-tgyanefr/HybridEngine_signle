#pragma once
#include "hybridengine/platform/window.hpp"
#include "hybridengine/editor/project.hpp"
#include "hybridengine/editor/uikit.hpp"
#include <memory>
#include <string>
#include <vector>

namespace HybridEngine::Editor {

// A：主菜单（Launcher）——同一 exe 双模式：默认=启动器；--project <dir> / --root <dir> → 编辑器
//   复用 UiKit 离屏渲染风格（Slate 配色/自绘控件——与六区编辑器同语言）
struct LauncherOptions {
    std::string projectsDir = Project::DefaultProjectsDir();   // *.hproj 扫描目录（--projects <dir>）
    int width = 1280, height = 840;   // 分辨率修复：主菜单默认 1280×840 物理像素（150% 屏≈1920×1260）
    bool createWindow = true;   // false=离屏（测试）
};

class LauncherApp {
public:
    static constexpr const char* kVersion = "v0.3";
    explicit LauncherApp(const LauncherOptions& opts = {});
    ~LauncherApp();

    int Run();                                       // 窗口主循环（pendingPath_ 非空=选定了项目）
    void RenderFrame(HybridEngine::Platform::IRenderer& r);
    void RefreshProjects();

    // —— 公开语义（测试直驱；与 UI 点击同链）——
    const std::vector<Project::ProjectInfo>& Projects() const { return projects_; }
    int SelectedIndex() const { return sel_; }
    bool Select(int i);
    // 新建项目：<projects>\<name>\ + Assets\ + <name>.hproj（JSON name/guid/root/createdAt/lastScene=""）
    //            → 刷新列表+选中+进入编辑器待命（pendingPath_）。失败=返回 false（newError_ 记录原因）
    bool NewProject(const std::string& name);
    // 打开项目：读 hproj→刷新最近打开时间→进入编辑器待命（pendingPath_=hproj；pendingRoot_=项目根）
    bool OpenProject(const std::string& hprojPath);
    // 删除项目（需确认）：首次调用=armed（返回 false——控制台提示+按钮变「确认删除?」）；
    //                    同一路径再次调用=执行（删除项目目录）并返回 true
    bool DeleteProject(const std::string& hprojPath);
    bool ConfirmArmed() const { return deleteArmed_; }
    const std::string& PendingProjectPath() const { return pendingPath_; }
    const std::string& PendingProjectRoot() const { return pendingRoot_; }
    std::string ProjectsDir() const { return projectsDir_; }
    std::string NewError() const { return newError_; }

    // —— 新建输入态 ——
    bool NewMode() const { return newMode_; }
    void SetNewMode(bool on) { newMode_ = on; if (on) newName_.clear(); newError_.clear(); }
    std::string NewName() const { return newName_; }

    // —— 事件路由（UI/测试同链）——
    bool ClickAt(double x, double y, bool doubleClick = false);
    bool HandleKey(int key, bool down);

    HybridEngine::Platform::IWindow* Window() const { return window_.get(); }
    HybridEngine::Platform::IRenderer* Renderer() const { return renderer_.get(); }
    double DpiScale() const { return dpiScale_; }

    // 命中区快照（最近 RenderFrame——测试）
    struct Rects {
        UiKit::Rect open, newBtn, del, openDir, refresh, create, cancel, nameEdit;
        std::vector<UiKit::Rect> rows;   // 与 Projects() 对齐
    };
    const Rects& LastRects() const { return rc_; }

private:
    void RefreshDpi();
    bool HitRect(double x, double y, const UiKit::Rect& rc) const;
    UiKit::Rect RowRect(int i) const;
    void DrawButton(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, const UiKit::Rect& rc,
                    const std::string& label, bool enabled, bool warn = false);
    void DrawNewModal(HybridEngine::Platform::IRenderer& r, const UiKit::Style& s, int w, int h);

    std::vector<Project::ProjectInfo> projects_;
    std::string projectsDir_;
    int sel_ = -1;
    bool newMode_ = false;
    std::string newName_;
    std::string newError_;
    bool deleteArmed_ = false;
    std::string deleteArmedPath_;
    std::string pendingPath_, pendingRoot_;
    int width_, height_;
    double dpiScale_ = 1.0;
    std::unique_ptr<HybridEngine::Platform::IWindow> window_;
    std::unique_ptr<HybridEngine::Platform::IRenderer> renderer_;
    UiKit::TextEdit nameEdit_;   // 新建项目名输入（NewMode 聚焦）
    Rects rc_;
};

} // namespace HybridEngine::Editor
