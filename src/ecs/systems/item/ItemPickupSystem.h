#ifndef MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
#define MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H

#include <glm/glm.hpp>

#include "../../ISystem.h"
#include "../../components/Components.h"

class Inventory;

namespace ecs {

/// Picks up nearby drop items for the local player.
/// Queries LocalPlayer entity directly from the registry.
class ItemPickupSystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<LocalPlayerTag, TransformComponent, DropItemTag, ItemComponent>,
                                          std::tuple<InventoryDataComponent>>;

    void update(SystemContext& ctx) override;

    /// Core pickup logic — can also be called directly.
    static uint32_t pickup(GameplayRegistry& registry, const glm::vec3& position, float radius,
                           class ::Inventory& inventory);
    static uint32_t pickup(GameplayRegistry& registry, const glm::vec3& position, float radius,
                           class ::Inventory& inventory, float minAgeSeconds);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
