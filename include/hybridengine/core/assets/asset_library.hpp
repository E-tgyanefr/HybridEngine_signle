#pragma once
#include "hybridengine/core/assets/reflection_registry.hpp"
#include "hybridengine/core/scene.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HybridEngine::Core::Assets {

// M2：资产域基础（错误码/类型表/AssetLibrary——懒加载+引用计数）
enum class AssetErrorCode : uint32_t {
    Ok = 0,
    NotFound,          // 文件/资产不存在
    InvalidFormat,     // 容器/JSON/二进制布局错误
    HashMismatch,      // 校验 FNV 不匹配（篡改/损坏）
    TraversalDenied,   // 路径 ../ 穿越拒绝
    TypeUnsupported,   // 类型不支持（如二进制 M2-a 骨架）
    TypeMismatch,      // 资产类型与请求 Load<T> 不符
    CyclicParent,      // 场景 parent 引用成环
    UnknownParent,     // parent 引用不存在
    DuplicateName,     // 对象名重复（父引用解析约定唯一）
    UnknownComponentType,  // 组件类型未注册
    UnknownField,      // 字段未注册/未知
    IoError,           // 读写失败
    NotSupported,      // 功能未支持（M2-a 二进制骨架）
};

struct AssetResult {
    bool ok = false;
    AssetErrorCode code = AssetErrorCode::Ok;
    std::string message;
    static AssetResult Success() { return {true, AssetErrorCode::Ok, ""}; }
    static AssetResult Fail(AssetErrorCode c, std::string m) { return {false, c, std::move(m)}; }
};

struct AssetId { std::string guid; std::string path; };
struct AssetMeta { std::string guid; std::string type; std::string importJson; };

// —— 资产类型（Load<T> 缓存实体）——
class AssetBase {
public:
    virtual ~AssetBase() = default;
};

struct SceneDocument : AssetBase {
    static constexpr const char* kTypeName = "Scene";
    static constexpr const char* kExt = "mscene";
    std::string guid;
    std::string name;
    std::unique_ptr<Scene> scene;
};

struct PrefabTemplate : AssetBase {
    static constexpr const char* kTypeName = "Prefab";
    static constexpr const char* kExt = "msprefab";
    std::string guid;
    std::string name;
    std::string json;   // 模板 JSON 载荷（实例化=重解析）
};

struct TextureAsset : AssetBase {
    static constexpr const char* kTypeName = "Texture";
    static constexpr const char* kExt = "bmp";
    int width = 0, height = 0, stride = 0;
    std::vector<uint32_t> pixels;   // 0xFFRRGGBB（与软渲同格式）
};

struct AudioAsset : AssetBase {
    static constexpr const char* kTypeName = "Audio";
    static constexpr const char* kExt = "wav";
    std::string absolutePath;   // WinMM 打开用（不做解码）
};

struct BlobAsset : AssetBase {
    static constexpr const char* kTypeName = "Blob";
    static constexpr const char* kExt = "blob";
    std::string type;       // 原始 type 字符串（自定义类型直通——G9 修复）
    std::vector<uint8_t> bytes;
};

struct MetaAsset : AssetBase {
    static constexpr const char* kTypeName = "Meta";
    static constexpr const char* kExt = "msmeta";
    AssetMeta meta;
};

// —— BMP 解码（BI_RGB 24/32bpp——M2-c 仅 BMP；PNG 后置）——
AssetResult DecodeBmp(const std::vector<uint8_t>& data, int& w, int& h, int& stride, std::vector<uint32_t>& pixels);

// —— PNG 解码（t7：受限子集=8bit 非隔行 RGB/RGBA；zlib inflate 自实现+CRC/adler32 严格校验；零第三方）——
AssetResult DecodePng(const std::vector<uint8_t>& data, int& w, int& h, int& stride, std::vector<uint32_t>& pixels);

// —— AssetLibrary ——
class AssetLibrary {
public:
    static AssetLibrary& Instance();

    bool SetProjectRoot(const std::string& abs);
    const std::string& ProjectRoot() const { return root_; }

    AssetResult Resolve(const std::string& assetPath, std::string& abs) const;   // ../ 拒绝
    bool Exists(const std::string& assetPath) const;
    AssetId GetMeta(const std::string& assetPath) const;   // 无 meta=按内容 FNV 生成（懒——M2-b）
    std::vector<std::string> List(const std::string& dir = "Assets/") const;   // 递归（类型表过滤）

    template<class TAsset> TAsset* Load(const std::string& assetPath);   // 按需+缓存+引用计数
    void Unload(const std::string& assetPath);
    int RefCount(const std::string& assetPath) const;   // 诊断（计数断言）

    AssetResult Save(const std::string& assetPath, const void* data, size_t bytes, const char* type);
    AssetResult Import(const std::string& srcAbs, const std::string& dest, const char* type);
    // t8 增量（审计 §10.4 红线放松——新增 API）：删除虚拟资产（实体+msmeta+缓存清退；文件系统级=非撤销域）
    AssetResult Delete(const std::string& assetPath);

    // Prefab（骨架：模板 JSON 往返；实例化=重解析——M3 编辑器完整挂接）
    SceneObject* InstantiatePrefab(const std::string& assetPath, Scene* targetScene, const Vec3& at = {});

    static const char* ExtToType(const std::string& ext);   // ".mscene"→"Scene"...（类型表）
    AssetBase* LoadGeneric(const std::string& assetPath);   // M2.5 绑定层：按扩展名泛型 Load（缓存/引用计数语义同 Load<T>）

    // M3D：模块扩展类型注册（render3d 注册 Mesh/.mstri——core 不依赖 render3d）
    // fn 解析文件字节→AssetBase*（失败=nullptr）；与内建类型表同语义（带缓存/引用计数）
    using AssetTypeFactory = AssetBase* (*)(const std::vector<uint8_t>& bytes);
    static bool RegisterExternalType(const char* ext, const char* typeName, AssetTypeFactory fn);
    static AssetTypeFactory ExternalFactory(const char* typeName);   // 注册表查询（LoadInternal 用）
    static AssetTypeFactory ExternalFactoryByExt(const char* ext);    // 按扩展名查询（多 ext 同 type 用——如 .obj/.mstri）

private:
    // t-perf-asset：保留缓存——refs 归零后不立刻 delete，而是按 LRU/字节上限保留一小段时间。
    // 每帧 ResolveMeshVisual* 会 Load→Unload；旧实现每帧重新读文件+解码纹理（1024² BMP 实测 ~5ms/帧），
    // 保留缓存把重复解码降为 map 命中+文件新鲜度校验；abs/mtime/size 任一不一致=失效重载。
    struct Entry {
        AssetBase* ptr = nullptr;
        int refs = 0;
        std::string type;
        std::string abs;          // 首次加载时的绝对路径（项目根切换防串档）
        std::string root;         // 首次加载时的 AssetLibrary root（不一致才 Resolve 复核）
        int64_t mtime = 0;        // 文件最后写入时间（外部改动→失效重载）
        uint64_t fileBytes = 0;   // 文件字节数（mtime 同精度兜底）
        bool retired = false;     // refs==0 的保留缓存条目
        uint64_t retiredStamp = 0;
    };
    AssetBase* LoadInternal(const std::string& assetPath, const char* typeName, const char* ext);
    AssetBase* AcquireCached(const std::string& assetPath, const char* typeName);   // 活跃命中/保留条目复用（含新鲜度校验）
    void StoreEntry(const std::string& key, AssetBase* ptr, const char* typeName, const std::string& abs);   // 新加载入缓存
    void RetireEntry(const std::string& key, Entry& e);        // refs 归零→有界保留（避免每帧重解码）
    void DropRetired(const std::string& key);                  // 保留条目失效/淘汰（delete+计数清理）
    void InvalidateCached(const std::string& key);             // Save/Delete 后清旧缓存
    std::string root_;
    std::unordered_map<std::string, Entry> cache_;   // key=虚拟资产路径
    std::unordered_map<std::string, uint64_t> retiredIndex_;   // 保留条目 LRU 索引（避免全表扫描）
    uint64_t retiredClock_ = 0;
    uint64_t retiredBytes_ = 0;
    static constexpr size_t kRetainMaxCount = 64;                        // 保留条目数上限
    static constexpr uint64_t kRetainMaxBytes = 128ull * 1024 * 1024;    // 保留载荷字节上限
};

// —— 模板 Load（类型分发 + 引用计数）——
template<class TAsset>
TAsset* AssetLibrary::Load(const std::string& assetPath) {
    AssetBase* p = LoadInternal(assetPath, TAsset::kTypeName, TAsset::kExt);
    return static_cast<TAsset*>(p);
}

} // namespace HybridEngine::Core::Assets