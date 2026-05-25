#include "IdRegistry.h"
#include "../../game/content/BuiltinIds.h"

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

void IdRegistry::initBuiltinBlockIds() {
    // Register built-in block IDs in stable order.
    // RuntimeId assignment order must remain fixed for data compatibility.
#define MECRAFT_REGISTER_BUILTIN_BLOCK(symbol, path) registerId(NamespacedId("minecraft", path));
    MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_REGISTER_BUILTIN_BLOCK)
#undef MECRAFT_REGISTER_BUILTIN_BLOCK
}

void IdRegistry::initBuiltinItemIds() {
    // Items use their own IdRegistry, starting from 0.
    // Block-backed items keep the same built-in prefix as blocks.
#define MECRAFT_REGISTER_BUILTIN_BLOCK_ITEM(symbol, path) registerId(NamespacedId("minecraft", path));
    MECRAFT_FOR_EACH_BUILTIN_BLOCK(MECRAFT_REGISTER_BUILTIN_BLOCK_ITEM)
#undef MECRAFT_REGISTER_BUILTIN_BLOCK_ITEM

    // Pure items keep their own stable IDs after the block-backed range.
#define MECRAFT_REGISTER_BUILTIN_PURE_ITEM(symbol, path) registerId(NamespacedId("minecraft", path));
    MECRAFT_FOR_EACH_BUILTIN_PURE_ITEM(MECRAFT_REGISTER_BUILTIN_PURE_ITEM)
#undef MECRAFT_REGISTER_BUILTIN_PURE_ITEM
}
