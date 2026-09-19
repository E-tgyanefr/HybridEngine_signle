#pragma once
#include "hybridengine/core/math.hpp"
#include <cstdint>
#include <vector>

namespace HybridEngine::Core {
class SceneObject;

// M0/M1：Transform（层级+矩阵，双精度——标准语义）
// M1.3 lazy-update：Set* 标脏（自身+子树）；WorldMatrix() 查时若脏→重算（先父后己），查询不重算（缓存）。
class Transform {
public:
    Vec3 Position() const { return position_; }
    void SetPosition(const Vec3& p) { position_ = p; MarkDirty(); }
    Quat Rotation() const { return rotation_; }
    void SetRotation(const Quat& q) { rotation_ = q; MarkDirty(); }
    Vec3 Scale() const { return scale_; }
    void SetScale(const Vec3& s) { scale_ = s; MarkDirty(); }
    Transform* Parent() const { return parent_; }
    SceneObject* Owner() const { return owner_; }   // 反向句柄（序列化 DFS 用）
    const std::vector<Transform*>& Children() const { return children_; }
    void SetParent(Transform* parent, bool keepWorld = false);
    Mat4 LocalMatrix() const { return Mat4::RT(position_, rotation_, scale_); }
    // 世界矩阵（lazy：脏→重算并缓存；干净→直接返回缓存）
    Mat4 WorldMatrix() const;
    Vec3 Forward() const { return Mat4::RT(Vec3(), rotation_, Vec3(1,1,1)).TransformDir(Vec3(0,0,1)); }
    Vec3 Right() const { return Mat4::RT(Vec3(), rotation_, Vec3(1,1,1)).TransformDir(Vec3(1,0,0)); }
    Vec3 Up() const { return Mat4::RT(Vec3(), rotation_, Vec3(1,1,1)).TransformDir(Vec3(0,1,0)); }
    // M1.3 诊断：世界矩阵重算次数（lazy-update 断言——查询不重算）
    uint64_t WorldRecomputeCount() const { return worldRecomputeCount_; }

    // t2（API 直觉化）：世界空间便捷——统一走 WorldMatrix 缓存（lazy-update 语义不变；均不改变既有 API）
    Vec3 GetWorldPosition() const;                                  // = WorldMatrix 平移分量（查询不重算）
    void SetWorldPosition(const Vec3& worldPos);                    // 父逆 RT 换算 local（列分解——旋转+缩放正确）
    Vec3 GetWorldScale() const;                                     // 世界矩阵列长（旋转+缩放；非均匀缩放+旋转链=近似）
    void SetWorldRotation(const Quat& worldRot);                    // 父旋转逆×目标（Quat 无逆/乘——经 Mat4 路径；local 四元数）

private:
    void MarkDirty();          // 标脏自身+子树（世界矩阵失效传播）
    Vec3 position_;
    Quat rotation_;
    Vec3 scale_{1, 1, 1};
    Transform* parent_ = nullptr;
    SceneObject* owner_ = nullptr;   // 由 SceneObject ctor 设（friend）
    std::vector<Transform*> children_;
    mutable bool worldDirty_ = true;      // 首次查询即重算
    mutable Mat4 worldCache_{};
    mutable uint64_t worldRecomputeCount_ = 0;
    friend class SceneObject;
};
} // namespace HybridEngine::Core