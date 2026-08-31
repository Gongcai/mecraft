#include "RedstoneSimulator.h"

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
    m_updateQueue.enqueue(pos);
    m_changedBlockQueue.enqueue(pos);
    for (const glm::ivec3& offset : kNeighborOffsets) {
        m_updateQueue.enqueue(pos + offset);
    }
    WireFaceGeometry::forEachWireOuterCornerPeerPosition(
        pos, [this](const glm::ivec3& peer) { m_updateQueue.enqueue(peer); });
    WireFaceGeometry::forEachWireOuterCornerPositionBlockedBy(
        pos, [this](const glm::ivec3& wirePosition) { m_updateQueue.enqueue(wirePosition); });
}

void RedstoneSimulator::onWireContainerPartsChanged(const glm::ivec3& pos) {
    m_updateQueue.enqueue(pos);
    m_changedBlockQueue.enqueue(pos);
    for (const glm::ivec3& offset : kNeighborOffsets) {
        m_updateQueue.enqueue(pos + offset);
    }
    WireFaceGeometry::forEachWireOuterCornerPeerPosition(
        pos, [this](const glm::ivec3& peer) { m_updateQueue.enqueue(peer); });
}

void RedstoneSimulator::onBlockStateReplaced(const glm::ivec3& pos, const BlockStateId oldState,
                                             const BlockStateId newState) {
    if (isRedstoneTorchRuntimeState(oldState) && !isRedstoneTorchRuntimeState(newState)) {
        m_runtimeState.eraseTorch(pos);
    }
}

void RedstoneSimulator::reset() {
    m_updateQueue.clear();
    m_changedBlockQueue.clear();
    m_scheduledUpdateQueue.clear();
    m_runtimeState.clear();
    // m_lastProcessedTick is preserved on purpose: World::init() never reset
    // it, and ProjectileSystem aligns new pulses against the maximum of this
    // value and the current redstone tick.
}
