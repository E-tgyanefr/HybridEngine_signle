#pragma once

namespace HybridEngine::Platform {

// M1：视口策略（v1 ViewportPolicy 端口——多分辨率统一：物理客户区 ↔ 设计坐标）
enum class FitMode { Stretch, Letterbox, Fill };

struct Viewport {
    double scale = 1.0;   // 物理→设计 缩放（min 轴）
    double ox = 0, oy = 0; // 居中偏移（物理像素）
};

class ViewportPolicy {
public:
    // Stretch 无等比语义→ {1,0,0}（宿主自异变换）；Fill=max(scale) 铺满裁边；Letterbox=min(scale) 等比黑边
    static Viewport Compute(int cw, int ch, int dw, int dh, FitMode mode);
    // 物理客户区 → 设计坐标（scale<=0 → 恒等）
    static void ToVirtual(const Viewport& vp, double x, double y, double& vx, double& vy);
    // 设计坐标 → 物理客户区（往返恒等）
    static void FromVirtual(const Viewport& vp, double vx, double vy, double& x, double& y);
};

} // namespace HybridEngine::Platform
