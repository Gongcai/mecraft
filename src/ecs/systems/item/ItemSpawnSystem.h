#ifndef MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H
#define MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H

#include <cstdint>
#include <glm/glm.hpp>

#include "../../GameplayRegistry.h"
#include "../../../item/Item.h"

namespace ecs {

class ItemSpawnSystem {
public:
    static void update(GameplayRegistry& registry);

    static void spawn(GameplayRegistry& registry,
                      ItemID itemId,
                      const glm::ivec3& blockPos,
                      uint32_t stackCount);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H
