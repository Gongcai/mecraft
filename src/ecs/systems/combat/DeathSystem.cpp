#include "DeathSystem.h"

#include <algorithm>
#include <cmath>
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

uint32_t dropCount(const DropTableComponent& dropTable) {
    return std::max(dropTable.minCount, dropTable.maxCount > 0 ? dropTable.maxCount : dropTable.minCount);
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
            if (dropTable->itemId != 0) {
                const auto* transform = reg.try_get<TransformComponent>(entity);
                const glm::vec3 position = transform ? transform->position : glm::vec3(0.0f);
                ItemSpawnSystem::spawn(registry,
                                       dropTable->itemId,
                                       glm::ivec3(std::floor(position.x),
                                                  std::floor(position.y),
                                                  std::floor(position.z)),
                                       dropCount(*dropTable));
            }
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
