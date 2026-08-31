#include "RedstoneSimulator.h"

#include "../World.h"
#include "../block/Block.h"
#include "../fluid/FluidState.h"
#include "WireFaceGeometry.h"

namespace {

constexpr glm::ivec3 kNeighborOffsets[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

bool isRedstoneTorchRuntimeState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "torch";
}

} // namespace

void RedstoneSimulator::onBlockChanged(const glm::ivec3& pos) {
    m_world.redstoneUpdateQueue().enqueue(pos);
    m_world.redstoneChangedBlockQueue().enqueue(pos);
    for (const glm::ivec3& offset : kNeighborOffsets) {
        m_world.redstoneUpdateQueue().enqueue(pos + offset);
    }
    WireFaceGeometry::forEachWireOuterCornerPeerPosition(
        pos, [this](const glm::ivec3& peer) { m_world.redstoneUpdateQueue().enqueue(peer); });
    WireFaceGeometry::forEachWireOuterCornerPositionBlockedBy(
        pos, [this](const glm::ivec3& wirePosition) { m_world.redstoneUpdateQueue().enqueue(wirePosition); });
}

void RedstoneSimulator::onWireContainerPartsChanged(const glm::ivec3& pos) {
    m_world.redstoneUpdateQueue().enqueue(pos);
    m_world.redstoneChangedBlockQueue().enqueue(pos);
    for (const glm::ivec3& offset : kNeighborOffsets) {
        m_world.redstoneUpdateQueue().enqueue(pos + offset);
    }
    WireFaceGeometry::forEachWireOuterCornerPeerPosition(
        pos, [this](const glm::ivec3& peer) { m_world.redstoneUpdateQueue().enqueue(peer); });
}

void RedstoneSimulator::onBlockStateReplaced(const glm::ivec3& pos, const BlockStateId oldState,
                                             const BlockStateId newState) {
    if (isRedstoneTorchRuntimeState(oldState) && !isRedstoneTorchRuntimeState(newState)) {
        m_world.redstoneRuntimeState().eraseTorch(pos);
    }
}
