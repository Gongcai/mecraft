#include "BlockSupportSystem.h"

#include "../../util/DropSpawnEventBuffer.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../components/Components.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"

namespace ecs {

namespace {

/// 6 cardinal neighbor offsets (in the same order Minecraft uses).
constexpr glm::ivec3 kNeighborOffsets[6] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  1,  0},
    { 0, -1,  0},
    { 0,  0,  1},
    { 0,  0, -1},
};

/// Check if a block at `supportPos` can provide support (solid block).
bool isSupportive(const World& world, const glm::ivec3& supportPos) {
    const BlockID id = world.getBlock(supportPos.x, supportPos.y, supportPos.z);
    if (id == 0) return false;
    return BlockRegistry::getFast(id).isSolid;
}

/// Evaluate the "ground" support rule: block below must be solid.
bool checkGround(const World& world, const glm::ivec3& pos) {
    return isSupportive(world, pos + glm::ivec3(0, -1, 0));
}

/// Evaluate the "attached_face" support rule for torches.
/// The torch has a `facing` property that indicates which face it's attached to.
/// We look up the facing value and check if the block on that side is solid.
bool checkAttachedFace(const World& world, const glm::ivec3& pos, StateID stateId) {
    // Read the facing property from the block state
    const uint16_t facingValueIndex = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    // facingValueIndex maps to: floor, north, south, east, west
    // For wall-mounted torches, we need the block on the opposite side of the face direction.

    // If facing=floor, the torch sits on top of a block — check below
    if (facingValueIndex == PropIndices::FACING_FLOOR) {
        return isSupportive(world, pos + glm::ivec3(0, -1, 0));
    }

    // Wall-mounted: check the block the torch is attached to
    // The facing direction points outward from the attachment surface,
    // so the attachment block is in the *opposite* direction.
    glm::ivec3 attachDir(0);
    if (facingValueIndex == PropIndices::FACING_NORTH) {
        attachDir = {0, 0, 1};   // torch faces -Z, attached to block at +Z
    } else if (facingValueIndex == PropIndices::FACING_SOUTH) {
        attachDir = {0, 0, -1};  // torch faces +Z, attached to block at -Z
    } else if (facingValueIndex == PropIndices::FACING_EAST) {
        attachDir = {-1, 0, 0};   // torch faces +X, attached to block at -X
    } else if (facingValueIndex == PropIndices::FACING_WEST) {
        attachDir = {1, 0, 0};    // torch faces -X, attached to block at +X
    }

    return isSupportive(world, pos + attachDir);
}

/// Returns true if the block at `pos` can survive given its supportRule.
bool canSurvive(const World& world, const glm::ivec3& pos) {
    const BlockID blockId = world.getBlock(pos.x, pos.y, pos.z);
    if (blockId == 0) return true;  // air always survives

    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.supportRule.empty()) return true;  // no rule = always survives

    const StateID stateId = world.getBlockState(pos.x, pos.y, pos.z);

    if (def.supportRule == "ground") {
        return checkGround(world, pos);
    }
    if (def.supportRule == "attached_face") {
        return checkAttachedFace(world, pos, stateId);
    }

    // Unknown rule — default to surviving
    return true;
}

} // namespace

void BlockSupportSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    auto& world = *ctx.services.world;
    auto& registry = ctx.registry;

    auto& updateQueue = world.neighborUpdateQueue();
    if (updateQueue.size() == 0) return;

    auto& dropBus = ensureDropSpawnEventBus(registry);
    auto& audioBus = ensureAudioEventBus(registry);
    auto& particleBus = ensureParticleEventBus(registry);

    // Drain up to 1024 positions per tick to avoid frame spikes.
    std::vector<glm::ivec3> positions;
    positions.reserve(1024);
    updateQueue.drain(positions, 1024);

    for (const glm::ivec3& pos : positions) {
        if (!canSurvive(world, pos)) {
            const BlockID blockId = world.getBlock(pos.x, pos.y, pos.z);
            world.setBlock(pos.x, pos.y, pos.z, 0);
            dropBus.push({blockId, pos});
            particleBus.push({pos, blockId});
            audioBus.push({"block.generic.break", glm::vec3(pos), true, 1.0f});
        }
    }
}

size_t BlockSupportSystem::processWorldQueue(World& world, const size_t budget) {
    auto& updateQueue = world.neighborUpdateQueue();
    if (updateQueue.size() == 0 || budget == 0) {
        return 0;
    }

    std::vector<glm::ivec3> positions;
    positions.reserve(budget);
    const size_t drained = updateQueue.drain(positions, budget);

    for (const glm::ivec3& pos : positions) {
        if (!canSurvive(world, pos)) {
            world.setBlock(pos.x, pos.y, pos.z, 0);
        }
    }

    return drained;
}

} // namespace ecs
