#pragma once
#include "hybridengine/core/assets/asset_library.hpp"
#include "hybridengine/render3d/render3d.hpp"

// M3D：.mstri MeshAsset（JSON 文本资产——设计 §2；AssetLibrary 类型表扩展=RegisterMeshAssetType）
// 容器=纯 JSON（{ "mstri":"1", "name":"Cube", "material":{"color":"#8A5CF6"}, "triangles":[{"a":[x,y,z],"b":..,"c":..,"uv":[[u0,v0],[u1,v1],[u2,v2]]}] }）
// t7：triangles 顶点可附带 uv（3 个 [u,v] 对或扁平 6 数字[顶点0 u/v, 顶点1 u/v, 顶点2 u/v]——缺省=0,0；与 OBJ 同供纹理采样）
// 法线缺省=ComputeNormals（面法线——设计：正常数缺省计算）
namespace HybridEngine::Render3D {

struct MeshAsset : HybridEngine::Core::Assets::AssetBase {
    static constexpr const char* kTypeName = "Mesh";
    static constexpr const char* kExt = "mstri";
    std::string guid;
    std::string name;
    MeshData mesh;
};

// 注册 .mstri→Mesh（幂等；进程内一次即可——绑定层/编辑器入口调用）
void RegisterMeshAssetType();
// m-obj：OBJ 模型识别（.obj→"Mesh"——标准 v/f 子集、多边形扇三角化）
void RegisterObjAssetType();


} // namespace HybridEngine::Render3D
