#ifndef MECRAFT_ECS_HOPPER_SYSTEM_H
#define MECRAFT_ECS_HOPPER_SYSTEM_H

#include "../../ISystem.h"
#include <cstddef>
#include <cstdint>

class World;

namespace ecs {

class GameplayRegistry;

/// Moves items through data-driven hopper block-entity inventories.
class HopperSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Process loaded hopper inventories on the authoritative world.
    /// @param world  Mutable world containing block states.
    /// @param registry  Gameplay registry that owns block-entity inventories and item drops.
    /// @param gameTick  Current 20 Hz game tick used for hopper transfer cadence.
    /// @return Number of item transfer operations performed.
    static std::size_t processWorld(World& world, GameplayRegistry& registry, std::uint64_t gameTick);
};

} // namespace ecs

#endif // MECRAFT_ECS_HOPPER_SYSTEM_H
