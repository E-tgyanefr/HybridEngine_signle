#pragma once
#include "hybridengine/core/instance.hpp"
#include "hybridengine/core/math.hpp"   // t2：AddChild 默认参数（Vec3/Quat 完整类型）
#include <string>
#include <vector>
#include <memory>

namespace HybridEngine::Core {
class Scene;
class LifecycleDriver;
class Component;
class Transform;

// M0：SceneObject（AddComponent/GetComponent/SetActive/Destroy 延迟/Transform 指针——不完整类型安全）
class SceneObject {
public:
    explicit SceneObject(std::string name);
    ~SceneObject();
    const std::string& Name() const { return name_; }
    ::HybridEngine::Core::InstanceId InstanceId() const { return id_; }   // M2.5 绑定层 ms_id（长期；全限定——成员名与类型同名遮蔽）
    void SetName(const std::string& n) { name_ = n; }

    // R3（M1.1）：GetTransform() 主签名（组件查询语义——成员命名与类型名不遮蔽）
    // 注：成员名与类型同名会遮蔽——全部用全限定 ::HybridEngine::Core::Transform（t111 教训）
    ::HybridEngine::Core::Transform* GetTransform() { return transform_.get(); }
    const ::HybridEngine::Core::Transform* GetTransform() const { return transform_.get(); }
    // 兼容别名（docs 迁移期）：deprecated——M1 后由 GetTransform() 取代
    [[deprecated("use GetTransform() instead")]]
    ::HybridEngine::Core::Transform* Transform() { return transform_.get(); }
    [[deprecated("use GetTransform() instead")]]
    const ::HybridEngine::Core::Transform* Transform() const { return transform_.get(); }

    template<class TComponent>
    TComponent* AddComponent();

    template<class TComponent>
    TComponent* GetComponent() const;

    // M2：运行时组件接入（反射工厂产物——反序列化用）
    Component* AddComponentRaw(Component* c);
    // t8 增量（审计 §10.4 红线放松——新增 API）：编辑态移除组件（LifecycleDriver::Unregister + OnDisable/OnDestroy 直调）
    void RemoveComponent(Component* c);
    const std::vector<std::unique_ptr<Component>>& Components() const { return components_; }

    void SetActive(bool v);
    bool ActiveInHierarchy() const { return activeSelf_; }
    void Destroy();

    // 多场景：该对象所属场景（未挂场景=nullptr）。绑定层需要它来「在该对象所在的场景里查/建」——
    // 此前若干接口误用引擎活动场景，单场景下不可见，多场景会作用到错误的场景。
    Scene* GetScene() const { return scene_; }

    // t2（API 直觉化）：便捷子对象构造（= Scene::AddRoot + SetParent + TRS 一行式——与 ms_go_add_child 同路径）
    // 默认参数=纯增量（不新增同名旧 API）；仅已挂接 Scene 的对象可用（scene_ 为空→nullptr 并返回）
    SceneObject* AddChild(const std::string& name, const Vec3& pos = {}, const Quat& rot = Quat::Identity(), const Vec3& scale = {1, 1, 1});

private:
    std::string name_;
    ::HybridEngine::Core::InstanceId id_ = 0;
    bool activeSelf_ = true;
    Scene* scene_ = nullptr;
    std::vector<std::unique_ptr<Component>> components_;
    // t111：成员函数 Transform() 遮蔽类名——成员类型用全限定 ::HybridEngine::Core::Transform
    std::unique_ptr<::HybridEngine::Core::Transform> transform_;

    friend class Scene;
    friend class LifecycleDriver;
    friend class Component;   // P1-a：Component::SetEnabled 经 scene_->Lifecycle().SetActive（不新增公开面）
};
} // namespace HybridEngine::Core