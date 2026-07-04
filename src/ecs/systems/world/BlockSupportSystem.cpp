#include "BlockSupportSystem.h"

#include "../../util/DropSpawnEventBuffer.h"
#include "../../util/FallingBlockEventBuffer.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../components/Components.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/AttachmentFaceGeometry.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/WireFaceGeometry.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace ecs {

namespace {

[[noreturn]] void failBlockSupportSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

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
    const BlockStateId stateId = world.getBlockState(supportPos.x, supportPos.y, supportPos.z);
    if (stateId == NULL_BLOCK_STATE) return false;
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isSolid;
}

/// Evaluate the "ground" support rule: block below must be solid.
bool checkGround(const World& world, const glm::ivec3& pos) {
    return isSupportive(world, pos + glm::ivec3(0, -1, 0));
}

bool checkFarmland(const World& world, const glm::ivec3& pos) {
    const BlockStateId stateId = world.getBlockState(pos.x, pos.y - 1, pos.z);
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).namespacedId.path() == "farmland";
}

/// Evaluate the "attached_face" support rule for face-attached blocks.
/// The `facing` property identifies the outward face, so support is located in
/// the opposite direction from that face normal.
bool checkAttachedFace(const World& world, const glm::ivec3& pos, BlockStateId stateId) {
    if (PropIndices::FACING == PropIndices::INVALID) {
        failBlockSupportSystem("attached_face support requires the facing property");
    }
    const uint16_t facingValueIndex = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (facingValueIndex == BlockStateRegistry::INVALID_INDEX) {
        failBlockSupportSystem("attached_face support requires a facing state value");
    }
    if (!WireFaceGeometry::isWireFacing(facingValueIndex)) {
        failBlockSupportSystem("attached_face support received an unsupported facing value");
    }

    return isSupportive(world, pos + WireFaceGeometry::supportOffset(facingValueIndex));
}

/// Evaluate the "face_attachment" support rule for oriented attached blocks.
/// The `face` property identifies the outward support face, and `facing`
/// remains available for the model or redstone output direction.
bool checkFaceAttachment(const World& world, const glm::ivec3& pos, BlockStateId stateId) {
    if (PropIndices::FACE == PropIndices::INVALID) {
        failBlockSupportSystem("face_attachment support requires the face property");
    }
    const uint16_t faceValueIndex = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACE);
    if (faceValueIndex == BlockStateRegistry::INVALID_INDEX) {
        failBlockSupportSystem("face_attachment support requires a face state value");
    }
    if (!AttachmentFaceGeometry::isAttachmentFace(faceValueIndex)) {
        failBlockSupportSystem("face_attachment support received an unsupported face value");
    }

    return isSupportive(world, pos + AttachmentFaceGeometry::supportOffset(faceValueIndex));
}

/// Returns true if the block at `pos` can survive given its supportRule.
bool canSurvive(const World& world, const glm::ivec3& pos) {
    const BlockStateId stateId = world.getBlockState(pos.x, pos.y, pos.z);
    if (stateId == NULL_BLOCK_STATE) return true;  // air always survives
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);

    const BlockDef& def = BlockRegistry::getFast(blockId);

    // Gravity-affected blocks (sand/gravel) only require solid ground below.
    if (def.affectedByGravity) {
        return checkGround(world, pos);
    }

    if (def.supportRule.empty()) return true;  // no rule = always survives

    if (def.supportRule == "ground") {
        return checkGround(world, pos);
    }
    if (def.supportRule == "farmland") {
        return checkFarmland(world, pos);
    }
    if (def.supportRule == "attached_face") {
        return checkAttachedFace(world, pos, stateId);
    }
    if (def.supportRule == "face_attachment") {
        return checkFaceAttachment(world, pos, stateId);
    }

    failBlockSupportSystem("Unsupported block support rule: " + def.supportRule);
}

struct SupportEventSinks {
    DropSpawnEventBus* dropBus = nullptr;
    AudioEventBus* audioBus = nullptr;
    ParticleEventBus* particleBus = nullptr;
    FallingBlockSpawnEventBus* fallingBlockBus = nullptr;
};

size_t processQueuedPositions(World& world,
                              const size_t budget,
                              const SupportEventSinks& sinks) {
    auto& updateQueue = world.neighborUpdateQueue();
    if (updateQueue.size() == 0 || budget == 0) {
        return 0;
    }

    std::vector<glm::ivec3> positions;
    positions.reserve(budget);
    const size_t drained = updateQueue.drain(positions, budget);

    for (const glm::ivec3& pos : positions) {
        if (canSurvive(world, pos)) {
            continue;
        }

        const BlockStateId stateId = world.getBlockState(pos.x, pos.y, pos.z);
        const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
        const BlockDef& def = BlockRegistry::getFast(blockId);

        world.setBlockState(pos.x, pos.y, pos.z, NULL_BLOCK_STATE);
        if (def.affectedByGravity) {
            if (sinks.fallingBlockBus != nullptr) {
                sinks.fallingBlockBus->push({blockId, pos});
            }
            continue;
        }

        if (sinks.dropBus != nullptr) {
            sinks.dropBus->push({blockId, pos});
        }
        if (sinks.particleBus != nullptr) {
            sinks.particleBus->push({pos, blockId});
        }
        if (sinks.audioBus != nullptr) {
            sinks.audioBus->push({"block.generic.break", glm::vec3(pos), true, 1.0f});
        }
    }

    return drained;
}

} // namespace

void BlockSupportSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    if (ctx.services.gameClient) return;
    auto& world = *ctx.services.world;
    auto& registry = ctx.registry;

    processWorldQueue(world, registry, 1024);
}

size_t BlockSupportSystem::processWorldQueue(World& world, const size_t budget) {
    return processQueuedPositions(world, budget, {});
}

size_t BlockSupportSystem::processWorldQueue(World& world, GameplayRegistry& registry, const size_t budget) {
    SupportEventSinks sinks;
    sinks.dropBus = &ensureDropSpawnEventBus(registry);
    sinks.audioBus = &ensureAudioEventBus(registry);
    sinks.particleBus = &ensureParticleEventBus(registry);
    sinks.fallingBlockBus = &ensureFallingBlockSpawnEventBus(registry);
    return processQueuedPositions(world, budget, sinks);
}

} // namespace ecs
