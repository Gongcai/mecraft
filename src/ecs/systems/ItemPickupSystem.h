#ifndef MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
#define MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H

#include <glm/glm.hpp>

#include "../GameplayRegistry.h"

class Inventory;

namespace ecs {

class ItemPickupSystem {
public:
    static uint32_t update(GameplayRegistry& registry,
                           const glm::vec3& position,
                           float radius,
                           Inventory& inventory);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PICKUP_SYSTEM_H
