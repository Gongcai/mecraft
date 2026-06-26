#include "IdRegistry.h"

RuntimeId IdRegistry::registerId(const NamespacedId& namespacedId) {
    auto it = m_toRuntime.find(namespacedId);
    if (it != m_toRuntime.end()) {
        return it->second;
    }

    RuntimeId id = static_cast<RuntimeId>(m_toNamespaced.size());
    m_toNamespaced.push_back(namespacedId);
    m_toRuntime[namespacedId] = id;
    return id;
}

RuntimeId IdRegistry::getRuntimeId(const NamespacedId& namespacedId) const {
    auto it = m_toRuntime.find(namespacedId);
    if (it != m_toRuntime.end()) {
        return it->second;
    }
    return RUNTIME_ID_NULL;
}

const NamespacedId& IdRegistry::getNamespacedId(const RuntimeId runtimeId) const {
    if (runtimeId < m_toNamespaced.size()) {
        return m_toNamespaced[runtimeId];
    }
    static const NamespacedId unknown("minecraft", "unknown");
    return unknown;
}

bool IdRegistry::contains(const NamespacedId& namespacedId) const {
    return m_toRuntime.find(namespacedId) != m_toRuntime.end();
}

size_t IdRegistry::size() const {
    return m_toNamespaced.size();
}
