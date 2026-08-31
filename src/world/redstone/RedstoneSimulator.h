#ifndef MECRAFT_REDSTONE_SIMULATOR_H
#define MECRAFT_REDSTONE_SIMULATOR_H

#include <glm/vec3.hpp>

#include "../block/BlockStateRegistry.h"

class World;

/// Routes block-change notifications into the redstone dirty queues.
///
/// Centralizes the redstone-specific neighborhood bookkeeping that used to
/// live inside the generic block write path: which positions must be
/// re-evaluated after an edit, including wire cells connected across outer
/// corners. Enqueue order matches the historical World sequences exactly,
/// because BlockNeighborUpdateQueue processes positions in first-enqueue
/// order.
class RedstoneSimulator {
public:
    explicit RedstoneSimulator(World& world) : m_world(world) {}

    /// Enqueue redstone updates for a block state edit: the edited cell, its
    /// 6 neighbors, outer-corner wire peers, and wire cells whose corner
    /// support is blocked by the edited cell.
    void onBlockChanged(const glm::ivec3& pos);

    /// Enqueue redstone updates for a wire container part change: the cell,
    /// its 6 neighbors, and outer-corner wire peers (no blocked-corner pass).
    void onWireContainerPartsChanged(const glm::ivec3& pos);

    /// Drop torch runtime state when a torch block is replaced by a
    /// non-torch state.
    void onBlockStateReplaced(const glm::ivec3& pos, BlockStateId oldState, BlockStateId newState);

private:
    World& m_world;
};

#endif // MECRAFT_REDSTONE_SIMULATOR_H
