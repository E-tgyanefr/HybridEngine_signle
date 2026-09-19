#pragma once
#include "hybridengine/core/assets/asset_library.hpp"
#include "hybridengine/render3d/render3d.hpp"   // t2：MeshVisual（材质解析——软渲/GPU 共用）
#include <string>

// C：渲染类资产——.hmat 轻量材质（JSON：type=Material, {name, color, texture[, specular, shininess, emissive]}）
// M5：color 支持 "#RRGGBB" 与 "#RRGGBBAA"（alpha<FF=半透明——透明队列/混合）；specular/shininess=Blinn-Phong 高光；emissive=自发光
//   - 经 AssetLibrary 外部类型表注册：".hmat"→Material（幂等）
//   - 元数据视图（列表/图标/Inspector 资产页字段）+ 写入（SerializeMaterialAsset——
//     ms_assets_save_text/编辑器保存共用；与 parse 对称）
//   - MeshVisual 关联：materialAsset 字段→ResolveMeshVisualColor（渲染层解析材质色覆盖 baseColor——
//     软渲 Render3D 与 GPU GpuRenderer 双后端同一解析函数=同色契约）
// 容器示例：{ "hmat":"1", "name":"Red", "color":"#E05B5B", "texture":"Assets/Tex.bmp" }
namespace HybridEngine::Render3D {

struct MaterialAsset : HybridEngine::Core::Assets::AssetBase {
    static constexpr const char* kTypeName = "Material";
    static constexpr const char* kExt = "hmat";
    std::string name;
    std::string color;     // "#RRGGBB"（解析失败/缺省=白）
    std::string texture;   // 纹理资产路径（"Assets/..."；空=无）
    // M5：Blinn-Phong 材质参数（缺省=不写文件/legacy 观感；specular>0 才启用高光）
    double specular = 0.0;
    double shininess = 32.0;
    std::string emissive;  // "#RRGGBB" 自发光（空=无）
};

// 注册 .hmat→Material（幂等——render3d/editor 入口调用一次）
void RegisterMaterialAssetType();

// t2：材质色 "#RRGGBB" → Rgba（解析失败=白 255,255,255；与 .mstri material.color 同口径）
HybridEngine::Platform::Rgba ParseMaterialColor(const std::string& hex);
// M5："#RRGGBB" 自发光 → 线性 0..1（空/非法=黑）
HybridEngine::Core::Vec3 ParseMaterialEmissive(const std::string& hex);

// t2：序列化 MaterialAsset → hmat 容器 JSON 文本（与 parse 对称：{hmat:"1",name,color,texture}；
//     color/texture 空=省略——解析缺省=白/无；ms_assets_save_text / AssetLibrary::Save 可直接落盘）
std::string SerializeMaterialAsset(const MaterialAsset& m);

// t2：MeshVisual 有效颜色——materialAsset 非空且可解析 → 材质色覆盖（a=255 纯覆盖）；
//     否则=组件 color（既有语义）。db=资产库（nullptr=不解析——编辑/测试隔离）。
//     AssetLibrary 懒加载+引用计数（内部 Load→Unload 成对——每帧调用安全；缓存命中=零 I/O）
HybridEngine::Platform::Rgba ResolveMeshVisualColor(const MeshVisual& mr,
                                                      HybridEngine::Core::Assets::AssetLibrary* db);

// t7：MeshVisual 材质纹理——.hmat 的 texture 字段（"Assets/*.bmp|*.png"）→ TextureAsset 像素（借出）。
//     返回 true=有纹理（out=借出像素——调用方在渲染完成后必须 Unload outLoadedPath——引用计数配对）；
//     返回 false=无纹理（材质缺失/texture 空/非 Texture——纯色路径逐位不变）。
//     outLoadedPath 非空=本次 Load 的纹理路径（成对 Unload；缓存命中也可 Unload——计数配对）。
// M5：材质参数一体化解析（色/高光/自发光）——有 .hmat 时填充 out（含 color.a=半透明；返回 true），
//   无材质资产/解析失败=false（out=组件 color + 网格自带材质参数——既有语义不变）
// t-perf-resolve：outTexturePath 非空时同一次材质解析顺带取出 texture 路径——调用方不必为了纹理所
// 再 Load 一次 .hmat（Engine/ScenePanel 每网格每帧原先 3 次材质解析，现在 1 次）。
bool ResolveMeshVisualMaterial(const MeshVisual& mr, HybridEngine::Core::Assets::AssetLibrary* db,
                                 MaterialParams& out, std::string* outTexturePath = nullptr);
// 按已解析出的 texturePath 加载纹理资产（借出像素；调用方渲染后成对 Unload outLoadedPath）。
// 仅接受 .bmp/.png（与 ResolveMeshVisualTexture 同一防御口径）；失败=返回 false 且不增加引用计数。
bool LoadMeshTextureRef(HybridEngine::Core::Assets::AssetLibrary* db, const std::string& texturePath,
                        TextureRef& out, std::string& outLoadedPath);
bool ResolveMeshVisualTexture(const MeshVisual& mr, HybridEngine::Core::Assets::AssetLibrary* db,
                                TextureRef& out, std::string& outLoadedPath);

} // namespace HybridEngine::Render3D
