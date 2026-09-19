#pragma once
#include "hybridengine/core/scene_object.hpp"
#include "hybridengine/core/lifecycle_driver.hpp"
#include <vector>
#include <memory>

namespace HybridEngine::Core {

// M0：Scene（AddRoot/Roots/LifecycleDriver）
class Scene {
public:
    Scene();
    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }
    SceneObject* AddRoot(const std::string& name);
    // t-perf-scene：渲染可见性修订号——只在「根节点/组件集合」变化时递增（组件字段/Transform 变化不算）。
    // SceneHasRenderables 的结果可按此缓存（每帧省一次全树 DFS）；任何可能的可渲染物增删都必须 Bump。
    uint64_t RenderRevision() const { return renderRevision_; }
    void BumpRenderRevision() { ++renderRevision_; }
    // t8 增量（审计 §10.4 红线放松——新增 API，不改既有签名/语义）：
    // 编辑态按对象删除（含子树——roots_ 为平铺存储；沿 Transform 子链递归收集后统一 erase）
    void RemoveRoot(SceneObject* go);
    const std::vector<std::unique_ptr<SceneObject>>& Roots() const { return roots_; }
    LifecycleDriver& Lifecycle() { return *lifecycle_; }

    // 场景被 move 进最终归属后必须调用本函数，重绑**两处回指**：
    //   ① LifecycleDriver 的场景回指（否则帧尾销毁队列会去操作一个已析构的 Scene）
    //   ② 全部 SceneObject 的 scene_ 回指（roots_ 为平铺存储——含子树节点，一次遍历即全覆盖）
    // ②此前一直漏掉：反序列化产物 move 进引擎后，对象的 scene_ 仍指向**临时 Scene**（随即析构），
    // 之后任何 AddChild / SetActive / Destroy 都会解引用已释放的 Scene。
    // 实测症状：unique_ptr<LifecycleDriver>::operator* 断言（临时 Scene 的 lifecycle_ 已被 move 走）。
    void RebindAfterMove() {
        lifecycle_->SetScene(this);
        for (auto& r : roots_) if (r) r->scene_ = this;   // Scene 是 SceneObject 的 friend
        ++renderRevision_;                                 // 内容已换（渲染可见性缓存必须失效）
    }

private:
    std::string name_ = "Scene";
    uint64_t renderRevision_ = 1;
    std::vector<std::unique_ptr<SceneObject>> roots_;
    std::unique_ptr<LifecycleDriver> lifecycle_;
    friend class SceneObject;
};
} // namespace HybridEngine::Core

// t110：SceneObject 模板定义（LifecycleDriver 完整类型后——无循环）
namespace HybridEngine::Core {
template<class TComponent>
TComponent* SceneObject::AddComponent() {
    auto c = std::make_unique<TComponent>();
    TComponent* raw = c.get();
    components_.push_back(std::move(c));
    if (scene_) {
        scene_->BumpRenderRevision();   // t-perf-scene：新增可能可渲染组件 -> 缓存失效
        scene_->Lifecycle().Register(raw, this);
    }
    return raw;
}

template<class TComponent>
TComponent* SceneObject::GetComponent() const {
    for (const auto& c : components_)
        if (dynamic_cast<TComponent*>(c.get())) return static_cast<TComponent*>(c.get());
    return nullptr;
}
} // namespace HybridEngine::Core