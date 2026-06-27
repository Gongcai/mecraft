#ifndef MECRAFT_ECS_MOVING_BLOCK_SYSTEM_H
#define MECRAFT_ECS_MOVING_BLOCK_SYSTEM_H

#include "../../ISystem.h"

#include <cstddef>

class World;

namespace ecs {

class GameplayRegistry;

/// Fixed-update system that advances piston-driven moving blocks.
class MovingBlockSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Advance all active moving blocks and resolve their entity collisions.
    /// @param world Mutable world receiving final block placement.
    /// @param registry Gameplay registry that owns moving-block and physics entities.
    /// @param dt Fixed-update delta time in seconds.
    /// @return Number of moving block entities completed during this update.
    static std::size_t processWorld(World& world, GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_MOVING_BLOCK_SYSTEM_H
