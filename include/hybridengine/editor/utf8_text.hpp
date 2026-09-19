#pragma once
#include <cstdint>
#include <string>

namespace HybridEngine::Editor {

// —— UTF-8 小工具（编辑器共用；不引入第三方）——
// 为什么必须有：内部一律 UTF-8（std::string），但**一切编辑/截断/量宽必须按码点**做——
// 否则中文/emoji 会被按字节切开，变成乱码或半个字符（用户实测：「无法输入中文汉字」的根因之一就是
// 输入框按字节追加/按字节 pop_back）。
//
// t-text：本头原属 script_text.hpp（脚本编辑器私有），现上提为编辑器**公共**文本层——
// UI 输入框（UiKit::TextEdit）、组件过滤框、插件路径框、脚本缓冲共用同一套码点语义（单一实现）。
inline int Utf8SeqLen(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;   // 非法首字节：当单字节走（不抛异常，编辑期不中断）
}

inline bool Utf8IsContinuation(unsigned char b) { return (b & 0xC0) == 0x80; }

inline int Utf8CpCount(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        i += (size_t)Utf8SeqLen((unsigned char)s[i]);
        ++n;
    }
    return n;
}

inline int Utf8ByteOffset(const std::string& s, int cpIndex) {
    if (cpIndex <= 0) return 0;
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (n == cpIndex) return (int)i;
        i += (size_t)Utf8SeqLen((unsigned char)s[i]);
        ++n;
    }
    return (int)s.size();
}

inline uint32_t Utf8DecodeAt(const std::string& s, int bytePos, int* lenOut) {
    if (bytePos < 0 || bytePos >= (int)s.size()) { if (lenOut) *lenOut = 0; return 0; }
    unsigned char b0 = (unsigned char)s[(size_t)bytePos];
    int len = Utf8SeqLen(b0);
    // 越界/续字节缺失 → 退化为 U+FFFD 单字节（编辑期容错优先）
    if (len == 1 || bytePos + len > (int)s.size()) {
        if (lenOut) *lenOut = 1;
        return b0 < 0x80 ? b0 : 0xFFFD;
    }
    for (int k = 1; k < len; ++k)
        if (!Utf8IsContinuation((unsigned char)s[(size_t)bytePos + k])) { if (lenOut) *lenOut = 1; return 0xFFFD; }
    uint32_t cp = 0;
    if (len == 2) cp = ((uint32_t)(b0 & 0x1F) << 6) | ((uint32_t)s[(size_t)bytePos + 1] & 0x3F);
    else if (len == 3) cp = ((uint32_t)(b0 & 0x0F) << 12) | (((uint32_t)s[(size_t)bytePos + 1] & 0x3F) << 6) |
                            ((uint32_t)s[(size_t)bytePos + 2] & 0x3F);
    else cp = ((uint32_t)(b0 & 0x07) << 18) | (((uint32_t)s[(size_t)bytePos + 1] & 0x3F) << 12) |
              (((uint32_t)s[(size_t)bytePos + 2] & 0x3F) << 6) | ((uint32_t)s[(size_t)bytePos + 3] & 0x3F);
    if (lenOut) *lenOut = len;
    return cp;
}

inline void Utf8Append(std::string& out, uint32_t cp) {
    // t-edit 加固：非法码点（> U+10FFFF 或 UTF-16 代理区）一律写成 U+FFFD。
    // 为什么要有这道闸：C++ 里 `'中'` 这种**多字节字符字面量**是个实现定义的整数
    // （GCC 把三个字节打包成 0xADB8E4），直接当码点用会编出 4 字节的非法 UTF-8——
    // 于是"输入一个中文字"变成"插入一个乱码字符"。宁可变替换符，也不要产生非法字节。
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// 删除末尾**一个码点**（返回 false=空串）。顺带清掉遗留的孤立续字节（历史按字节退格造成的半字符自愈）。
// 为什么不能用 std::string::pop_back：它删 1 字节，中文 3 字节会被切成非法序列 → 后续渲染成乱码。
inline bool Utf8PopBack(std::string& s) {
    if (s.empty()) return false;
    size_t i = s.size();
    while (i > 0 && Utf8IsContinuation((unsigned char)s[i - 1])) --i;
    if (i > 0) --i;   // 落到该码点首字节
    s.erase(i);       // i==0（整串是孤立续字节，病态）→ 全清
    return true;
}

// 末尾追加一个码点（过滤控制字符：\r 忽略、\n/\t 由调用方决定——输入框一律单行，调用方过滤）
inline void Utf8AppendChar(std::string& s, uint32_t cp) {
    if (cp == 0 || cp == '\r' || cp == '\n' || cp == '\t') return;
    if (cp < 0x20) return;          // 其它控制字符丢弃
    if (cp == 0x7F) return;         // DEL
    Utf8Append(s, cp);
}

} // namespace HybridEngine::Editor
