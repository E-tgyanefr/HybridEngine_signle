#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace HybridEngine::Core::Assets {

// M2：msjson（手写 JSON 最小集——零第三方红线；保序对象=vector<pair>（diff 友好/确定性序列化））
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;   // 保序对象

    bool IsNull() const { return type == Type::Null; }
    static JsonValue Null() { return {}; }
    static JsonValue Bool(bool v) { JsonValue j; j.type = Type::Bool; j.b = v; return j; }
    static JsonValue Number(double v) { JsonValue j; j.type = Type::Number; j.num = v; return j; }
    static JsonValue String(std::string v) { JsonValue j; j.type = Type::String; j.str = std::move(v); return j; }
    static JsonValue Array() { JsonValue j; j.type = Type::Array; return j; }
    static JsonValue Object() { JsonValue j; j.type = Type::Object; return j; }
    void Push(const JsonValue& v) { arr.push_back(v); }
    void Set(const std::string& k, JsonValue v) {
        for (auto& kv : obj) if (kv.first == k) { kv.second = std::move(v); return; }
        obj.emplace_back(k, std::move(v));
    }
    const JsonValue* Find(const std::string& k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

// —— JSON 解析（严格：尾随字符/语法错误→false+err；errPos=失败字符偏移（0-based——行号定位用））——
bool JsonParse(const std::string& text, JsonValue& out, std::string& err, size_t* errPos = nullptr);

// —— 序列化（indent<0=紧凑；>0=缩进美化——保序）——
std::string JsonStringify(const JsonValue& v, int indent = 2);

// FNV-1a 64（与 v1 FnvHash/parity 同实现——uint32 字节序小端）
inline uint64_t Fnv1a64Bytes(const uint8_t* data, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; ++i) { h ^= (uint64_t)data[i]; h *= 1099511628211ULL; }
    return h;
}

} // namespace HybridEngine::Core::Assets

// —— 解析实现（头文件 inline——单头零第三方）——
namespace HybridEngine::Core::Assets {
namespace parser_detail {

inline bool SkipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return true;
}

inline bool ParseString(const char*& p, const char* end, std::string& out, std::string& err) {
    if (p >= end || *p != '"') { err = "expected string"; return false; }
    ++p;
    out.clear();
    while (p < end) {
        char c = *p;
        if (c == '"') { ++p; return true; }
        if (c == '\\') {
            ++p;
            if (p >= end) { err = "bad escape"; return false; }
            char e = *p;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (p + 4 >= end) { err = "bad \\u"; return false; }
                unsigned cp = 0;
                for (int i = 1; i <= 4; ++i) {
                    char hc = p[i];
                    unsigned d = 0;
                    if (hc >= '0' && hc <= '9') d = (unsigned)(hc - '0');
                    else if (hc >= 'a' && hc <= 'f') d = (unsigned)(hc - 'a' + 10);
                    else if (hc >= 'A' && hc <= 'F') d = (unsigned)(hc - 'A' + 10);
                    else { err = "bad \\u hex"; return false; }
                    cp = cp * 16 + d;
                }
                p += 4;
                // 仅支持 BMP 单码点（UTF-8 三字节）——代理对不在 M2 范围（场景元数据 ASCII）
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                break;
            }
            default: err = "bad escape"; return false;
            }
            ++p;
        } else if ((unsigned char)c < 0x20) { err = "control char in string"; return false; }
        else { out += c; ++p; }
    }
    err = "unterminated string";
    return false;
}

inline bool ParseNumber(const char*& p, const char* end, double& out) {
    const char* s = p;
    if (p < end && (*p == '-')) ++p;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9') { ++p; digits = true; }
    if (p < end && *p == '.') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') { ++p; digits = true; }
    }
    if (!digits) return false;
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) ++p;
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    std::string tok(s, (size_t)(p - s));
    try { out = std::stod(tok); } catch (...) { return false; }
    return true;
}

inline bool ParseValue(const char*& p, const char* end, JsonValue& out, std::string& err);

inline bool ParseArray(const char*& p, const char* end, JsonValue& out, std::string& err) {
    ++p;
    out = JsonValue::Array();
    SkipWs(p, end);
    if (p < end && *p == ']') { ++p; return true; }
    while (true) {
        JsonValue v;
        if (!ParseValue(p, end, v, err)) return false;
        out.Push(v);
        SkipWs(p, end);
        if (p >= end) { err = "unterminated array"; return false; }
        if (*p == ',') { ++p; SkipWs(p, end); continue; }
        if (*p == ']') { ++p; return true; }
        err = "expected , or ]";
        return false;
    }
}

inline bool ParseObject(const char*& p, const char* end, JsonValue& out, std::string& err) {
    ++p;
    out = JsonValue::Object();
    SkipWs(p, end);
    if (p < end && *p == '}') { ++p; return true; }
    while (true) {
        SkipWs(p, end);
        std::string key;
        if (!ParseString(p, end, key, err)) return false;
        SkipWs(p, end);
        if (p >= end || *p != ':') { err = "expected :"; return false; }
        ++p;
        JsonValue v;
        if (!ParseValue(p, end, v, err)) return false;
        out.Set(key, std::move(v));
        SkipWs(p, end);
        if (p >= end) { err = "unterminated object"; return false; }
        if (*p == ',') { ++p; continue; }
        if (*p == '}') { ++p; return true; }
        err = "expected , or }";
        return false;
    }
}

inline bool ParseValue(const char*& p, const char* end, JsonValue& out, std::string& err) {
    SkipWs(p, end);
    if (p >= end) { err = "unexpected EOF"; return false; }
    char c = *p;
    if (c == '{') return ParseObject(p, end, out, err);
    if (c == '[') return ParseArray(p, end, out, err);
    if (c == '"') { std::string s; if (!ParseString(p, end, s, err)) return false; out = JsonValue::String(std::move(s)); return true; }
    if (c == 't') { if (end - p >= 4 && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') { p += 4; out = JsonValue::Bool(true); return true; } err = "bad literal"; return false; }
    if (c == 'f') { if (end - p >= 5 && p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') { p += 5; out = JsonValue::Bool(false); return true; } err = "bad literal"; return false; }
    if (c == 'n') { if (end - p >= 4 && p[1] == 'u' && p[2] == 'l' && p[3] == 'l') { p += 4; out = JsonValue::Null(); return true; } err = "bad literal"; return false; }
    double num = 0;
    if (ParseNumber(p, end, num)) { out = JsonValue::Number(num); return true; }
    err = "unexpected character";
    return false;
}

} // namespace parser_detail

inline bool JsonParse(const std::string& text, JsonValue& out, std::string& err, size_t* errPos) {
    const char* p = text.c_str();
    const char* end = p + text.size();
    const char* start = p;
    auto fail = [&](const char* at) { if (errPos) *errPos = (size_t)(at - start); return false; };
    // UTF-8 BOM 剥离（真实世界 JSON 资产含 EF BB BF——引擎使用者 corpus 实证；全消费者 BOM 安全）
    if (end - p >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    parser_detail::SkipWs(p, end);
    if (!parser_detail::ParseValue(p, end, out, err)) return fail(p);
    parser_detail::SkipWs(p, end);
    if (p != end) { err = "trailing characters"; return fail(p); }
    if (errPos) *errPos = text.size();
    return true;
}

namespace stringify_detail {

inline void AppendString(std::string& s, const std::string& in) {
    s += '"';
    for (char c : in) {
        switch (c) {
        case '"': s += "\\\""; break;
        case '\\': s += "\\\\"; break;
        case '\n': s += "\\n"; break;
        case '\r': s += "\\r"; break;
        case '\t': s += "\\t"; break;
        default: s += c;
        }
    }
    s += '"';
}

inline void AppendIndent(std::string& s, int indent, int depth) {
    if (indent >= 0) s.append((size_t)(indent * depth), ' ');
}

inline void AppendValue(std::string& s, const JsonValue& v, int indent, int depth) {
    switch (v.type) {
    case JsonValue::Type::Null: s += "null"; break;
    case JsonValue::Type::Bool: s += v.b ? "true" : "false"; break;
    case JsonValue::Type::Number: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.num);
        s += buf;
        break;
    }
    case JsonValue::Type::String: AppendString(s, v.str); break;
    case JsonValue::Type::Array: {
        if (v.arr.empty()) { s += "[]"; break; }
        s += '[';
        if (indent >= 0) s += '\n';
        for (size_t i = 0; i < v.arr.size(); ++i) {
            AppendIndent(s, indent, depth + 1);
            AppendValue(s, v.arr[i], indent, depth + 1);
            if (i + 1 < v.arr.size()) s += ',';
            if (indent >= 0) s += '\n';
        }
        AppendIndent(s, indent, depth);
        s += ']';
        break;
    }
    case JsonValue::Type::Object: {
        if (v.obj.empty()) { s += "{}"; break; }
        s += '{';
        if (indent >= 0) s += '\n';
        for (size_t i = 0; i < v.obj.size(); ++i) {
            AppendIndent(s, indent, depth + 1);
            AppendString(s, v.obj[i].first);
            s += indent >= 0 ? ": " : ":";
            AppendValue(s, v.obj[i].second, indent, depth + 1);
            if (i + 1 < v.obj.size()) s += ',';
            if (indent >= 0) s += '\n';
        }
        AppendIndent(s, indent, depth);
        s += '}';
        break;
    }
    }
}

} // namespace stringify_detail

inline std::string JsonStringify(const JsonValue& v, int indent) {
    std::string s;
    stringify_detail::AppendValue(s, v, indent, 0);
    return s;
}

} // namespace HybridEngine::Core::Assets