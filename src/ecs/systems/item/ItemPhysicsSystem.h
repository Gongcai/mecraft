#ifndef MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
#define MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H

#include "../../GameplayRegistry.h"

class World;

namespace ecs {

class ItemPhysicsSystem {
public:
    static void update(GameplayRegistry& registry, const World& world, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
