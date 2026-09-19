#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace HybridEngine::Platform {

// 文本白掩码（0xFFFFFFFF=前景，0=透明）。平台无关；Windows 仍走 GDI，本接口用于 Linux/macOS 等非 GDI 平台。
struct TextMask {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;   // row-major，w×h

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    void clear() { width = 0; height = 0; pixels.clear(); }
};

// UTF-8 字符串 → 白掩码。优先 FreeType 系统字体；不可用/失败=内置位图回退。
bool RasterizeText(const char* utf8, int sizePx, TextMask& out);

// 当前进程是否使用 FreeType 系统字体光栅（调试/诊断用）
bool TextRasterUsingFreeType();

} // namespace HybridEngine::Platform
