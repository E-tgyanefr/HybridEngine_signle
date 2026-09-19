#pragma once
#include "hybridengine/core/instance.hpp"

namespace HybridEngine::Core {
class SceneObject;
class LifecycleDriver;

// M0：Component 基类（八回调——标准执行顺序;顺序由 LifecycleDriver 保证）
class Component {
public:
    explicit Component() = default;
    virtual ~Component() = default;
    virtual void Awake() {}
    virtual void OnEnable() {}
    virtual void Start() {}
    virtual void Update(double) {}
    virtual void FixedUpdate(double) {}
    virtual void LateUpdate(double) {}
    virtual void OnDisable() {}
    virtual void OnDestroy() {}

    // t4：序列化类型名虚拟化（纯增量）——非空=场景/预制体序列化用该名（脚本组件=per-instance 注册键）；
    // 默认 nullptr=走既存 RTTI 路径（ReflectionRegistry::TypeNameOf）。不改任何既有签名/语义。
    virtual const char* ScriptTypeName() const { return nullptr; }

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool v);
    SceneObject* Owner() const { return owner_; }
    InstanceId Id() const { return id_; }

protected:
    bool enabled_ = true;
    bool started_ = false;

private:
    InstanceId id_ = 0;
    SceneObject* owner_ = nullptr;
    friend class SceneObject;
    friend class LifecycleDriver;
};
} // namespace HybridEngine::Core
