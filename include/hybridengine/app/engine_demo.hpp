#pragma once
#include "hybridengine/platform/renderer.hpp"
#include <cstddef>
#include <cstdint>

namespace HybridEngine::App {

// M2.5 parity：确定性黄金帧场景（原 tests/parity_scene.hpp 升级为公共引擎 parity 校准场景）
// 几何全部整数坐标+整数半径——仅 IEEE 正确舍入运算（无超越函数）→ 跨编译链恒定
constexpr int kDemoWidth = 1280;
constexpr int kDemoHeight = 800;

inline void RenderDemoScene(HybridEngine::Platform::IRenderer* r) {
    using namespace HybridEngine::Platform;
    r->Clear({18, 22, 30, 255});
    r->FillRoundedRect(200, 150, 880, 500, 24, {46, 58, 96, 255});
    r->FillRect(240, 180, 60, 40, {235, 90, 60, 255});
    r->ClearRect(640, 180, 100, 40, {255, 204, 64, 255});
    r->FillCircle(640, 400, 60, {96, 160, 255, 255});
    r->FillTriangle(520, 520, 760, 520, 640, 340, {140, 220, 120, 255});
    r->FillQuad(300, 600, 420, 550, 540, 600, 420, 650, {235, 90, 60, 255});
    r->DrawLine(260, 260, 1020, 260, {200, 200, 210, 255}, 3);
    r->DrawLine(260, 300, 1020, 620, {80, 100, 200, 255}, 2);
}

// FNV-1a 64（uint32 小端字节序——与 v1 FnvHash 同口径）
inline uint64_t DemoFrameHash(const uint32_t* px, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) {
        uint32_t v = px[i];
        for (int k = 0; k < 4; ++k) {
            h ^= (uint64_t)((v >> (k * 8)) & 0xFF);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

} // namespace HybridEngine::App