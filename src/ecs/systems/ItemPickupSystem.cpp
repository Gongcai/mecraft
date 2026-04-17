#include "ItemPickupSystem.h"

#include <vector>

#include "../components/Components.h"
#include "../../player/Inventory.h"

namespace ecs {

uint32_t ItemPickupSystem::update(GameplayRegistry& registry,
                                  const glm::vec3& position,
                                  const float radius,
                                  Inventory& inventory) {
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
