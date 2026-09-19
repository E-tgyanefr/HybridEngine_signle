#pragma once
#include "hybridengine/core/instance.hpp"
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <typeindex>

namespace HybridEngine::Core {

// M0：EventBus（Publish 入队、Flush 派发保序——防迭代中增删）
    struct IHolder { virtual ~IHolder() = default; };
    template<class E> struct Holder : IHolder { E value; explicit Holder(E v) : value(v) {} };

    class EventBus {
public:
    EventBus() { pending_.reserve(32); }
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    template<class E>
    void Subscribe(InstanceId id, std::function<void(const E&)> f) {
        subscribers_[std::type_index(typeid(E))][id] = [f](const IHolder& h) { f(static_cast<const Holder<E>&>(h).value); };
    }
    template<class E>
    void Publish(const E& e) {
        // M0：类型擦除拷贝（防悬垂——Publish 返回后 payload 存活）
        pending_.push_back({ std::type_index(typeid(E)), std::make_unique<Holder<E>>(e) });
    }
    void Flush();
    void Unsubscribe(InstanceId id) { unsubscribed_.push_back(id); }

private:
    std::unordered_map<std::type_index, std::unordered_map<InstanceId, std::function<void(const IHolder&)>>> subscribers_;
    using Pending = std::pair<std::type_index, std::unique_ptr<IHolder>>;
    std::vector<Pending> pending_;   // t-perf-event：unique_ptr（旧 shared_ptr 每次 Flush 还整表拷贝）
    std::vector<InstanceId> unsubscribed_;
};
} // namespace HybridEngine::Core
