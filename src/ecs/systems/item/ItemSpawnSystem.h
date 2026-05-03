#ifndef MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H
#define MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H

#include <cstdint>
#include <glm/glm.hpp>

#include "../../ISystem.h"
#include "../../GameplayRegistry.h"
#include "../../../item/Item.h"

namespace ecs {

class ItemSpawnSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    static void spawn(GameplayRegistry& registry,
                      ItemID itemId,
                      const glm::ivec3& blockPos,
                      uint32_t stackCount);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_SPAWN_SYSTEM_H
