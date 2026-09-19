#pragma once
#include <cstdint>

namespace HybridEngine::Core {
using InstanceId = std::uint64_t;

class InstanceIds {
public:
    static InstanceId Next() { return s_next++; }
private:
    static inline InstanceId s_next = 1;
};
} // namespace HybridEngine::Core
