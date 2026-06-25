#ifndef MECRAFT_ECS_FALLING_BLOCK_TICK_SYSTEM_H
#define MECRAFT_ECS_FALLING_BLOCK_TICK_SYSTEM_H

#include "../../ISystem.h"

#include <cstddef>

class World;

namespace ecs {

class GameplayRegistry;

/// Tick-rate (20 TPS) system that advances each falling-block entity one
/// grid cell downward per tick (Minecraft falling-block semantics).
/// On landing (solid block below, world bottom, or unload), the entity
/// writes its block back into the world via World::setBlock and is destroyed.
class FallingBlockTickSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    static size_t tickWorld(World& world, GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_FALLING_BLOCK_TICK_SYSTEM_H
