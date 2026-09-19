#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// t1：RuntimeFont —— **平台层**文本光栅（GDI createDTB 白掩码；零第三方：GDI=系统库）
// 语义：字符串单次光栅到白掩码位图（GlyphCache LRU 512——key=(utf8,size)）；
//       绘制=掩码点→色（软件逐像素）；measure=掩码宽/高（像素）；CJK 直接支持（GDI Unicode）。
// t10：掩码零拷贝——px 为共享引用（引用计数）：缓存与 Op 共享同一缓冲（录制零拷贝）；
//       LRU 淘汰后 Op 仍 pinned（引用计数保持——回放安全）；缓存命中=共享同一缓冲（无复制）。
//
// ── 归属说明（2026-09-18 从 src/bind/runtime_text.* 迁到平台层并正名）──
// 它此前住在 `src/bind/`、命名空间是 `HybridEngine::Bind`、名字叫 runtime_text，
// 但这三件事都名不副实：
//   · 它与 C ABI / 托管承载**没有任何关系**——不认识 ms_engine，也不碰 Bridge；
//   · 它是"字符串→白掩码位图 + LRU + 共享引用"的**文本光栅**，按平台能力提供
//     （Windows 走 GDI，非 Windows 是明确降级的安全桩），与 `platform/font_raster.cpp` 同域；
//   · 原来的 include 靠 `target_include_directories(... src/bind)` 才能被 bind/tests/tools 找到，
//     属"实现目录被当公开头目录用"。
// 现在：公开头在 `include/hybridengine/platform/`，实现在 `src/platform/`，命名空间 `Platform`。
//
// ⚠ 别与 `editor/cjk_font.hpp` 混为一谈：那是**编辑器域**的 GDI 文本光栅（Ui/Mono 两族、
//   点径、span 快路径、列对齐）。两者都叫"文本光栅"但服务对象不同（bind 回放/录制 vs 编辑器 UI），
//   **不要合并**。想统一的是"语义重复"，不是"名字相似"。
namespace HybridEngine::Platform {

class RuntimeFont {
public:
    // 白掩码位图（DIBSection 32bpp 原始像素——B/G/R 字节与 editor CjkFont 同口径：亮度和>180=前景）
    struct Mask {
        int w = 0, h = 0;
        std::shared_ptr<std::vector<uint32_t>> px;   // t10：共享像素缓冲（缓存持有 1 引用；Op pin +1）
    };

    static RuntimeFont& Instance();          // 全局单例（主线程语义——ABI 线程模型内）

    const Mask* Get(const char* utf8, int sizePx);   // 光栅化/缓存查找（失败=nullptr）
    bool Measure(const char* utf8, int sizePx, int* outW, int* outH);   // 像素宽/高
    void ClearCache();                                 // 测试辅助（清空 LRU；pinned Op 引用不受影响）

private:
    struct Key {
        std::string utf8;
        int size = 0;
    };
    struct KeyLess {
        bool operator()(const Key& a, const Key& b) const {
            if (a.size != b.size) return a.size < b.size;
            return a.utf8 < b.utf8;
        }
    };
    struct Entry {
        Mask mask;
        uint64_t stamp = 0;   // LRU 时间戳（最近使用升序）
    };

    std::map<Key, Entry, KeyLess> cache_;
    uint64_t clock_ = 0;
    static constexpr size_t kCap = 512;      // GlyphCache LRU 上限
};

} // namespace HybridEngine::Platform
