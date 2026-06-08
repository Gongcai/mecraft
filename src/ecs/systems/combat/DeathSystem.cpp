#include "DeathSystem.h"

#include <algorithm>
#include <random>
#include <vector>

#include "../item/ItemSpawnSystem.h"

namespace ecs {
namespace {

void collectEntityTree(entt::registry& registry, const entt::entity entity, std::vector<entt::entity>& out) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }

    out.push_back(entity);
    if (const auto* children = registry.try_get<ChildrenComponent>(entity)) {
        for (const entt::entity child : children->children) {
            collectEntityTree(registry, child, out);
        }
    }
}

std::mt19937& lootRng() {
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

uint32_t rollDropCount(const DropTableEntry& drop) {
    const uint32_t minCount = drop.minCount;
    uint32_t maxCount = drop.maxCount > 0 ? drop.maxCount : drop.minCount;
    maxCount = std::max(minCount, maxCount);
    if (minCount == maxCount) {
        return minCount;
    }

    std::uniform_int_distribution<uint32_t> dist(minCount, maxCount);
    return dist(lootRng());
}

template <typename SpawnFn>
void forEachDropEntry(const DropTableComponent& dropTable, SpawnFn&& spawn) {
    if (!dropTable.entries.empty()) {
        for (const DropTableEntry& drop : dropTable.entries) {
            spawn(drop);
        }
        return;
    }

    spawn(DropTableEntry{dropTable.itemId, dropTable.minCount, dropTable.maxCount});
}

} // namespace

void DeathSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    auto& reg = registry.registry();

    std::vector<entt::entity> deadMobs;
    auto view = reg.view<MobTag, HealthComponent>();
    for (const entt::entity entity : view) {
        const auto& health = view.get<HealthComponent>(entity);
        if (health.current <= 0) {
            deadMobs.push_back(entity);
        }
    }

    for (const entt::entity entity : deadMobs) {
        if (!reg.valid(entity)) {
            continue;
        }

        if (const auto* dropTable = reg.try_get<DropTableComponent>(entity)) {
            const auto* transform = reg.try_get<TransformComponent>(entity);
            const glm::vec3 position = transform ? transform->position : glm::vec3(0.0f);
            forEachDropEntry(*dropTable, [&](const DropTableEntry& drop) {
                const uint32_t count = rollDropCount(drop);
                if (drop.itemId != 0 && count != 0) {
                    ItemSpawnSystem::spawnAtPosition(registry, drop.itemId, position, count);
                }
            });
        }

        std::vector<entt::entity> toDestroy;
        collectEntityTree(reg, entity, toDestroy);
        for (const entt::entity doomed : toDestroy) {
            if (reg.valid(doomed)) {
                reg.destroy(doomed);
            }
        }
    }
}

} // namespace ecs
