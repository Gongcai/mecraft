#ifndef MECRAFT_ECS_ITEM_PLACEMENT_RESOLVE_SYSTEM_H
#define MECRAFT_ECS_ITEM_PLACEMENT_RESOLVE_SYSTEM_H

#include <glm/glm.hpp>

#include "../../GameplayRegistry.h"

class World;

namespace ecs {

class ItemPlacementResolveSystem {
public:
    static void update(GameplayRegistry& registry, const World& world, const glm::ivec3& blockPos);
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PLACEMENT_RESOLVE_SYSTEM_H
