#ifndef MECRAFT_REDSTONE_SIMULATOR_H
#define MECRAFT_REDSTONE_SIMULATOR_H

#include <cstdint>

#include <glm/vec3.hpp>

#include "../block/BlockNeighborUpdateQueue.h"
#include "../block/BlockStateRegistry.h"
#include "RedstoneRuntimeState.h"
#include "RedstoneUpdateQueue.h"

/// Owns all transient redstone simulation state: the dirty-position queue,
/// the changed-block queue, the scheduled device-action queue, torch runtime
/// state, and the last processed redstone tick. World forwards block-change
/// notifications here so redstone bookkeeping stays out of the generic block
/// storage path.
class RedstoneSimulator {
public:
    /// Enqueue redstone dirty/changed entries for a block state edit: the
    /// edited cell, its 6 neighbors, outer-corner wire peers, and wire cells
    /// whose corner support is blocked by the edited cell. Enqueue order
    /// matches the historical World::setBlockState sequence exactly.
    void onBlockChanged(const glm::ivec3& pos);

    /// Enqueue redstone entries for a wire container part update: the cell,
    /// its 6 neighbors, and outer-corner wire peers (no blocked-corner
    /// pass). Enqueue order matches the historical
    /// World::notifyWireContainerPartsChanged sequence exactly.
    void onWireContainerPartsChanged(const glm::ivec3& pos);

    /// Drop torch runtime state when a torch block is replaced by a
    /// non-torch state.
    void onBlockStateReplaced(const glm::ivec3& pos, BlockStateId oldState, BlockStateId newState);

    /// Clear all queues and runtime state. Mirrors World::init() exactly:
    /// the last processed redstone tick is intentionally preserved.
    void reset();

    BlockNeighborUpdateQueue& updateQueue() { return m_updateQueue; }
    const BlockNeighborUpdateQueue& updateQueue() const { return m_updateQueue; }
    BlockNeighborUpdateQueue& changedBlockQueue() { return m_changedBlockQueue; }
    const BlockNeighborUpdateQueue& changedBlockQueue() const { return m_changedBlockQueue; }
    RedstoneUpdateQueue& scheduledUpdateQueue() { return m_scheduledUpdateQueue; }
    const RedstoneUpdateQueue& scheduledUpdateQueue() const { return m_scheduledUpdateQueue; }
    RedstoneRuntimeState& runtimeState() { return m_runtimeState; }
    const RedstoneRuntimeState& runtimeState() const { return m_runtimeState; }

    void setLastProcessedTick(const uint64_t redstoneTick) { m_lastProcessedTick = redstoneTick; }
    [[nodiscard]] uint64_t lastProcessedTick() const { return m_lastProcessedTick; }

private:
    BlockNeighborUpdateQueue m_updateQueue;
    BlockNeighborUpdateQueue m_changedBlockQueue;
    RedstoneUpdateQueue m_scheduledUpdateQueue;
    RedstoneRuntimeState m_runtimeState;
    uint64_t m_lastProcessedTick = 0;
};

#endif // MECRAFT_REDSTONE_SIMULATOR_H
