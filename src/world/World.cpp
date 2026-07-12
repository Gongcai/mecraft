#include "World.h"
#include "WorldRaycast.h"
#include "../save/SaveManager.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include "engine//platform/Time.h"
#include "block/AttachmentFaceGeometry.h"
#include "block/BlockSelection.h"
#include "block/BlockStateRegistry.h"
#include "block/PropIndices.h"
#include "fluid/FluidRegistry.h"
#include "fluid/FluidState.h"
#include "redstone/WireFaceGeometry.h"

namespace {
[[noreturn]] void failWorld(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

int worldToChunkCoord(const int world, const int chunkSize) {
    // floor-divide for negative coordinates
    return static_cast<int>(std::floor(static_cast<float>(world) / static_cast<float>(chunkSize)));
}

void markChunkSubChunkAndVerticalNeighborsDirty(Chunk& chunk, const int scy, const int localY) {
    chunk.markSubChunkDirty(scy);
    if (localY == 0) {
        chunk.markSubChunkDirty(scy - 1);
    }
    if (localY == Chunk::SUB_CHUNK_SIZE - 1) {
        chunk.markSubChunkDirty(scy + 1);
    }
}

template <typename Fn>
void forEachWireOuterCornerPeerPosition(const glm::ivec3& position, Fn&& fn) {
    for (const uint16_t selfFacing : WireFaceGeometry::wireFacings()) {
        const glm::ivec3 support = WireFaceGeometry::supportPosition(position, selfFacing);
        for (const uint16_t peerFacing : WireFaceGeometry::wireFacings()) {
            if (!WireFaceGeometry::arePerpendicularFacings(selfFacing, peerFacing)) {
                continue;
            }
            fn(WireFaceGeometry::wirePositionOnSupportFace(support, peerFacing));
        }
    }
}

template <typename Fn>
void forEachWireOuterCornerPositionBlockedBy(const glm::ivec3& blocker, Fn&& fn) {
    for (const uint16_t facingA : WireFaceGeometry::wireFacings()) {
        for (const uint16_t facingB : WireFaceGeometry::wireFacings()) {
            if (facingA >= facingB || !WireFaceGeometry::arePerpendicularFacings(facingA, facingB)) {
                continue;
            }

            const glm::ivec3 support =
                blocker - WireFaceGeometry::surfaceNormal(facingA) - WireFaceGeometry::surfaceNormal(facingB);
            fn(WireFaceGeometry::wirePositionOnSupportFace(support, facingA));
            fn(WireFaceGeometry::wirePositionOnSupportFace(support, facingB));
        }
    }
}

template <typename Fn>
void forEachWirePositionOnSupport(const glm::ivec3& support, Fn&& fn) {
    for (const uint16_t facing : WireFaceGeometry::wireFacings()) {
        fn(WireFaceGeometry::wirePositionOnSupportFace(support, facing));
    }
}

bool canWaterOccupyBlockLayer(const BlockStateId state) {
    const FluidDesc& waterDesc = FluidRegistry::get(FluidKind::Water);
    return FluidState::canReplace(waterDesc, state) || FluidState::canCoexist(waterDesc, state);
}

bool canBlockStateKeepFluidLayer(const BlockStateId blockState, const BlockStateId fluidState) {
    const DecodedFluid fluid = FluidState::decode(fluidState);
    if (fluid.kind == FluidKind::None) {
        return true;
    }
    if (FluidState::decode(blockState).kind != FluidKind::None) {
        return false;
    }
    return FluidState::canCoexist(FluidRegistry::get(fluid.kind), blockState);
}

bool changesFluidPathing(const BlockStateId oldState, const BlockStateId newState) {
    return canWaterOccupyBlockLayer(oldState) != canWaterOccupyBlockLayer(newState);
}

BlockStateId normalizeFluidBlockState(const BlockStateId stateId) {
    return stateId;
}

bool isConnectedBlockDef(const BlockDef& def) {
    return def.placementStrategy == "fence" || def.placementStrategy == "wall";
}

bool isStairsBlockDef(const BlockDef& def) {
    return def.placementStrategy == "stairs";
}

bool isRedstoneWireBlockDef(const BlockDef& def) {
    return def.redstoneBehavior == "wire";
}

bool isWireContainerState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isWireContainer;
}

bool isSolidBlockState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isSolid;
}

bool isRedstoneWireState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return isRedstoneWireBlockDef(BlockRegistry::getFast(blockId));
}

bool isRedstoneTorchRuntimeState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "torch";
}

bool isFaceOrientedLogicUnitDef(const BlockDef& def) {
    return def.redstoneBehavior == "repeater" ||
           def.redstoneBehavior == "comparator";
}

bool faceOrientedLogicUnitMatchesWireFacing(const BlockStateId stateId, const uint16_t wireFacing) {
    const uint16_t face = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACE);
    if (face == BlockStateRegistry::INVALID_INDEX) {
        failWorld("Face-oriented redstone logic unit is missing the face property");
    }
    return AttachmentFaceGeometry::facingValueForFace(face) == wireFacing;
}

bool isMatchingRedstoneWireState(const BlockStateId stateId, const uint16_t wireChannelId) {
    if (!isRedstoneWireState(stateId)) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneWireChannelId == wireChannelId;
}

uint16_t redstoneWireFacingForState(const BlockStateId stateId) {
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (facing == BlockStateRegistry::INVALID_INDEX) {
        failWorld("Redstone wire connection update requires wire facing values");
    }
    if (!WireFaceGeometry::isWireFacing(facing)) {
        failWorld("Redstone wire connection update received an unsupported facing value");
    }
    return facing;
}

bool isMatchingRedstoneWireStateWithFacing(const BlockStateId stateId,
                                           const uint16_t wireChannelId,
                                           const uint16_t wireFacing) {
    return isMatchingRedstoneWireState(stateId, wireChannelId) &&
           redstoneWireFacingForState(stateId) == wireFacing;
}

const WirePart* findWireContainerPartAt(const World& world,
                                        const glm::ivec3& position,
                                        const uint16_t wireChannelId,
                                        const uint16_t wireFacing) {
    const WireContainerParts* parts = world.wireContainerParts().find(position);
    return parts == nullptr ? nullptr : parts->find(wireChannelId, wireFacing);
}

bool hasWireContainerPartAt(const World& world,
                            const glm::ivec3& position,
                            const uint16_t wireChannelId,
                            const uint16_t wireFacing) {
    return findWireContainerPartAt(world, position, wireChannelId, wireFacing) != nullptr;
}

bool hasMatchingRedstoneWireAt(const World& world,
                               const glm::ivec3& position,
                               const uint16_t wireChannelId,
                               const uint16_t wireFacing) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (isMatchingRedstoneWireStateWithFacing(stateId, wireChannelId, wireFacing)) {
        return true;
    }
    return isWireContainerState(stateId) &&
           hasWireContainerPartAt(world, position, wireChannelId, wireFacing);
}

bool canConnectedBlockAttachTo(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return isConnectedBlockDef(def) || def.isSolid;
}

bool canRedstoneWireAttachToAt(const World& world,
                               const glm::ivec3& position,
                               const uint16_t wireChannelId,
                               const uint16_t wireFacing) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    if (hasMatchingRedstoneWireAt(world, position, wireChannelId, wireFacing)) {
        return true;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.isRedstonePowerSource) {
        return true;
    }

    if (isFaceOrientedLogicUnitDef(def)) {
        return faceOrientedLogicUnitMatchesWireFacing(stateId, wireFacing);
    }
    return def.redstoneBehavior == "observer";
}

bool canPlanarRedstoneWireAttachTo(const BlockStateId stateId,
                                   const uint16_t wireChannelId,
                                   const uint16_t wireFacing) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (isRedstoneWireBlockDef(def)) {
        if (def.redstoneWireChannelId != wireChannelId) {
            return false;
        }
        return redstoneWireFacingForState(stateId) == wireFacing;
    }

    if (def.isRedstonePowerSource) {
        return true;
    }

    if (isFaceOrientedLogicUnitDef(def)) {
        return faceOrientedLogicUnitMatchesWireFacing(stateId, wireFacing);
    }
    return def.redstoneBehavior == "observer";
}

uint16_t redstoneWirePlanarConnectionValue(const BlockStateId neighborState,
                                           const uint16_t wireChannelId,
                                           const uint16_t wireFacing,
                                           const uint16_t noneValue,
                                           const uint16_t sideValue) {
    return canPlanarRedstoneWireAttachTo(neighborState, wireChannelId, wireFacing)
        ? sideValue
        : noneValue;
}

bool hasSameCellRedstoneWireConnection(const World& world,
                                       const glm::ivec3& pos,
                                       const uint16_t wireChannelId,
                                       const uint16_t wireFacing,
                                       const glm::ivec3& connectionOffset) {
    const uint16_t peerFacing = WireFaceGeometry::facingFromSurfaceNormal(-connectionOffset);
    if (!WireFaceGeometry::arePerpendicularFacings(wireFacing, peerFacing)) {
        failWorld("Redstone wire same-cell connection requires a perpendicular face");
    }
    return hasMatchingRedstoneWireAt(world, pos, wireChannelId, peerFacing);
}

bool hasOuterCornerRedstoneWireConnection(const World& world,
                                          const glm::ivec3& pos,
                                          const uint16_t wireChannelId,
                                          const uint16_t wireFacing,
                                          const glm::ivec3& connectionOffset) {
    const uint16_t peerFacing = WireFaceGeometry::facingFromSurfaceNormal(connectionOffset);
    if (!WireFaceGeometry::arePerpendicularFacings(wireFacing, peerFacing)) {
        failWorld("Redstone wire outer-corner connection requires a perpendicular face");
    }

    const glm::ivec3 support = WireFaceGeometry::supportPosition(pos, wireFacing);
    const glm::ivec3 blocker = WireFaceGeometry::outerCornerBlockingPosition(support, wireFacing, peerFacing);
    if (isSolidBlockState(world.getBlockState(blocker.x, blocker.y, blocker.z))) {
        return false;
    }

    const glm::ivec3 peerPosition = WireFaceGeometry::wirePositionOnSupportFace(support, peerFacing);
    return hasMatchingRedstoneWireAt(world, peerPosition, wireChannelId, peerFacing);
}

uint16_t redstoneWireConnectionValueAt(const World& world,
                                       const glm::ivec3& pos,
                                       const uint16_t wireChannelId,
                                       const uint16_t wireFacing,
                                       const WireFaceGeometry::ConnectionDirection& connection) {
    const glm::ivec3 planarNeighbor = pos + connection.offset;
    if (canRedstoneWireAttachToAt(world, planarNeighbor, wireChannelId, wireFacing)) {
        return connection.sideValue;
    }
    return (hasSameCellRedstoneWireConnection(world, pos, wireChannelId, wireFacing, connection.offset) ||
            hasOuterCornerRedstoneWireConnection(world, pos, wireChannelId, wireFacing, connection.offset))
        ? connection.sideValue
        : connection.noneValue;
}

glm::ivec3 wireAxis1PositiveOffset(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST || facing == PropIndices::FACING_WEST) {
        return {0, 0, 1};
    }
    if (WireFaceGeometry::isWireFacing(facing)) {
        return {1, 0, 0};
    }
    failWorld("Wire container connection update received an unsupported wire facing");
}

glm::ivec3 wireAxis2PositiveOffset(const uint16_t facing) {
    if (facing == PropIndices::FACING_FLOOR || facing == PropIndices::FACING_CEILING) {
        return {0, 0, 1};
    }
    if (WireFaceGeometry::isWireFacing(facing)) {
        return {0, 1, 0};
    }
    failWorld("Wire container connection update received an unsupported wire facing");
}

uint8_t wireConnectionBitForOffset(const uint16_t facing, const glm::ivec3& offset) {
    const glm::ivec3 axis1 = wireAxis1PositiveOffset(facing);
    const glm::ivec3 axis2 = wireAxis2PositiveOffset(facing);
    if (offset == axis1) {
        return WireConnectionBits::AXIS1_POS;
    }
    if (offset == -axis1) {
        return WireConnectionBits::AXIS1_NEG;
    }
    if (offset == axis2) {
        return WireConnectionBits::AXIS2_POS;
    }
    if (offset == -axis2) {
        return WireConnectionBits::AXIS2_NEG;
    }
    failWorld("Wire container connection update received a connection outside the wire plane");
}

uint8_t refreshedWireContainerConnections(const World& world,
                                          const glm::ivec3& pos,
                                          const WirePart& part) {
    uint8_t connections = 0;
    for (const WireFaceGeometry::ConnectionDirection& connection :
         WireFaceGeometry::connectionDirections(part.facing)) {
        const glm::ivec3 planarNeighbor = pos + connection.offset;
        const bool connects =
            canRedstoneWireAttachToAt(world, planarNeighbor, part.channelId, part.facing) ||
            hasSameCellRedstoneWireConnection(world, pos, part.channelId, part.facing, connection.offset) ||
            hasOuterCornerRedstoneWireConnection(world, pos, part.channelId, part.facing, connection.offset);
        if (connects) {
            connections |= wireConnectionBitForOffset(part.facing, connection.offset);
        }
    }
    return connections;
}

void requireHorizontalConnectionProperties() {
    if (PropIndices::NORTH == PropIndices::INVALID ||
        PropIndices::SOUTH == PropIndices::INVALID ||
        PropIndices::EAST == PropIndices::INVALID ||
        PropIndices::WEST == PropIndices::INVALID ||
        PropIndices::NORTH_TRUE == PropIndices::INVALID ||
        PropIndices::NORTH_FALSE == PropIndices::INVALID ||
        PropIndices::SOUTH_TRUE == PropIndices::INVALID ||
        PropIndices::SOUTH_FALSE == PropIndices::INVALID ||
        PropIndices::EAST_TRUE == PropIndices::INVALID ||
        PropIndices::EAST_FALSE == PropIndices::INVALID ||
        PropIndices::WEST_TRUE == PropIndices::INVALID ||
        PropIndices::WEST_FALSE == PropIndices::INVALID ||
        PropIndices::NORTH_NONE == PropIndices::INVALID ||
        PropIndices::NORTH_SIDE == PropIndices::INVALID ||
        PropIndices::SOUTH_NONE == PropIndices::INVALID ||
        PropIndices::SOUTH_SIDE == PropIndices::INVALID ||
        PropIndices::EAST_NONE == PropIndices::INVALID ||
        PropIndices::EAST_SIDE == PropIndices::INVALID ||
        PropIndices::WEST_NONE == PropIndices::INVALID ||
        PropIndices::WEST_SIDE == PropIndices::INVALID) {
        failWorld("Horizontal connection updates require north/south/east/west connection properties");
    }
}

void requireStairShapeProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::HALF == PropIndices::INVALID ||
        PropIndices::SHAPE == PropIndices::INVALID ||
        PropIndices::SHAPE_STRAIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_INNER_RIGHT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_LEFT == PropIndices::INVALID ||
        PropIndices::SHAPE_OUTER_RIGHT == PropIndices::INVALID) {
        failWorld("Stair shape updates require facing, half, and shape properties");
    }
}

glm::ivec3 offsetForHorizontalFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    failWorld("Stair shape updates require a horizontal facing value");
}

uint16_t oppositeHorizontalFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return PropIndices::FACING_WEST;
    }
    if (facing == PropIndices::FACING_WEST) {
        return PropIndices::FACING_EAST;
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return PropIndices::FACING_NORTH;
    }
    if (facing == PropIndices::FACING_NORTH) {
        return PropIndices::FACING_SOUTH;
    }
    failWorld("Stair shape updates require a horizontal facing value");
}

uint16_t counterClockwiseHorizontalFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return PropIndices::FACING_NORTH;
    }
    if (facing == PropIndices::FACING_NORTH) {
        return PropIndices::FACING_WEST;
    }
    if (facing == PropIndices::FACING_WEST) {
        return PropIndices::FACING_SOUTH;
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return PropIndices::FACING_EAST;
    }
    failWorld("Stair shape updates require a horizontal facing value");
}

bool arePerpendicularHorizontalFacings(const uint16_t a, const uint16_t b) {
    const bool aEastWest = a == PropIndices::FACING_EAST || a == PropIndices::FACING_WEST;
    const bool bEastWest = b == PropIndices::FACING_EAST || b == PropIndices::FACING_WEST;
    if ((aEastWest || a == PropIndices::FACING_NORTH || a == PropIndices::FACING_SOUTH) &&
        (bEastWest || b == PropIndices::FACING_NORTH || b == PropIndices::FACING_SOUTH)) {
        return aEastWest != bEastWest;
    }
    failWorld("Stair shape updates require horizontal facing values");
}

bool isWithinChunkRenderDistance(const int cx,
                                 const int cz,
                                 const int playerChunkX,
                                 const int playerChunkZ,
                                 const int renderDistance) {
    const int dx = cx - playerChunkX;
    const int dz = cz - playerChunkZ;
    return dx * dx + dz * dz <= renderDistance * renderDistance;
}

bool rayIntersectsAabb(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& boxMin,
                       const glm::vec3& boxMax,
                       const float maxDist,
                       float& tHit,
                       glm::ivec3& normal) {
    constexpr float kEpsilon = 1e-6f;
    float tMin = 0.0f;
    float tMax = maxDist;
    glm::ivec3 enterNormal(0);

    const auto testAxis = [&](const float origin,
                              const float dir,
                              const float minValue,
                              const float maxValue,
                              const glm::ivec3& negNormal,
                              const glm::ivec3& posNormal) {
        if (std::abs(dir) < kEpsilon) {
            return origin >= minValue && origin <= maxValue;
        }

        float t1 = (minValue - origin) / dir;
        float t2 = (maxValue - origin) / dir;
        glm::ivec3 axisNormal = negNormal;
        if (t1 > t2) {
            std::swap(t1, t2);
            axisNormal = posNormal;
        }

        if (t1 > tMin) {
            tMin = t1;
            enterNormal = axisNormal;
        }
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!testAxis(rayOrigin.x, rayDir.x, boxMin.x, boxMax.x,
                  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0))) {
        return false;
    }
    if (!testAxis(rayOrigin.y, rayDir.y, boxMin.y, boxMax.y,
                  glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0))) {
        return false;
    }
    if (!testAxis(rayOrigin.z, rayDir.z, boxMin.z, boxMax.z,
                  glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1))) {
        return false;
    }

    if (tMax < 0.0f || tMin > maxDist) {
        return false;
    }

    tHit = std::max(0.0f, tMin);
    normal = (tMin > 0.0f) ? enterNormal : glm::ivec3(0);
    return true;
}
}
void World::init(uint32_t seed) {
    m_seed = seed;
    m_terrainGen.init(seed, m_flatSurfaceY);
    m_chunks.clear();
    m_loadQueue.clear();
    m_generationInFlight.clear();
    {
        std::lock_guard<std::mutex> lock(m_completedGenMutex);
        m_completedGenQueue.clear();
    }
    m_fluidSystem.reset();
    m_neighborUpdateQueue.clear();
    m_redstoneUpdateQueue.clear();
    m_redstoneChangedBlockQueue.clear();
    m_redstoneScheduledUpdateQueue.clear();
    m_redstoneRuntimeState.clear();
    m_wireContainerParts.clear();
    m_ticketManager.reset();
    m_ticketManager.setViewRadius(m_renderDistance);
    m_ticketManager.setSimulationRadius(8);
    ++m_activeChunkRevision;
    ++m_blockContentRevision;
    m_lightService = std::make_unique<LightService>(*this);
    m_lightService->setLightChangeCallback(m_lightChangeCallback);
    m_lightService->start(m_threadPool);
    m_interactiveLightFlushRequested = false;
    m_dayNightSystem.setTimeOfDay(300.0f); // Default to mid-day
}

void World::update(const glm::vec3& playerPos, const float dt) {
    updateStreaming(playerPos,
                    dt,
                    kMaxChunkLoadSubmitsPerFrame,
                    kMaxGenerationInFlight,
                    kMaxChunkLoadFinalizesPerFrame,
                    kChunkLoadFinalizeTimeBudgetMs,
                    -1,
                    -1,
                    -1.0f);
}

void World::updateForInitialLoad(const glm::vec3& playerPos, const float dt) {
    updateStreaming(playerPos,
                    dt,
                    24,
                    std::max(8, m_threadPool ? m_threadPool->numWorkers() * 2 : 8),
                    12,
                    8.0,
                    24,
                    24,
                    4.0f);
}

void World::flushInteractiveLighting(const glm::vec3& playerPos) {
    if (!m_lightService || !m_interactiveLightFlushRequested) {
        return;
    }

    constexpr int kInteractiveLightJobBudget = 4;
    constexpr int kInteractiveLightMergeBudget = 16;
    constexpr float kInteractiveLightMergeTimeBudgetMs = 2.0f;
    m_lightService->processInteractiveJobsInline(playerPos,
                                                 kInteractiveLightJobBudget,
                                                 kInteractiveLightMergeBudget,
                                                 kInteractiveLightMergeTimeBudgetMs);
    m_interactiveLightFlushRequested = m_lightService->countPendingInteractiveJobs() > 0;
}

void World::updateStreaming(const glm::vec3& playerPos,
                            const float dt,
                            const int submitBudget,
                            const int maxGenerationInFlight,
                            const int finalizeBudget,
                            const double finalizeTimeBudgetMs,
                            const int lightSubmitBudgetOverride,
                            const int lightMergeBudgetOverride,
                            const float lightMergeTimeBudgetMsOverride) {
    m_dayNightSystem.update(dt);
    m_weatherSystem.update(dt);

    const int playerChunkX = worldToChunkCoord(static_cast<int>(std::floor(playerPos.x)), Chunk::SIZE_X);
    const int playerChunkZ = worldToChunkCoord(static_cast<int>(std::floor(playerPos.z)), Chunk::SIZE_Z);

    // Update ticket manager with player position
    m_ticketManager.updatePlayerPosition(playerChunkX, playerChunkZ);

    // Unload chunks outside unload radius (with hysteresis)
    std::vector<int64_t> toUnload;
    for (const auto& pair : m_chunks) {
        const int cx = static_cast<int>(pair.first >> 32);
        const int cz = static_cast<int>(static_cast<int32_t>(pair.first & 0xFFFFFFFF));
        if (m_ticketManager.shouldUnload(cx, cz)) {
            toUnload.push_back(pair.first);
        }
    }
    for (const int64_t key : toUnload) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
        unloadChunk(cx, cz);
    }

    // Get chunks to load from ticket manager (sorted by distance)
    // Build set of already loaded + in-flight chunks
    std::unordered_set<int64_t> loadedKeys;
    for (const auto& pair : m_chunks) {
        loadedKeys.insert(pair.first);
    }
    for (const int64_t key : m_generationInFlight) {
        loadedKeys.insert(key);
    }

    const auto chunksToLoad = m_ticketManager.getChunksToLoad(
        submitBudget * 4,  // Look ahead a bit for prioritization
        loadedKeys);

    // Submit chunk generation jobs from the prioritized list
    int submitted = 0;
    for (const auto& pos : chunksToLoad) {
        if (submitted >= submitBudget) {
            break;
        }
        if (static_cast<int>(m_generationInFlight.size()) >= maxGenerationInFlight) {
            break;
        }

        submitChunkLoad(pos.x, pos.y);
        ++submitted;
    }

    // Finalize completed generation results on the main thread with a small
    // frame budget. Neighbor linking and initial light queuing can dirty many
    // chunks, so batching every completed generation result at once causes
    // visible frame spikes while moving into new terrain.
    {
        std::vector<save::ChunkLoadData> completed;
        {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            completed.swap(m_completedGenQueue);
        }
        const auto finalizeStart = std::chrono::steady_clock::now();
        std::vector<save::ChunkLoadData> deferred;
        deferred.reserve(completed.size());

        int finalized = 0;
        for (auto& loadData : completed) {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - finalizeStart).count();
            if (finalized >= finalizeBudget ||
                elapsedMs >= finalizeTimeBudgetMs) {
                deferred.push_back(std::move(loadData));
                continue;
            }

            if (!loadData.chunk) {
                continue;
            }

            const int64_t key = chunkKey(loadData.chunk->m_chunkX, loadData.chunk->m_chunkZ);
            m_generationInFlight.erase(key);
            finalizeChunkLoad(std::move(loadData));
            ++finalized;
        }

        if (!deferred.empty()) {
            std::lock_guard<std::mutex> lock(m_completedGenMutex);
            m_completedGenQueue.insert(m_completedGenQueue.begin(),
                                       std::make_move_iterator(deferred.begin()),
                                       std::make_move_iterator(deferred.end()));
        }
    }

    if (m_lightService) {
        const int dirtyCount = m_lightService->countDirtyChunks();
        const int completedDepth = m_lightService->completedCount();

        // Scale submit budget with load, back off when the completed queue is deep.
        int lightSubmitBudget = 6;
        if (completedDepth > 48) {
            lightSubmitBudget = 0;           // backpressure: let drain catch up
        } else if (dirtyCount > 50) {
            lightSubmitBudget = 10;          // many dirty chunks, increase throughput
        } else if (dirtyCount < 5) {
            lightSubmitBudget = 3;           // low load, conserve resources
        }

        // Drain more aggressively when results are piling up.
        int mergeBudget = (completedDepth > 32) ? 12 : 6;
        float mergeTimeBudgetMs = (completedDepth > 32) ? 1.5f : 0.75f;

        if (lightSubmitBudgetOverride >= 0) {
            lightSubmitBudget = lightSubmitBudgetOverride;
        }
        if (lightMergeBudgetOverride >= 0) {
            mergeBudget = lightMergeBudgetOverride;
        }
        if (lightMergeTimeBudgetMsOverride >= 0.0f) {
            mergeTimeBudgetMs = lightMergeTimeBudgetMsOverride;
        }

        m_lightService->submitJobs(playerPos, lightSubmitBudget);
        m_lightService->drainCompleted(*this, mergeBudget, mergeTimeBudgetMs);
    }
}

BlockStateId World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return NULL_BLOCK_STATE;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        int localX = x - chunkX * Chunk::SIZE_X;
        int localZ = z - chunkZ * Chunk::SIZE_Z;
        return it->second->getBlock(localX, y, localZ);
    }
    return NULL_BLOCK_STATE;
}

uint8_t World::getPackedLight(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return 0;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return 0;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    return it->second->getPackedLight(localX, y, localZ);
}

BlockStateId World::getBlockState(const int x, const int y, const int z) const {
    return getBlock(x, y, z);
}

BlockStateId World::getFluidState(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return NULL_BLOCK_STATE;

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return NULL_BLOCK_STATE;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    const int scy = Chunk::toSubChunkIndex(y);
    const SubChunk* sc = it->second->getSubChunk(scy);
    if (!sc) {
        return NULL_BLOCK_STATE;
    }

    // First check the dedicated fluid layer
    const int localY = Chunk::toSubChunkLocalY(y);
    const BlockStateId fluidLayer = sc->getFluidLayer(localX, localY, localZ);
    if (fluidLayer != NULL_BLOCK_STATE) {
        return fluidLayer;
    }

    // Pure fluid cells store the fluid state directly in the block layer.
    return FluidState::getFluidState(sc->getBlock(localX, localY, localZ));
}

FluidCellView World::getCombinedCell(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) return {};

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return {};
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    const int scy = Chunk::toSubChunkIndex(y);
    const SubChunk* sc = it->second->getSubChunk(scy);
    if (!sc) {
        return {};
    }

    const int localY = Chunk::toSubChunkLocalY(y);
    const BlockStateId blockState = sc->getBlock(localX, localY, localZ);
    const BlockStateId fluidLayer = sc->getFluidLayer(localX, localY, localZ);

    const DecodedFluid blockFluid = FluidState::decode(blockState);
    if (blockFluid.kind != FluidKind::None) {
        // Pure fluid position (block layer IS the fluid)
        return FluidCellView{NULL_BLOCK_STATE, blockState};
    }
    // Block position (possibly waterlogged)
    return FluidCellView{blockState, fluidLayer};
}

BlockStateId World::sampleGeneratedBlock(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return NULL_BLOCK_STATE;
    }

    return m_terrainGen.sampleBlock(x, y, z);
}

void World::setBlock(int x, int y, int z, BlockID id) {
    setBlockState(x, y, z, BlockStateRegistry::getDefaultState(id));
}

void World::setFluidState(const int x, const int y, const int z, const BlockStateId stateId) {
    if (y < 0 || y >= Chunk::SIZE_Y) return;
    const BlockStateId normalizedStateId = normalizeFluidBlockState(stateId);

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    Chunk& chunk = *it->second;
    const int scy = Chunk::toSubChunkIndex(y);
    SubChunk* sc = chunk.getOrCreateSubChunk(scy);
    if (!sc) {
        return;
    }

    const int localY = Chunk::toSubChunkLocalY(y);
    const DecodedFluid newFluid = FluidState::decode(normalizedStateId);
    const BlockStateId currentBlock = sc->getBlock(localX, localY, localZ);
    const DecodedFluid currentBlockFluid = FluidState::decode(currentBlock);

    if (currentBlockFluid.kind != FluidKind::None) {
        // Current block layer IS fluid (pure water position).
        // If new state is also fluid of same kind, update block layer directly.
        // If new state is air/no-fluid, clear block layer to air.
        const BlockStateId targetBlockState = (newFluid.kind != FluidKind::None)
            ? normalizedStateId
            : NULL_BLOCK_STATE;
        setBlockState(x, y, z, targetBlockState);
        return;
    }

    // Block layer is a real block (possibly waterlogged).
    if (newFluid.kind == FluidKind::None) {
        // Removing fluid from this cell
        const BlockStateId oldFluid = sc->getFluidLayer(localX, localY, localZ);
        if (oldFluid == NULL_BLOCK_STATE) {
            return;  // Nothing to do
        }
        sc->setFluidLayer(localX, localY, localZ, NULL_BLOCK_STATE);
    } else {
        // Adding/updating fluid in a waterlogged cell
        if (BlockRegistry::getFast(BlockStateRegistry::getBlockId(currentBlock)).allowsFluidCoexistence) {
            sc->setFluidLayer(localX, localY, localZ, normalizedStateId);
        } else {
            // Block doesn't allow fluid coexistence — replace the block with fluid
            setBlockState(x, y, z, normalizedStateId);
            return;
        }
    }

    // Mark dirty for remesh
    ++m_blockContentRevision;
    markChunkSubChunkAndVerticalNeighborsDirty(chunk, scy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, scy, localY);
    }

    m_fluidSystem.onBlockChanged(glm::ivec3(x, y, z));

    // Notify block change callback for waterlogged fluid changes
    // (pure fluid and block-replacement paths go through setBlockState, which already fires the callback)
    if (m_blockChangeCallback) {
        const BlockStateId currentBlockState = sc->getBlock(localX, localY, localZ);
        m_blockChangeCallback(x, y, z, currentBlockState);
    }

    // Mark chunk dirty for persistence
    markChunkSaveDirty(chunkX, chunkZ);
}

bool World::isChunkLoadedForBlock(const int x, const int y, const int z) const {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return false;
    }

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    return m_chunks.find(chunkKey(chunkX, chunkZ)) != m_chunks.end();
}

void World::setBlockState(int x, int y, int z, BlockStateId id) {
    if (y < 0 || y >= Chunk::SIZE_Y) return;
    id = normalizeFluidBlockState(id);

    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);

    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const int localX = x - chunkX * Chunk::SIZE_X;
    const int localZ = z - chunkZ * Chunk::SIZE_Z;
    Chunk& chunk = *it->second;

    const int editedScy = Chunk::toSubChunkIndex(y);
    const int localY = Chunk::toSubChunkLocalY(y);
    const SubChunk* existingSubChunk = chunk.getSubChunk(editedScy);
    const BlockStateId oldId = chunk.getBlock(localX, y, localZ);
    const BlockStateId oldFluidLayer = existingSubChunk
        ? existingSubChunk->getFluidLayer(localX, localY, localZ)
        : NULL_BLOCK_STATE;

    BlockStateId targetState = id;
    const bool uncoverFluidLayer =
        id == NULL_BLOCK_STATE &&
        oldFluidLayer != NULL_BLOCK_STATE &&
        FluidState::decode(oldId).kind == FluidKind::None &&
        FluidState::decode(oldFluidLayer).kind != FluidKind::None;
    if (uncoverFluidLayer) {
        targetState = oldFluidLayer;
    }

    const bool clearFluidLayer =
        oldFluidLayer != NULL_BLOCK_STATE &&
        !uncoverFluidLayer &&
        !canBlockStateKeepFluidLayer(targetState, oldFluidLayer);
    const bool blockStateChanges = oldId != targetState;

    if (!blockStateChanges && !uncoverFluidLayer && !clearFluidLayer) {
        return;
    }

    if (blockStateChanges) {
        if (m_lightService) {
            chunk.setBlockWithoutMeshDirty(localX, y, localZ, targetState);
            m_lightService->onBlockChanged(x, y, z, oldId, targetState);
            m_interactiveLightFlushRequested = true;
        } else {
            chunk.setBlock(localX, y, localZ, targetState);
        }

        if (isRedstoneTorchRuntimeState(oldId) && !isRedstoneTorchRuntimeState(targetState)) {
            m_redstoneRuntimeState.eraseTorch(glm::ivec3(x, y, z));
        }
        if (isWireContainerState(oldId) && !isWireContainerState(targetState)) {
            m_wireContainerParts.erase(glm::ivec3(x, y, z));
        }
    }

    if (uncoverFluidLayer || clearFluidLayer) {
        if (SubChunk* sc = chunk.getSubChunk(editedScy)) {
            sc->setFluidLayer(localX, localY, localZ, NULL_BLOCK_STATE);
        }
    }


    // Geometry edits must always trigger remesh, regardless of lighting pipeline.
    ++m_blockContentRevision;
    markChunkSubChunkAndVerticalNeighborsDirty(chunk, editedScy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }

    m_fluidSystem.onBlockChanged(glm::ivec3(x, y, z),
                                 clearFluidLayer || changesFluidPathing(oldId, targetState));

    // Enqueue the edited cell and its 6 neighbors for support-rule validation.
    // The edited cell matters for gravity blocks placed directly into an
    // unsupported position; neighbors matter when this edit removes support.
    static constexpr glm::ivec3 kNeighborOffsets[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1},
    };
    m_neighborUpdateQueue.enqueue(glm::ivec3(x, y, z));
    m_redstoneUpdateQueue.enqueue(glm::ivec3(x, y, z));
    m_redstoneChangedBlockQueue.enqueue(glm::ivec3(x, y, z));
    for (const auto& off : kNeighborOffsets) {
        m_neighborUpdateQueue.enqueue(glm::ivec3(x, y, z) + off);
        m_redstoneUpdateQueue.enqueue(glm::ivec3(x, y, z) + off);
    }
    forEachWireOuterCornerPeerPosition(glm::ivec3(x, y, z), [this](const glm::ivec3& peer) {
        m_redstoneUpdateQueue.enqueue(peer);
    });
    forEachWireOuterCornerPositionBlockedBy(glm::ivec3(x, y, z), [this](const glm::ivec3& wirePosition) {
        m_redstoneUpdateQueue.enqueue(wirePosition);
    });
    // Notify block change callback (used by GameServer for BlockUpdateBatch)
    if (m_blockChangeCallback) {
        m_blockChangeCallback(x, y, z, targetState);
    }

    // Mark chunk dirty for persistence
    markChunkSaveDirty(chunkX, chunkZ);
    refreshConnectedBlocksAround(glm::ivec3(x, y, z));
}

void World::notifyWireContainerPartsChanged(const glm::ivec3& pos) {
    if (pos.y < 0 || pos.y >= Chunk::SIZE_Y) {
        return;
    }

    const int chunkX = worldToChunkCoord(pos.x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(pos.z, Chunk::SIZE_Z);
    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const BlockStateId currentState = getBlockState(pos.x, pos.y, pos.z);
    if (!isWireContainerState(currentState)) {
        failWorld("Wire container parts changed at a non-container block");
    }

    const int localX = pos.x - chunkX * Chunk::SIZE_X;
    const int localZ = pos.z - chunkZ * Chunk::SIZE_Z;
    const int editedScy = Chunk::toSubChunkIndex(pos.y);
    const int localY = Chunk::toSubChunkLocalY(pos.y);
    Chunk& chunk = *it->second;

    ++m_blockContentRevision;
    if (m_wireContainerChangeCallback) {
        m_wireContainerChangeCallback(pos);
    }
    markChunkSubChunkAndVerticalNeighborsDirty(chunk, editedScy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }

    static constexpr glm::ivec3 kNeighborOffsets[6] = {
        { 1,  0,  0}, {-1,  0,  0},
        { 0,  1,  0}, { 0, -1,  0},
        { 0,  0,  1}, { 0,  0, -1},
    };
    m_redstoneUpdateQueue.enqueue(pos);
    m_redstoneChangedBlockQueue.enqueue(pos);
    for (const glm::ivec3& offset : kNeighborOffsets) {
        m_redstoneUpdateQueue.enqueue(pos + offset);
    }
    forEachWireOuterCornerPeerPosition(pos, [this](const glm::ivec3& peer) {
        m_redstoneUpdateQueue.enqueue(peer);
    });
    markChunkSaveDirty(chunkX, chunkZ);
    refreshConnectedBlocksAround(pos);
}

void World::refreshConnectedBlockAt(const glm::ivec3& pos) {
    if (pos.y < 0 || pos.y >= Chunk::SIZE_Y) {
        return;
    }

    const BlockStateId currentState = getBlockState(pos.x, pos.y, pos.z);
    if (currentState == NULL_BLOCK_STATE) {
        return;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(currentState);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    const bool isConnectedBlock = isConnectedBlockDef(def);
    const bool isStairsBlock = isStairsBlockDef(def);
    const bool isRedstoneWireBlock = isRedstoneWireState(currentState);
    const bool isWireContainerBlock = isWireContainerState(currentState);
    if (!isConnectedBlock && !isStairsBlock && !isRedstoneWireBlock && !isWireContainerBlock) {
        return;
    }

    if (isWireContainerBlock) {
        WireContainerParts* parts = m_wireContainerParts.findMutable(pos);
        if (parts == nullptr) {
            return;
        }

        bool changed = false;
        parts->forEachMutable([&](WirePart& part) {
            const uint8_t refreshedConnections = refreshedWireContainerConnections(*this, pos, part);
            if (part.connections != refreshedConnections) {
                part.connections = refreshedConnections;
                changed = true;
            }
        });
        if (changed) {
            notifyWireContainerPartsChanged(pos);
        }
        return;
    }

    BlockStateId updatedState = currentState;
    if (isConnectedBlock) {
        requireHorizontalConnectionProperties();

        updatedState = BlockStateRegistry::withProperty(
            updatedState,
            PropIndices::NORTH,
            canConnectedBlockAttachTo(getBlockState(pos.x, pos.y, pos.z - 1))
                ? PropIndices::NORTH_TRUE
                : PropIndices::NORTH_FALSE);
        updatedState = BlockStateRegistry::withProperty(
            updatedState,
            PropIndices::SOUTH,
            canConnectedBlockAttachTo(getBlockState(pos.x, pos.y, pos.z + 1))
                ? PropIndices::SOUTH_TRUE
                : PropIndices::SOUTH_FALSE);
        updatedState = BlockStateRegistry::withProperty(
            updatedState,
            PropIndices::EAST,
            canConnectedBlockAttachTo(getBlockState(pos.x + 1, pos.y, pos.z))
                ? PropIndices::EAST_TRUE
                : PropIndices::EAST_FALSE);
        updatedState = BlockStateRegistry::withProperty(
            updatedState,
            PropIndices::WEST,
            canConnectedBlockAttachTo(getBlockState(pos.x - 1, pos.y, pos.z))
                ? PropIndices::WEST_TRUE
                : PropIndices::WEST_FALSE);
    } else if (isRedstoneWireBlock) {
        requireHorizontalConnectionProperties();
        const uint16_t wireChannelId = def.redstoneWireChannelId;
        if (wireChannelId == 0) {
            failWorld("Redstone wire block is missing redstoneWireChannelId");
        }
        const uint16_t wireFacing = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::FACING);
        if (!WireFaceGeometry::isWireFacing(wireFacing)) {
            failWorld("Redstone wire connection update requires a supported facing value");
        }

        if (wireFacing == PropIndices::FACING_FLOOR) {
            for (const WireFaceGeometry::ConnectionDirection& connection :
                 WireFaceGeometry::connectionDirections(wireFacing)) {
                updatedState = BlockStateRegistry::withProperty(
                    updatedState,
                    connection.property,
                    redstoneWireConnectionValueAt(
                        *this,
                        pos,
                        wireChannelId,
                        PropIndices::FACING_FLOOR,
                        connection));
            }
        } else {
            for (const WireFaceGeometry::ConnectionDirection& connection :
                 WireFaceGeometry::connectionDirections(wireFacing)) {
                updatedState = BlockStateRegistry::withProperty(
                    updatedState,
                    connection.property,
                    redstoneWireConnectionValueAt(
                        *this,
                        pos,
                        wireChannelId,
                        wireFacing,
                        connection));
            }
        }
    } else if (isStairsBlock) {
        requireStairShapeProperties();

        const uint16_t currentFacing = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::FACING);
        const uint16_t currentHalf = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::HALF);
        const uint16_t currentShape = BlockStateRegistry::getPropertyIndex(currentState, PropIndices::SHAPE);
        if (currentFacing == BlockStateRegistry::INVALID_INDEX ||
            currentHalf == BlockStateRegistry::INVALID_INDEX ||
            currentShape == BlockStateRegistry::INVALID_INDEX) {
            failWorld("Stair shape updates require state facing, half, and shape values");
        }

        const auto isStairWithSameHalf = [&](const BlockStateId state) {
            if (state == NULL_BLOCK_STATE) {
                return false;
            }
            const BlockID otherBlockId = BlockStateRegistry::getBlockId(state);
            const BlockDef& otherDef = BlockRegistry::getFast(otherBlockId);
            if (!isStairsBlockDef(otherDef)) {
                return false;
            }
            return BlockStateRegistry::getPropertyIndex(state, PropIndices::HALF) == currentHalf;
        };
        const auto canUseShapeSide = [&](const uint16_t sideFacing) {
            const glm::ivec3 offset = offsetForHorizontalFacing(sideFacing);
            const BlockStateId sideState = getBlockState(pos.x + offset.x, pos.y, pos.z + offset.z);
            if (!isStairWithSameHalf(sideState)) {
                return true;
            }
            return BlockStateRegistry::getPropertyIndex(sideState, PropIndices::FACING) != currentFacing;
        };
        const auto stairNeighborFacing = [&](const glm::ivec3& offset, uint16_t& neighborFacing) {
            const BlockStateId neighborState = getBlockState(pos.x + offset.x, pos.y, pos.z + offset.z);
            if (!isStairWithSameHalf(neighborState)) {
                return false;
            }
            neighborFacing = BlockStateRegistry::getPropertyIndex(neighborState, PropIndices::FACING);
            if (neighborFacing == BlockStateRegistry::INVALID_INDEX) {
                failWorld("Stair shape updates require neighbor facing values");
            }
            return arePerpendicularHorizontalFacings(currentFacing, neighborFacing);
        };

        uint16_t shapeValue = PropIndices::SHAPE_STRAIGHT;
        uint16_t neighborFacing = PropIndices::INVALID;
        const glm::ivec3 frontOffset = offsetForHorizontalFacing(currentFacing);
        if (stairNeighborFacing(frontOffset, neighborFacing) &&
            canUseShapeSide(oppositeHorizontalFacing(neighborFacing))) {
            shapeValue = (neighborFacing == counterClockwiseHorizontalFacing(currentFacing))
                ? PropIndices::SHAPE_OUTER_LEFT
                : PropIndices::SHAPE_OUTER_RIGHT;
        } else if (stairNeighborFacing(-frontOffset, neighborFacing) &&
                   canUseShapeSide(neighborFacing)) {
            shapeValue = (neighborFacing == counterClockwiseHorizontalFacing(currentFacing))
                ? PropIndices::SHAPE_INNER_LEFT
                : PropIndices::SHAPE_INNER_RIGHT;
        }

        updatedState = BlockStateRegistry::withProperty(updatedState, PropIndices::SHAPE, shapeValue);
    }

    if (updatedState == currentState) {
        return;
    }

    const int chunkX = worldToChunkCoord(pos.x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(pos.z, Chunk::SIZE_Z);
    auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it == m_chunks.end()) {
        return;
    }

    const int localX = pos.x - chunkX * Chunk::SIZE_X;
    const int localZ = pos.z - chunkZ * Chunk::SIZE_Z;
    const int editedScy = Chunk::toSubChunkIndex(pos.y);
    const int localY = Chunk::toSubChunkLocalY(pos.y);
    Chunk& chunk = *it->second;

    if (m_lightService) {
        chunk.setBlockWithoutMeshDirty(localX, pos.y, localZ, updatedState);
    } else {
        chunk.setBlock(localX, pos.y, localZ, updatedState);
    }

    markChunkSubChunkAndVerticalNeighborsDirty(chunk, editedScy, localY);
    if (localX == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX - 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localX == Chunk::SIZE_X - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX + 1, chunkZ));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == 0) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ - 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        auto nit = m_chunks.find(chunkKey(chunkX, chunkZ + 1));
        if (nit != m_chunks.end()) markChunkSubChunkAndVerticalNeighborsDirty(*nit->second, editedScy, localY);
    }

    if (m_blockChangeCallback) {
        m_blockChangeCallback(pos.x, pos.y, pos.z, updatedState);
    }
    markChunkSaveDirty(chunkX, chunkZ);
}

void World::refreshConnectedBlocksAround(const glm::ivec3& pos) {
    // Offsets cover all positions whose redstone wire connection state may
    // change when the block at pos changes:
    //   - Self, 4 horizontal neighbors, and direct vertical neighbors.
    //   - Wires attached to pos as a support block and outer-corner peers are
    //     covered below.
    static constexpr std::array<glm::ivec3, 7> kRefreshOffsets = {{
        { 0,  0,  0},
        { 1,  0,  0},
        {-1,  0,  0},
        { 0,  0,  1},
        { 0,  0, -1},
        { 0,  1,  0},
        { 0, -1,  0},
    }};

    for (const glm::ivec3& offset : kRefreshOffsets) {
        refreshConnectedBlockAt(pos + offset);
    }
    forEachWirePositionOnSupport(pos, [this](const glm::ivec3& wirePosition) {
        refreshConnectedBlockAt(wirePosition);
    });
    forEachWireOuterCornerPeerPosition(pos, [this](const glm::ivec3& peer) {
        refreshConnectedBlockAt(peer);
    });
    forEachWireOuterCornerPositionBlockedBy(pos, [this](const glm::ivec3& wirePosition) {
        refreshConnectedBlockAt(wirePosition);
    });
}

void World::setThreadPool(ThreadPool* pool) {
    m_threadPool = pool;
    if (m_lightService) {
        m_lightService->shutdown();
        m_lightService->setLightChangeCallback(m_lightChangeCallback);
        m_lightService->start(m_threadPool);
    }
}

void World::setLightChangeCallback(LightChangeCallback callback) {
    m_lightChangeCallback = std::move(callback);
    if (m_lightService) {
        m_lightService->setLightChangeCallback(m_lightChangeCallback);
    }
}

// ---------------------------------------------------------------------------
// Save system integration
// ---------------------------------------------------------------------------

void World::setSaveManager(save::SaveManager* saveManager) {
    m_saveManager = saveManager;
}

void World::markChunkSaveDirty(int cx, int cz) {
    m_dirtySaveChunks.insert(chunkKey(cx, cz));
}

std::vector<save::WireContainerSaveEntry> World::collectWireContainersForChunk(const int cx, const int cz) const {
    std::vector<save::WireContainerSaveEntry> entries;
    m_wireContainerParts.forEach([&](const glm::ivec3& position, const WireContainerParts& parts) {
        if (worldToChunkCoord(position.x, Chunk::SIZE_X) != cx ||
            worldToChunkCoord(position.z, Chunk::SIZE_Z) != cz) {
            return;
        }
        if (parts.empty()) {
            failWorld("Cannot save an empty wire container part set");
        }
        const BlockStateId stateId = getBlockState(position.x, position.y, position.z);
        if (stateId == NULL_BLOCK_STATE ||
            !BlockRegistry::getFast(BlockStateRegistry::getBlockId(stateId)).isWireContainer) {
            failWorld("Cannot save wire container parts for a non-container block");
        }

        save::WireContainerSaveEntry entry;
        entry.position = position;
        entry.parts = parts;
        entries.push_back(entry);
    });
    return entries;
}

void World::applyLoadedWireContainers(const int cx,
                                      const int cz,
                                      const std::vector<save::WireContainerSaveEntry>& wireContainers) {
    eraseWireContainersInChunk(cx, cz);
    for (const save::WireContainerSaveEntry& entry : wireContainers) {
        if (worldToChunkCoord(entry.position.x, Chunk::SIZE_X) != cx ||
            worldToChunkCoord(entry.position.z, Chunk::SIZE_Z) != cz) {
            failWorld("Loaded wire container parts target a different chunk");
        }
        if (entry.parts.empty()) {
            failWorld("Loaded wire container parts are empty");
        }
        const BlockStateId stateId = getBlockState(entry.position.x, entry.position.y, entry.position.z);
        if (stateId == NULL_BLOCK_STATE ||
            !BlockRegistry::getFast(BlockStateRegistry::getBlockId(stateId)).isWireContainer) {
            failWorld("Loaded wire container parts target a non-container block");
        }
        if (m_wireContainerParts.find(entry.position) != nullptr) {
            failWorld("Loaded wire container parts contain a duplicate position");
        }
        m_wireContainerParts.getOrCreate(entry.position) = entry.parts;
    }
}

void World::eraseWireContainersInChunk(const int cx, const int cz) {
    std::vector<glm::ivec3> positions;
    m_wireContainerParts.forEach([&](const glm::ivec3& position, const WireContainerParts&) {
        if (worldToChunkCoord(position.x, Chunk::SIZE_X) == cx &&
            worldToChunkCoord(position.z, Chunk::SIZE_Z) == cz) {
            positions.push_back(position);
        }
    });
    for (const glm::ivec3& position : positions) {
        m_wireContainerParts.erase(position);
    }
}

void World::flushSaves() {
    if (!m_saveManager) return;

    // Submit all remaining dirty chunks for saving
    for (int64_t key : m_dirtySaveChunks) {
        int cx = static_cast<int>(key >> 32);
        int cz = static_cast<int>(static_cast<int32_t>(key & 0xFFFFFFFF));
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) {
            m_saveManager->submitSaveChunk(cx, cz, *it->second, collectWireContainersForChunk(cx, cz));
        }
    }
    m_dirtySaveChunks.clear();

    // Wait for all pending saves to complete
    m_saveManager->flushPendingSaves();
}

LightFrameStats World::getLightFrameStats() const {
    if (!m_lightService) {
        return {};
    }
    return m_lightService->getFrameStats();
}

RayHit World::raycast(const PhysicsInfo& ray, const float maxDist) const {
    return raycastWorldView(*this, ray, maxDist);
}

bool World::raycast(const PhysicsInfo& ray, const float maxDist, glm::ivec3& hitBlock, glm::ivec3& placeBlock) const {
    const RayHit hit = raycast(ray, maxDist);
    if (!hit.hit) {
        return false;
    }

    hitBlock = hit.blockPos;
    placeBlock = hit.blockPos + hit.normal;
    return true;
}

void World::setRenderDistance(int dist) {
    m_renderDistance = std::max(1, dist);
    m_ticketManager.setViewRadius(m_renderDistance);
}

void World::setSimulationDistance(int distance) {
    m_ticketManager.setSimulationRadius(std::max(1, distance));
}

int World::getSurfaceY(int x, int z) const {
    const int chunkX = worldToChunkCoord(x, Chunk::SIZE_X);
    const int chunkZ = worldToChunkCoord(z, Chunk::SIZE_Z);
    const auto it = m_chunks.find(chunkKey(chunkX, chunkZ));
    if (it != m_chunks.end()) {
        const int localX = x - chunkX * Chunk::SIZE_X;
        const int localZ = z - chunkZ * Chunk::SIZE_Z;
        for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
            if (it->second->getBlock(localX, y, localZ) != NULL_BLOCK_STATE) {
                return y;
            }
        }
        return 0;
    }

    return m_terrainGen.sampleSurfaceY(x, z);
}

TerrainBiome World::getBiome(int x, int z) const {
    return m_terrainGen.sampleBiome(x, z);
}

glm::ivec2 World::getChunkCoords(int worldX, int worldZ) const {
    return {
        worldToChunkCoord(worldX, Chunk::SIZE_X),
        worldToChunkCoord(worldZ, Chunk::SIZE_Z)
    };
}

World::ChunkLoadProgress World::getChunkLoadProgress(const glm::vec3& center) const {
    const int centerChunkX = worldToChunkCoord(static_cast<int>(std::floor(center.x)), Chunk::SIZE_X);
    const int centerChunkZ = worldToChunkCoord(static_cast<int>(std::floor(center.z)), Chunk::SIZE_Z);

    ChunkLoadProgress progress{};
    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > m_renderDistance * m_renderDistance) {
                continue;
            }

            ++progress.target;
            const int cx = centerChunkX + dx;
            const int cz = centerChunkZ + dz;
            const int64_t key = chunkKey(cx, cz);
            if (m_chunks.find(key) != m_chunks.end()) {
                ++progress.loaded;
            }
            if (m_generationInFlight.count(key) > 0) {
                ++progress.inFlight;
            }
        }
    }
    return progress;
}

const char* World::biomeToString(TerrainBiome biome) {
    switch (biome) {
        case TerrainBiome::Temperate:
            return "Temperate";
        case TerrainBiome::Arid:
            return "Arid";
        case TerrainBiome::Mountain:
            return "Mountain";
        case TerrainBiome::HighMountain:
            return "High Mountain";
        default:
            return "Unknown";
    }
}

int64_t World::chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | (static_cast<int64_t>(cz) & 0xFFFFFFFF);
}

void World::submitChunkLoad(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;
    if (m_generationInFlight.count(key)) return;
    if (!m_threadPool || !m_threadPool->isRunning()) {
        // Without a worker pool, load this chunk synchronously.
        loadChunk(cx, cz);
        return;
    }

    m_generationInFlight.insert(key);

    // Both paths push to m_completedGenQueue which is consumed by finalizeChunkLoad().
    auto chunk = std::make_shared<Chunk>(cx, cz);
    TerrainGenerator* terrainGen = &m_terrainGen;
    save::SaveManager* sm = m_saveManager;

    m_threadPool->submit([chunk, terrainGen, sm, this]() {
        bool loadedFromDisk = false;

        if (sm && sm->chunkFileExists(chunk->m_chunkX, chunk->m_chunkZ)) {
            save::ChunkLoadData loaded = sm->tryLoadChunkData(chunk->m_chunkX, chunk->m_chunkZ);
            if (loaded.chunk) {
                loaded.chunk->seedInitialLightMap();
                {
                    std::lock_guard<std::mutex> lock(m_completedGenMutex);
                    m_completedGenQueue.push_back(std::move(loaded));
                }
                loadedFromDisk = true;
            }
        }

        if (!loadedFromDisk) {
            terrainGen->generateChunk(*chunk);
            chunk->seedInitialLightMap();
            save::ChunkLoadData generated;
            generated.chunk = chunk;
            {
                std::lock_guard<std::mutex> lock(m_completedGenMutex);
                m_completedGenQueue.push_back(std::move(generated));
            }
        }
    }, 0);
}

void World::finalizeChunkLoad(save::ChunkLoadData loadData) {
    if (!loadData.chunk) {
        return;
    }
    std::shared_ptr<Chunk> chunk = std::move(loadData.chunk);
    const int cx = chunk->m_chunkX;
    const int cz = chunk->m_chunkZ;
    const int64_t key = chunkKey(cx, cz);

    // Guard against duplicate finalization (e.g. if loadChunk was called directly)
    if (m_chunks.find(key) != m_chunks.end()) return;

    m_chunks[key] = std::move(chunk);
    applyLoadedWireContainers(cx, cz, loadData.wireContainers);
    ++m_activeChunkRevision;
    ++m_blockContentRevision;

    // Wire up neighbor pointers for the new chunk and its existing neighbors
    Chunk* cur = m_chunks[key].get();
    auto markNeighborBorderDirty = [&](Chunk& neighbor) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Only mark dirty when both sides have content — if the new chunk
            // has no subchunk here, its border is all-air and the neighbor's
            // faces are already correct. If the neighbor has no subchunk, there
            // is nothing to remesh.
            if (cur->getSubChunk(scy) && neighbor.getSubChunk(scy)) {
                neighbor.markSubChunkDirty(scy);
            }
        }
    };
    auto linkNeighbor = [&](int ncx, int ncz, int selfIdx, int neighborIdx) {
        auto it = m_chunks.find(chunkKey(ncx, ncz));
        if (it == m_chunks.end()) {
            return;
        }

        Chunk* neighbor = it->second.get();
        cur->neighbors[selfIdx] = neighbor;
        neighbor->neighbors[neighborIdx] = cur;
        cur->linkExistingSubChunksWithNeighbor(selfIdx);
        markNeighborBorderDirty(*neighbor);
    };
    linkNeighbor(cx + 1, cz, 0, 1);
    linkNeighbor(cx - 1, cz, 1, 0);
    linkNeighbor(cx, cz + 1, 2, 3);
    linkNeighbor(cx, cz - 1, 3, 2);

    // Initialize lighting after terrain generation and neighbor linking
    if (m_lightService) {
        m_lightService->onChunkLoaded(m_chunks[key]);
    }
}

void World::loadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    if (m_chunks.find(key) != m_chunks.end()) return;

    save::ChunkLoadData loadData;
    if (m_saveManager && m_saveManager->chunkFileExists(cx, cz)) {
        loadData = m_saveManager->tryLoadChunkData(cx, cz);
    }
    if (!loadData.chunk) {
        loadData.chunk = std::make_shared<Chunk>(cx, cz);
        m_terrainGen.generateChunk(*loadData.chunk);
    }
    std::shared_ptr<Chunk> chunk = std::move(loadData.chunk);
    chunk->seedInitialLightMap();

    m_chunks[key] = std::move(chunk);
    applyLoadedWireContainers(cx, cz, loadData.wireContainers);
    ++m_activeChunkRevision;
    ++m_blockContentRevision;

    // Wire up neighbor pointers for the new chunk and its existing neighbors
    Chunk* cur = m_chunks[key].get();
    auto markNeighborBorderDirty = [&](Chunk& neighbor) {
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (cur->getSubChunk(scy) && neighbor.getSubChunk(scy)) {
                neighbor.markSubChunkDirty(scy);
            }
        }
    };
    auto linkNeighbor = [&](int ncx, int ncz, int selfIdx, int neighborIdx) {
        auto it = m_chunks.find(chunkKey(ncx, ncz));
        if (it == m_chunks.end()) {
            return;
        }

        Chunk* neighbor = it->second.get();
        cur->neighbors[selfIdx] = neighbor;
        neighbor->neighbors[neighborIdx] = cur;
        cur->linkExistingSubChunksWithNeighbor(selfIdx);
        markNeighborBorderDirty(*neighbor);
    };
    linkNeighbor(cx + 1, cz, 0, 1);
    linkNeighbor(cx - 1, cz, 1, 0);
    linkNeighbor(cx, cz + 1, 2, 3);
    linkNeighbor(cx, cz - 1, 3, 2);

    // Initialize lighting after terrain generation and neighbor linking
    if (m_lightService) {
        m_lightService->onChunkLoaded(m_chunks[key]);
    }

}

void World::unloadChunk(int cx, int cz) {
    int64_t key = chunkKey(cx, cz);
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;

    // Save dirty chunk to disk before unloading
    if (m_saveManager && m_dirtySaveChunks.count(key)) {
        m_saveManager->submitSaveChunk(cx, cz, *it->second, collectWireContainersForChunk(cx, cz));
        m_dirtySaveChunks.erase(key);
    }

    if (m_lightService) {
        m_lightService->onChunkUnloaded(it->first);
    }

    Chunk* chunk = it->second.get();
    for (int direction = 0; direction < 4; ++direction) {
        Chunk* neighbor = chunk->neighbors[direction];
        if (!neighbor) {
            continue;
        }

        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            // Only mark neighbor dirty if the unloading chunk had content at
            // this level that could have been hiding the neighbor's border faces.
            if (chunk->getSubChunk(scy) && neighbor->getSubChunk(scy)) {
                neighbor->markSubChunkDirty(scy);
            }
        }
        chunk->unlinkExistingSubChunksFromNeighbor(direction);
    }

    if (chunk->neighbors[0]) chunk->neighbors[0]->neighbors[1] = nullptr;
    if (chunk->neighbors[1]) chunk->neighbors[1]->neighbors[0] = nullptr;
    if (chunk->neighbors[2]) chunk->neighbors[2]->neighbors[3] = nullptr;
    if (chunk->neighbors[3]) chunk->neighbors[3]->neighbors[2] = nullptr;

    m_chunks.erase(it);
    eraseWireContainersInChunk(cx, cz);
    ++m_activeChunkRevision;
    ++m_blockContentRevision;
}


size_t World::getTotalVertexCount() const {
    size_t total = 0;
    for (const auto& pair : m_chunks) {
        if (pair.second) {
            for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                const SubChunk* sc = pair.second->getSubChunk(scy);
                if (sc) {
                    total += sc->getMesh().opaqueRange.vertexCount;
                    total += sc->getMesh().cutoutRange.vertexCount;
                    total += sc->getMesh().cutoutDistanceRange.vertexCount;
                    total += sc->getMesh().transparentRange.vertexCount;
                    total += sc->getMesh().waterRange.vertexCount;
                }
            }
        }
    }
    return total;
}

void World::updateLoadQueue(int playerChunkX, int playerChunkZ) {
    m_loadQueue.clear();

    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > m_renderDistance * m_renderDistance) {
                continue;
            }
            int cx = playerChunkX + dx;
            int cz = playerChunkZ + dz;
            const int64_t key = chunkKey(cx, cz);
            if (m_chunks.find(key) == m_chunks.end() && !m_generationInFlight.count(key)) {
                m_loadQueue.push_back(glm::ivec2(cx, cz));
            }
        }
    }

    std::sort(m_loadQueue.begin(), m_loadQueue.end(),
              [playerChunkX, playerChunkZ](const glm::ivec2& a, const glm::ivec2& b) {
                  int distA = (a.x - playerChunkX) * (a.x - playerChunkX) +
                              (a.y - playerChunkZ) * (a.y - playerChunkZ);
                  int distB = (b.x - playerChunkX) * (b.x - playerChunkX) +
                              (b.y - playerChunkZ) * (b.y - playerChunkZ);
                  return distA > distB;
              });
}
