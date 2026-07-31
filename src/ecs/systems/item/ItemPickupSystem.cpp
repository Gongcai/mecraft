#include "ItemPickupSystem.h"

#include <vector>

#include "../../components/Components.h"
#include "../../../player/Inventory.h"

namespace ecs {

void ItemPickupSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    constexpr float kDropCollectRadius = 1.35f;
    constexpr float kAutoPickupDelaySeconds = 0.35f;

    auto playerView = registry.view<LocalPlayerTag, TransformComponent, InventoryDataComponent>();
    for (auto e : playerView) {
        if (const auto* health = registry.registry().try_get<HealthComponent>(e);
            health != nullptr && health->current <= 0) {
            continue;
        }
        const auto& transform = playerView.get<TransformComponent>(e);
        auto& inventoryData = playerView.get<InventoryDataComponent>(e);
        pickup(registry, transform.position, kDropCollectRadius, inventoryData.inventory, kAutoPickupDelaySeconds);
    }
}

uint32_t ItemPickupSystem::pickup(GameplayRegistry& registry, const glm::vec3& position, const float radius,
                                  Inventory& inventory) {
    return pickup(registry, position, radius, inventory, 0.0f);
}

uint32_t ItemPickupSystem::pickup(GameplayRegistry& registry, const glm::vec3& position, const float radius,
                                  Inventory& inventory, const float minAgeSeconds) {
    if (radius <= 0.0f) {
        return 0;
    }

    auto view = registry.view<DropItemTag, TransformComponent, ItemComponent>();
    std::vector<entt::entity> removed;
    const float radiusSq = radius * radius;
    uint32_t collectedTotal = 0;

    for (const entt::entity e : view) {
        const auto& transform = view.get<TransformComponent>(e);
        auto& item = view.get<ItemComponent>(e);
        if (item.stackCount == 0 || item.itemId == 0) {
            continue;
        }
        if (minAgeSeconds > 0.0f) {
            const auto* lifetime = registry.registry().try_get<LifetimeComponent>(e);
            if (lifetime != nullptr && lifetime->ageSeconds < minAgeSeconds) {
                continue;
            }
        }

        const glm::vec3 delta = transform.position - position;
        if (glm::dot(delta, delta) > radiusSq) {
            continue;
        }

        const uint32_t before = item.stackCount;
        const uint32_t remaining = inventory.addItem(item.itemId, item.stackCount);
        item.stackCount = remaining;
        collectedTotal += (before - remaining);
        if (item.stackCount == 0) {
            removed.push_back(e);
        }
    }

    for (const entt::entity e : removed) {
        if (registry.registry().valid(e)) {
            registry.destroy(e);
        }
    }

    return collectedTotal;
}

} // namespace ecs
