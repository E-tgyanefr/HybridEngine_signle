#pragma once
#include "hybridengine/core/math.hpp"
#include "hybridengine/platform/window.hpp"   // IWindow（Handle/ClientWidth/ClientHeight/Present + PresentPixels）
#include "hybridengine/render3d/render3d.hpp"  // MeshData/Camera/Mat4（同构复用——GPU 实现与软渲接口对齐）
#include <cstdint>
#include <vector>

namespace HybridEngine::Render3D {

// ============ m-gpu：D3D11 硬件光栅器（Render3D 的 GPU 实现） ============
// 职责：场景三角形（MeshData+世界矩阵+tint）→ 硬件着色管线（HLSL：VSM 视变换/投影、
// VSNormal 世界法线、PSTex 朗伯光照/背面剔除/深度缓冲）→ 交换链呈现。
// 帧序与 Render3D 同（Begin/SetCamera/Submit/End/Composite）；零第三方（D3D11/DXGI/HLSL
// 编译器=Windows 系统组件——与 user32/gdi32/winmm 同红线）。
// 无窗口/初始化失败=安全降级（Begin 返回 false；调用方回退软件路径）。
class GpuRenderer {
public:
    ~GpuRenderer();

    // 初始化：挂窗口句柄（0=失败/降级）。像素格式=客户区（DPI 感知）。
    // ⚠ vsync 语义（2026-09-17 修正）：此前该参数**被接收后丢弃**（`(void)vsync`），
    //   而 `Composite()` 硬编码 `Present(0, 0)` ⇒ 垂直同步**永远不生效**、且调用方无从得知。
    //   现改为：`vsync=true` → `Present(1, 0)`（等待垂直同步）；`false` → `Present(0, 0)`（立即返回）。
    //   查询实际生效值用 `EffectiveVsync()`。Init 失败/降级时返回 false。
    bool Init(HybridEngine::Platform::IWindow* wnd, bool vsync = true);
    bool Ok() const;   // 实现=impl_ && impl_->init（头文件不访问不完整类型）
    int Width() const { return width_; }
    int Height() const { return height_; }
    // 实际生效的垂直同步开关（Init 之后才有意义；未初始化=false）。
    // 为什么要有它：调用方（编辑器/宿主）需要区分"我要 vsync"与"vsync 真的在用"，
    //   否则又回到"静默忽略参数"的老问题。
    bool EffectiveVsync() const { return vsync_; }

    // —— 与 Render3D 同序（软渲等价接口）——
    bool Begin(int w, int h);                              // 开始帧（清屏+深度；w/h<1→客户区）
    void SetCamera(const Camera& cam, double aspect, double deviceScale = 1.0);
    // 提交网格（CPU 侧顶点展开 + world 矩阵→GPU；tint=a>0 覆盖 baseColor——同 Submit 语义）
    // t7：tex.Valid()=纹理采样（UV→SRV+Sampler——PSTex；无纹理=既有 PSM 纯色路径）
    void Submit(const MeshData& mesh, const HybridEngine::Core::Mat4& world, HybridEngine::Platform::Rgba tint,
                const TextureRef& tex = {});
    void End();
    // 呈现（交换链 Present；窗口 Present 像素=客户区）
    void Composite();               // 呈现到窗口（PresentPixels——GPU 表面）
    // 选项（与 RasterOptions 对齐子集）
    struct Options {
        HybridEngine::Platform::Rgba clear{8, 10, 18, 255};
        HybridEngine::Platform::Rgba ambient{30, 30, 36, 255};
        HybridEngine::Core::Vec3 lightDir{-0.5, -0.7, -0.5};
        double lambert = 0.85;
        bool cullBackface = true;
    };
    void SetOptions(const Options& o) { opts_ = o; }

    // 读回当前帧像素（staging 回读——验证/截图用：out 填充 w×h 0xFFRRGGBB；成功=true）
    bool ReadbackFrame(uint32_t* out) const;

private:
    struct Impl;   // D3D11 内部（d3d11.h 不泄露到 public 头）
    Impl* impl_ = nullptr;
    HybridEngine::Platform::IWindow* wnd_ = nullptr;
    int width_ = 0, height_ = 0;
    bool vsync_ = false;   // Composite() 用来决定 Present 的 sync interval（见 Init 注释）
    Options opts_{};
    HybridEngine::Core::Mat4 viewProj_{};   // SetCamera 缓存（Submit 常量更新用）
};

// 工厂（nullptr=无 D3D11/DXGI——安全降级）
GpuRenderer* CreateGpuRenderer(HybridEngine::Platform::IWindow* wnd, bool vsync = true);

} // namespace HybridEngine::Render3D
