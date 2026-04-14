#pragma once

#include "NamespacedId.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

// Runtime compact integer ID
using RuntimeId = uint16_t;
constexpr RuntimeId RUNTIME_ID_NULL = 0;

class IdRegistry {
public:
    // Register a NamespacedId, return assigned RuntimeId.
    // If already exists, return the existing RuntimeId.
    RuntimeId registerId(const NamespacedId& namespacedId);

    // Queries
    [[nodiscard]] RuntimeId getRuntimeId(const NamespacedId& namespacedId) const;
    [[nodiscard]] const NamespacedId& getNamespacedId(RuntimeId runtimeId) const;
    [[nodiscard]] bool contains(const NamespacedId& namespacedId) const;
    [[nodiscard]] size_t size() const;

    // Pre-register built-in block IDs (called during init, guarantees stable RuntimeId order)
    void initBuiltinBlockIds();

    // Pre-register built-in item IDs
    void initBuiltinItemIds();

private:
    std::unordered_map<NamespacedId, RuntimeId> m_toRuntime;
    std::vector<NamespacedId> m_toNamespaced;  // index = RuntimeId
};
