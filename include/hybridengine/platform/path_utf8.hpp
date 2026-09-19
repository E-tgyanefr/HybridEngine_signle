#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace HybridEngine::Platform {

// —— UTF-8 路径工具（Windows 必用；否则中文路径直接崩）——
//
// 问题（实测崩溃）：
//   引擎/编辑器内部一律用 UTF-8 窄串（std::string）传路径，而 `std::filesystem::path` 由**窄串**
//   构造时，Windows 上按「当前 ANSI 代码页」解释该串；libstdc++（MinGW）转换失败时**抛异常**：
//     terminate called after throwing an instance of 'std::filesystem::__cxx11::filesystem_error'
//       what():  filesystem error: Cannot convert character sequence: Illegal byte sequence
//   → 项目路径只要含中文（`D:\...\弹幕射击`），编辑器一启动就崩。
//   注意：`fs::exists(p, ec)` 这类带 ec 的重载**救不了**——异常来自 path 构造，不是系统调用。
//   同理 `std::ifstream f(窄串)` 用 ANSI 打开文件，中文路径**静默打不开**（误判成"文件不存在"）。
//
// 为什么这么实现：
//   · 不用 `path(std::u8string)`：本机 libstdc++ 对 char8_t 源同样抛 Illegal byte sequence（实测）。
//   · 不 include windows.h：`CreateWindow` 等宏会把 `Platform::CreateWindow(...)` 炸掉
//     （且 core 是 platform 的下层，不能反向依赖 platform 库）。
//   · 于是自研 UTF-8 ⇄ UTF-16 转换（手写码点解码/编码），头文件内联、无平台头依赖；
//     转换结果喂给 `fs::path(std::wstring)`（Windows 上是宽字符原生）。
//
// 2026-09-17（0 警告门禁）：原实现用 `std::wstring_convert<codecvt_utf8_utf16<wchar_t>>`——
//   C++17 起弃用、C++26 移除，GCC 16.1 三链均告警（-Wdeprecated-declarations）。
//   改为手写转换：语义与 wstring_convert 一致（本用途 = 路径转换，不需要 GBK 等代码页），
//   且**非法字节序不再抛异常**：按 U+FFFD 替换（旧实现 catch(...) 返回空串 = 路径静默丢失，
//   新实现保留尽可能多的合法前缀，对中文路径更稳——实测用例零变化）。
#if defined(_WIN32)
namespace detail {
// UTF-8 → UTF-16（BMP + 代理对；非法序列写 U+FFFD 并前进 1 字节，绝不抛）
inline std::wstring Utf8ToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        size_t len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }   // 非法首字节
        if (i + len > n) { out.push_back(0xFFFD); break; }   // 截断
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }   // 续字节非法
            cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        // 最短编码校验（防过长编码）——过长/越界一律 U+FFFD
        static const uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(0xFFFD); i += len; continue;
        }
        if (cp <= 0xFFFF) {
            out.push_back((wchar_t)cp);
        } else {   // 代理对（UTF-16）
            cp -= 0x10000;
            out.push_back((wchar_t)(0xD800 + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
        i += len;
    }
    return out;
}
// UTF-16 → UTF-8（未配对代理写 U+FFFD）
inline std::string WideToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = (uint32_t)(uint16_t)w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {   // 高代理
            if (i + 1 < w.size() && (uint16_t)w[i + 1] >= 0xDC00 && (uint16_t)w[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)(uint16_t)w[++i] - 0xDC00);
            } else { cp = 0xFFFD; }   // 未配对
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;              // 孤立低代理
        }
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
} // namespace detail
inline std::wstring Utf8ToWidePath(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    return detail::Utf8ToWide(utf8);
}
inline std::string WideToUtf8Path(const std::wstring& w) {
    if (w.empty()) return std::string();
    return detail::WideToUtf8(w);
}
inline std::filesystem::path U8Path(const std::string& utf8) { return std::filesystem::path(Utf8ToWidePath(utf8)); }
inline std::string PathToU8(const std::filesystem::path& p) { return WideToUtf8Path(p.wstring()); }
#else
// 非 Windows：路径本就是 UTF-8 字节串，直通。
inline std::filesystem::path U8Path(const std::string& utf8) { return std::filesystem::path(utf8); }
inline std::string PathToU8(const std::filesystem::path& p) { return p.string(); }
#endif

} // namespace HybridEngine::Platform
