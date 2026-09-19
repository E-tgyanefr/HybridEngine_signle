#pragma once
#include <string>
#include <vector>

namespace HybridEngine::Core {
class SceneObject;
class Component;
class Scene;

// M0：LifecycleDriver（标准顺序分派——SequenceLog=组件回调轨迹，组件自行记录）
class LifecycleDriver {
public:
    void Register(Component* c, SceneObject* owner);
    void Unregister(Component* c);
    void Tick(double dt);
    void SetActive(Component* c, bool v);
    void PushDestroy(SceneObject* go);
    // t-perf-log：轨迹有界化——SequenceLog 是测试/诊断用的回调轨迹，不应在长跑中无界增长
    // （旧实现=每帧每组件 push，永不清理；100 组件 3000 帧=90 万条/27.5MB）。窗口满则清空重记，
    // 保留最近事件；测试日志远小于窗口 -> 语义不变。宿主可 SetSequenceLogEnabled(false) 关闭=零开销。
    void SetSequenceLogEnabled(bool on) { sequenceLogEnabled_ = on; if (!on) sequenceLog_.clear(); }
    bool SequenceLogEnabled() const { return sequenceLogEnabled_; }
    // P1-b：场景回指——帧尾 destroy 队列需要真正把对象从场景移除（Scene::RemoveRoot）；
    // 无回指（游离 driver）=仅回调、不迁移归属。由 Scene 构造时注入。
    void SetScene(Scene* s) { scene_ = s; }
    Scene* OwnerScene() const { return scene_; }
    const std::vector<std::string>& SequenceLog() const { return sequenceLog_; }
    std::vector<std::string>& SequenceLog() { return sequenceLog_; }

private:
    friend class Scene;   // Scene::RemoveRoot 同步写轨迹（Trace 私有实现细节）
    std::vector<Component*> registry_;
    std::vector<SceneObject*> destroyQueue_;
    void Trace(const char* event);   // 统一入口：有界窗口 + 可关闭（见 SetSequenceLogEnabled）
    std::vector<std::string> sequenceLog_;
    bool sequenceLogEnabled_ = true;
    static constexpr size_t kSequenceLogCap = 4096;   // 诊断窗口（满则清空重记——长跑有界）
    Scene* scene_ = nullptr;
    double fixedAccum_ = 0;   // R1：FixedUpdate 固定步长累加器
    double fixedStep_ = 1.0 / 60.0;
    int fixedCount_ = 0;
};
} // namespace HybridEngine::Core
