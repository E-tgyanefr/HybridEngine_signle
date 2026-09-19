#pragma once
#include "hybridengine/core/assets/msjson.hpp"
#include "hybridengine/core/component.hpp"
#include "hybridengine/core/math.hpp"
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace HybridEngine::Core {

// M2：ReflectionRegistry（序列化+Inspector 共用——HYBRIDENGINE_REFLECT 类尾宏注册字段元数据）
enum class FieldKind { Int, Float, Bool, String, Enum, Vec3, Struct };

struct ReflectField {
    const char* name = nullptr;
    FieldKind kind = FieldKind::Struct;
    size_t offset = 0;
    const char* structType = nullptr;          // Struct 类型名（若已注册）
    const std::type_info* structInfo = nullptr; // Struct 递归查表用
};

// 字段类型→Kind 推导（int/double/bool/string/Vec3/枚举——其余=Struct 待注册）
template<class F> struct ReflectKindOf {
    static constexpr FieldKind kKind = std::is_enum_v<F> ? FieldKind::Enum : FieldKind::Struct;
    static const char* kStructType() { return nullptr; }
    static const std::type_info* kInfo() { return std::is_enum_v<F> ? nullptr : &typeid(F); }
};
template<> struct ReflectKindOf<int> { static constexpr FieldKind kKind = FieldKind::Int; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };
template<> struct ReflectKindOf<float> { static constexpr FieldKind kKind = FieldKind::Float; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };
template<> struct ReflectKindOf<double> { static constexpr FieldKind kKind = FieldKind::Float; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };
template<> struct ReflectKindOf<bool> { static constexpr FieldKind kKind = FieldKind::Bool; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };
template<> struct ReflectKindOf<std::string> { static constexpr FieldKind kKind = FieldKind::String; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };
template<> struct ReflectKindOf<Vec3> { static constexpr FieldKind kKind = FieldKind::Vec3; static const char* kStructType() { return nullptr; } static const std::type_info* kInfo() { return nullptr; } };

struct ReflectDescriptor {
    const char* typeName = nullptr;
    const std::type_info* info = nullptr;
    std::vector<ReflectField> fields;
    void* (*create)() = nullptr;   // 组件工厂（is_base_of Component 注册时）
};

template<class T> struct ReflectTag {};   // ADL 锚点（反射描述生成函数经 ADL 查找）

// —— 宏：类尾一行注册反射字段（1..12 字段；friend+inline 描述生成——无需外部特化）——
// 字段计数（NARG=字段数 K≥1；空参=编译错误——组件字段≥1）
#define MS_MS_NARG(...) MS_MS_NARG_(__VA_ARGS__, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define MS_MS_NARG_(...) MS_MS_NARG__(__VA_ARGS__)
#define MS_MS_NARG__(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,N,...) N

#define MS_MS_FIELD_ENTRY(type, field) \
    ::HybridEngine::Core::ReflectField{ #field, ::HybridEngine::Core::ReflectKindOf<decltype(type::field)>::kKind, \
        (size_t)((const char*)&((type*)0)->field - (const char*)0), \
        ::HybridEngine::Core::ReflectKindOf<decltype(type::field)>::kStructType(), \
        ::HybridEngine::Core::ReflectKindOf<decltype(type::field)>::kInfo() },

#define MS_MS_FIELDS_1(type, a) { MS_MS_FIELD_ENTRY(type, a) }
#define MS_MS_FIELDS_2(type, a, b) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) }
#define MS_MS_FIELDS_3(type, a, b, c) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) }
#define MS_MS_FIELDS_4(type, a, b, c, d) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) }
#define MS_MS_FIELDS_5(type, a, b, c, d, e) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) }
#define MS_MS_FIELDS_6(type, a, b, c, d, e, f) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) }
#define MS_MS_FIELDS_7(type, a, b, c, d, e, f, g) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) }
#define MS_MS_FIELDS_8(type, a, b, c, d, e, f, g, h) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) MS_MS_FIELD_ENTRY(type, h) }
#define MS_MS_FIELDS_9(type, a, b, c, d, e, f, g, h, i) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) MS_MS_FIELD_ENTRY(type, h) MS_MS_FIELD_ENTRY(type, i) }
#define MS_MS_FIELDS_10(type, a, b, c, d, e, f, g, h, i, j) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) MS_MS_FIELD_ENTRY(type, h) MS_MS_FIELD_ENTRY(type, i) MS_MS_FIELD_ENTRY(type, j) }
#define MS_MS_FIELDS_11(type, a, b, c, d, e, f, g, h, i, j, k) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) MS_MS_FIELD_ENTRY(type, h) MS_MS_FIELD_ENTRY(type, i) MS_MS_FIELD_ENTRY(type, j) MS_MS_FIELD_ENTRY(type, k) }
#define MS_MS_FIELDS_12(type, a, b, c, d, e, f, g, h, i, j, k, l) { MS_MS_FIELD_ENTRY(type, a) MS_MS_FIELD_ENTRY(type, b) MS_MS_FIELD_ENTRY(type, c) MS_MS_FIELD_ENTRY(type, d) MS_MS_FIELD_ENTRY(type, e) MS_MS_FIELD_ENTRY(type, f) MS_MS_FIELD_ENTRY(type, g) MS_MS_FIELD_ENTRY(type, h) MS_MS_FIELD_ENTRY(type, i) MS_MS_FIELD_ENTRY(type, j) MS_MS_FIELD_ENTRY(type, k) MS_MS_FIELD_ENTRY(type, l) }

#define MS_MS_CAT_(a, b) a##b
#define MS_MS_CAT(a, b) MS_MS_CAT_(a, b)
#define MS_MS_FIELDS_GET(type, ...) MS_MS_CAT(MS_MS_FIELDS_, MS_MS_NARG(__VA_ARGS__))(type, __VA_ARGS__)

#define HYBRIDENGINE_REFLECT(type, ...) \
    friend inline ::HybridEngine::Core::ReflectDescriptor msMsReflectDesc(::HybridEngine::Core::ReflectTag<type>) { \
        ::HybridEngine::Core::ReflectDescriptor d; \
        d.typeName = #type; \
        d.info = &typeid(type); \
        d.fields = MS_MS_FIELDS_GET(type, __VA_ARGS__); \
        return d; \
    }


// M2：反射注册表（Register<T> + 字段查表 + Get/Set（offset 定位）+ 组件工厂 + TypeNameOf）
class ReflectionRegistry {
public:
    template<class T> static void Register(const char* typeName);
    // M3.4：无工厂描述注册（运行时类型名——BindComponent 脚本组件：序列化认领，反序列化经 C# 桥重放=P1）
    static void RegisterRaw(const char* typeName, const std::type_info& ti);
    static const std::vector<ReflectField>& Fields(const char* typeName);
    static bool GetField(const char* typeName, void* obj, const char* field, ::HybridEngine::Core::Assets::JsonValue& out);
    static bool SetField(const char* typeName, void* obj, const char* field, const ::HybridEngine::Core::Assets::JsonValue& in);
    static const char* TypeNameOf(const std::type_info& ti);
    static void* Create(const char* typeName);        // 组件工厂（无=nullptr）
    static bool HasFactory(const char* typeName);
    // t8 增量（审计 §10.4 红线放松）：全部已注册类型名
    static std::vector<std::string> RegisteredNames();
    // 嵌套 struct 递归（已注册类型）
    static bool GetFieldByInfo(const std::type_info& ti, void* base, const char* field, ::HybridEngine::Core::Assets::JsonValue& out);
    static const ReflectDescriptor* FindByInfo(const std::type_info& ti);   // 嵌套递归查表（公开供字段读写）

private:
    struct Entry { ReflectDescriptor desc; };
    // t-perf-refl：透明哈希——Find(typeName) 不再为每次查表构造 std::string；type_index 索引替代线性扫表。
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
        size_t operator()(const std::string& s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    struct StringEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };
    using TableMap = std::unordered_map<std::string, ReflectDescriptor, StringHash, StringEq>;
    static TableMap& Table();
    static std::unordered_map<std::type_index, std::string>& TypeIndex();
    static const ReflectDescriptor* Find(const char* typeName);
};

// —— 实现（inline：Register 模板+宏配合——头文件即发布）——
template<class T>
void ReflectionRegistry::Register(const char* typeName) {
    ReflectDescriptor d = msMsReflectDesc(ReflectTag<T>());   // ADL 找到类尾 friend
    if constexpr (std::is_base_of_v<Component, T>) {
        d.create = []() -> void* { return static_cast<void*>(new T()); };
    }
    Table()[std::string(typeName)] = std::move(d);
    TypeIndex()[std::type_index(typeid(T))] = typeName;   // 反向索引（FindByInfo/TypeNameOf 不再扫表）
}

} // namespace HybridEngine::Core