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

void IdRegistry::initBuiltinBlockIds() {
    // Register built-in block IDs in stable order.
    // RuntimeId assignment order must remain fixed for data compatibility.

    registerId(NamespacedId("minecraft", "air"));
    registerId(NamespacedId("minecraft", "dirt"));
    registerId(NamespacedId("minecraft", "grass_block"));
    registerId(NamespacedId("minecraft", "stone"));
    registerId(NamespacedId("minecraft", "sand"));
    registerId(NamespacedId("minecraft", "oak_log"));
    registerId(NamespacedId("minecraft", "glass"));
    registerId(NamespacedId("minecraft", "coal_ore"));
    registerId(NamespacedId("minecraft", "diamond_ore"));
    registerId(NamespacedId("minecraft", "gold_ore"));
    registerId(NamespacedId("minecraft", "iron_ore"));
    registerId(NamespacedId("minecraft", "water"));
    registerId(NamespacedId("minecraft", "bedrock"));
    registerId(NamespacedId("minecraft", "tall_grass"));
    registerId(NamespacedId("minecraft", "rose"));
    registerId(NamespacedId("minecraft", "oak_planks"));
    registerId(NamespacedId("minecraft", "spruce_planks"));
    registerId(NamespacedId("minecraft", "birch_planks"));
    registerId(NamespacedId("minecraft", "jungle_planks"));
    registerId(NamespacedId("minecraft", "acacia_planks"));
    registerId(NamespacedId("minecraft", "dark_oak_planks"));
    registerId(NamespacedId("minecraft", "mangrove_planks"));
    registerId(NamespacedId("minecraft", "cherry_planks"));
    registerId(NamespacedId("minecraft", "pale_oak_planks"));
    registerId(NamespacedId("minecraft", "bamboo_planks"));
    registerId(NamespacedId("minecraft", "crimson_planks"));
    registerId(NamespacedId("minecraft", "warped_planks"));
    registerId(NamespacedId("minecraft", "birch_log"));
    registerId(NamespacedId("minecraft", "torch"));
    registerId(NamespacedId("minecraft", "brown_mushroom"));
}

void IdRegistry::initBuiltinItemIds() {
    // Items use their own IdRegistry, starting from 0.
    // Block↔Item mapping is handled explicitly by ItemRegistry.

    // 0 → minecraft:air
    registerId(NamespacedId("minecraft", "air"));
    // Block items follow the same order as blocks (1-29)
    registerId(NamespacedId("minecraft", "dirt"));
    registerId(NamespacedId("minecraft", "grass_block"));
    registerId(NamespacedId("minecraft", "stone"));
    registerId(NamespacedId("minecraft", "sand"));
    registerId(NamespacedId("minecraft", "oak_log"));
    registerId(NamespacedId("minecraft", "glass"));
    registerId(NamespacedId("minecraft", "coal_ore"));
    registerId(NamespacedId("minecraft", "diamond_ore"));
    registerId(NamespacedId("minecraft", "gold_ore"));
    registerId(NamespacedId("minecraft", "iron_ore"));
    registerId(NamespacedId("minecraft", "water"));
    registerId(NamespacedId("minecraft", "bedrock"));
    registerId(NamespacedId("minecraft", "tall_grass"));
    registerId(NamespacedId("minecraft", "rose"));
    registerId(NamespacedId("minecraft", "oak_planks"));
    registerId(NamespacedId("minecraft", "spruce_planks"));
    registerId(NamespacedId("minecraft", "birch_planks"));
    registerId(NamespacedId("minecraft", "jungle_planks"));
    registerId(NamespacedId("minecraft", "acacia_planks"));
    registerId(NamespacedId("minecraft", "dark_oak_planks"));
    registerId(NamespacedId("minecraft", "mangrove_planks"));
    registerId(NamespacedId("minecraft", "cherry_planks"));
    registerId(NamespacedId("minecraft", "pale_oak_planks"));
    registerId(NamespacedId("minecraft", "bamboo_planks"));
    registerId(NamespacedId("minecraft", "crimson_planks"));
    registerId(NamespacedId("minecraft", "warped_planks"));
    registerId(NamespacedId("minecraft", "birch_log"));
    registerId(NamespacedId("minecraft", "torch"));
    registerId(NamespacedId("minecraft", "brown_mushroom"));
    // Pure items (were 256+ in old system)
    registerId(NamespacedId("minecraft", "coal"));
    registerId(NamespacedId("minecraft", "iron_pickaxe"));
}
