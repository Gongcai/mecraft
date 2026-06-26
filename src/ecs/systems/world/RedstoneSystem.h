#ifndef MECRAFT_ECS_REDSTONE_SYSTEM_H
#define MECRAFT_ECS_REDSTONE_SYSTEM_H

#include "../../ISystem.h"
#include <cstddef>
#include <cstdint>

class World;

namespace ecs {

class GameplayRegistry;

/// Tick-rate system that evaluates deterministic redstone power propagation.
class RedstoneSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Process dirty redstone positions and their connected local work sets.
    /// @param world  Mutable world containing loaded chunks and block states.
    /// @param redstoneTick  Redstone tick index used by delayed devices in later phases.
    /// @param budget  Maximum number of dirty positions drained from the redstone queue.
    /// @return Number of block states changed by this processing pass.
    static size_t processWorld(World& world, uint64_t redstoneTick, size_t budget = 4096);

    /// Process dirty redstone positions with access to gameplay block-entity inventories.
    /// @param world  Mutable world containing loaded chunks and block states.
    /// @param redstoneTick  Redstone tick index used by delayed devices.
    /// @param registry  Gameplay registry that owns block-entity inventory stores.
    /// @param budget  Maximum number of dirty positions drained from the redstone queue.
    /// @return Number of block states changed by this processing pass.
    static size_t processWorld(World& world,
                               uint64_t redstoneTick,
                               const GameplayRegistry& registry,
                               size_t budget = 4096);

    /// Process dirty redstone positions with mutable gameplay entities.
    /// @param world  Mutable world containing loaded chunks and block states.
    /// @param redstoneTick  Redstone tick index used by delayed devices.
    /// @param registry  Gameplay registry that owns entities and block-entity inventory stores.
    /// @param budget  Maximum number of dirty positions drained from the redstone queue.
    /// @return Number of block states changed by this processing pass.
    static size_t processWorld(World& world,
                               uint64_t redstoneTick,
                               GameplayRegistry& registry,
                               size_t budget = 4096);
};

} // namespace ecs

#endif // MECRAFT_ECS_REDSTONE_SYSTEM_H
