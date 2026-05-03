#ifndef MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
#define MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H

#include <glm/glm.hpp>

#include "../../ISystem.h"

class Inventory;

namespace ecs {

/// Picks up nearby drop items for the local player.
/// Queries LocalPlayer entity directly from the registry.
class ItemPickupSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Core pickup logic — can also be called directly.
    static uint32_t pickup(GameplayRegistry& registry,
                           const glm::vec3& position,
                           float radius,
                           class ::Inventory& inventory);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
