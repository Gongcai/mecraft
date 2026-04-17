#ifndef MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H
#define MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H

#include "../GameplayRegistry.h"

namespace ecs {

class ItemLifetimeSystem {
public:
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_LIFETIME_SYSTEM_H
