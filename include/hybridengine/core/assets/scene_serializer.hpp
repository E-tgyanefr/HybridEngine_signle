#pragma once
#include "hybridengine/core/assets/asset_library.hpp"
#include "hybridengine/core/component.hpp"
#include <functional>
#include <string>
#include <vector>

namespace HybridEngine::Core::Assets {

// M2：场景序列化——.mscene=.msasset 文本容器（MSASSET1/type/guid/name + JSON payload + fnv 尾行）
// 载荷 schema（§2.1——平铺+父引用；内建 Transform 由 transform 块承载；rot=四元数 [w,x,y,z]（忠实往返））
// { "mscene":"1","guid":"...","name":"...",
//   "roots":[ {"name":"..","active":true,"parent":null|"父名",
//              "transform":{"pos":[x,y,z],"rot":[w,x,y,z],"scale":[x,y,z]},
//              "components":[{"type":"全限定名","fields":{"f":值,...}}]} ] }

// —— P1-a：脚本组件重放（场景加载）——
// 脚本组件在场景里以「保存时的 per-instance 注册键」(FullName#N) 作为 type 落盘，
// 而 ReflectionRegistry 对它没有工厂（RegisterRaw 只登记描述）——因此反序列化需要一条外部工厂通道。
// 组件工厂钩子：按类型名创建组件实例；返回 nullptr=未认领（调用方此前已试过 ReflectionRegistry::Create，
//   两者都失败才报 UnknownComponentType）。
using ComponentFactoryHook = std::function<Component*(const std::string& typeName)>;

// 经工厂钩子创建出来的脚本组件分录（调用方据此补挂托管实例 + 回填 scriptFields）
struct ReplayedScriptComponent {
    Component* comp = nullptr;
    std::string typeKey;      // 保存时的 per-instance 注册键（= 场景里 components[].type）
    std::string fieldsJson;   // scriptFields 原文（""=该条目无脚本字段）
};

// 反序列化上下文——纯增量；nullptr = 既有行为（无工厂、不收集脚本分录）
struct DeserializeContext {
    ComponentFactoryHook factory;
    std::vector<ReplayedScriptComponent>* outScripts = nullptr;
};

// Scene→容器文本（type 行=Scene；guid 空则按 name+内容 FNV 生成——M2-b 确定性）
AssetResult SerializeSceneToContainer(const Scene& scene, const std::string& guidOrEmpty, std::string& outText);

// 容器文本→Scene（FNV 校验/环检测/字段反射——错误码+日志）
// ctx≠null：脚本组件条目经 ctx->factory 创建，并记入 ctx->outScripts（供托管侧重放）
AssetResult DeserializeSceneFromContainer(const std::string& containerText, std::unique_ptr<Scene>& outScene,
                                           std::string& outGuid, std::string& outName,
                                           const DeserializeContext* ctx = nullptr);

// Prefab 骨架：模板（单根对象条目）JSON 往返
AssetResult SerializePrefabToContainer(const SceneObject& root, const std::string& guidOrEmpty, std::string& outText);
AssetResult DeserializePrefabFromContainer(const std::string& containerText, std::string& outGuid,
                                            std::string& outName, std::string& outTemplateJson);

// —— 内部：对象条目应用（场景/预制体共用——parent 已解析）——
SceneObject* ApplyObjectEntry(Scene& scene, const JsonValue& entry, SceneObject* parent, const Vec3& offset,
                              AssetResult& out, const DeserializeContext* ctx = nullptr);
// 平铺子树应用（roots 数组+父名引用+环检测——场景/预制体实例化共用；M3.4）
AssetResult ApplyObjectTreeEntries(Scene& scene, const JsonValue& roots, const Vec3& offset, HybridEngine::Core::SceneObject*& outFirst);   // outFirst=模板根（首个条目）

} // namespace HybridEngine::Core::Assets