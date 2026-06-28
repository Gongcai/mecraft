#include "Placement.h"

#include "BedBlock.h"
#include "BlockCollision.h"
#include "DoorBlock.h"
#include "PropIndices.h"
#include "../redstone/WireFaceGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

std::unordered_map<std::string, PlacementStrategyFn> PlacementStrategyRegistry::s_strategies;

void PlacementStrategyRegistry::registerStrategy(const std::string& name, PlacementStrategyFn fn) {
    s_strategies[name] = fn;
}

PlacementStrategyFn PlacementStrategyRegistry::getStrategy(const std::string& name) {
    const auto it = s_strategies.find(name);
    return (it != s_strategies.end()) ? it->second : nullptr;
}

namespace {

constexpr float kPlacementEpsilon = 0.0001f;

BlockStateId strategySimple(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

BlockStateId strategyAttachWall(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y == 1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_FLOOR);
    }
    if (n.y == -1) {
        return NULL_BLOCK_STATE;
    }
    if (n.z == -1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_NORTH);
    }
    if (n.z == 1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_SOUTH);
    }
    if (n.x == -1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_WEST);
    }
    if (n.x == 1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_EAST);
    }
    return NULL_BLOCK_STATE;
}

void requireWallFacePlanePlacementProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::FACING_NORTH == PropIndices::INVALID ||
        PropIndices::FACING_SOUTH == PropIndices::INVALID ||
        PropIndices::FACING_EAST == PropIndices::INVALID ||
        PropIndices::FACING_WEST == PropIndices::INVALID) {
        throw std::runtime_error("Wall face plane placement requires facing=north/south/east/west properties");
    }
}

void requireFloorFacePlanePlacementProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::FACING_FLOOR == PropIndices::INVALID) {
        throw std::runtime_error("Floor face plane placement requires facing=floor properties");
    }
}

void requireRedstoneWireFacePlacementProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::FACING_FLOOR == PropIndices::INVALID ||
        PropIndices::FACING_CEILING == PropIndices::INVALID ||
        PropIndices::FACING_NORTH == PropIndices::INVALID ||
        PropIndices::FACING_SOUTH == PropIndices::INVALID ||
        PropIndices::FACING_EAST == PropIndices::INVALID ||
        PropIndices::FACING_WEST == PropIndices::INVALID) {
        throw std::runtime_error("Redstone wire placement requires floor, ceiling, and wall facing values");
    }
}

void requireSixWayFacingPlacementProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::FACING_NORTH == PropIndices::INVALID ||
        PropIndices::FACING_SOUTH == PropIndices::INVALID ||
        PropIndices::FACING_EAST == PropIndices::INVALID ||
        PropIndices::FACING_WEST == PropIndices::INVALID ||
        PropIndices::FACING_UP == PropIndices::INVALID ||
        PropIndices::FACING_DOWN == PropIndices::INVALID) {
        throw std::runtime_error("Six-way placement requires facing=north/south/east/west/up/down properties");
    }
}

void requireHopperFacingPlacementProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::FACING_NORTH == PropIndices::INVALID ||
        PropIndices::FACING_SOUTH == PropIndices::INVALID ||
        PropIndices::FACING_EAST == PropIndices::INVALID ||
        PropIndices::FACING_WEST == PropIndices::INVALID ||
        PropIndices::FACING_DOWN == PropIndices::INVALID) {
        throw std::runtime_error("Hopper placement requires facing=north/south/east/west/down properties");
    }
}

uint16_t facePlaneFacingFromWallNormal(const glm::ivec3& normal) {
    if (normal.z == -1) {
        return PropIndices::FACING_NORTH;
    }
    if (normal.z == 1) {
        return PropIndices::FACING_SOUTH;
    }
    if (normal.x == -1) {
        return PropIndices::FACING_WEST;
    }
    if (normal.x == 1) {
        return PropIndices::FACING_EAST;
    }
    throw std::runtime_error("Wall face plane placement requires a horizontal hit normal");
}

uint16_t horizontalFacingFromYaw(const float playerYaw) {
    const float angle = std::fmod(playerYaw + 360.0f, 360.0f);

    if (angle >= 45.0f && angle < 135.0f) {
        return PropIndices::FACING_SOUTH;
    }
    if (angle >= 135.0f && angle < 225.0f) {
        return PropIndices::FACING_WEST;
    }
    if (angle >= 225.0f && angle < 315.0f) {
        return PropIndices::FACING_NORTH;
    }
    return PropIndices::FACING_EAST;
}

uint16_t facingFromSideNormal(const glm::ivec3& normal) {
    if (normal.x > 0) {
        return PropIndices::FACING_EAST;
    }
    if (normal.x < 0) {
        return PropIndices::FACING_WEST;
    }
    if (normal.z > 0) {
        return PropIndices::FACING_SOUTH;
    }
    if (normal.z < 0) {
        return PropIndices::FACING_NORTH;
    }
    throw std::runtime_error("Stair side placement requires a horizontal hit normal");
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
    throw std::runtime_error("Stair side placement requires a horizontal facing value");
}

uint16_t applyPlacementFacingRevert(const BlockID blockId, const uint16_t facing) {
    return BlockRegistry::get(blockId).revertPlacementFacing
        ? oppositeHorizontalFacing(facing)
        : facing;
}

uint16_t stairFacingFromSideNormal(const BlockID blockId, const glm::ivec3& normal) {
    return applyPlacementFacingRevert(blockId, facingFromSideNormal(normal));
}

float boxVolume(const BlockCollisionBox& box) {
    const glm::vec3 size = box.max - box.min;
    return size.x * size.y * size.z;
}

float totalBoxVolume(const std::vector<BlockCollisionBox>& boxes) {
    float volume = 0.0f;
    for (const BlockCollisionBox& box : boxes) {
        volume += boxVolume(box);
    }
    return volume;
}

bool boxesIntersect(const BlockCollisionBox& a, const BlockCollisionBox& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

bool anyBoxesIntersect(const std::vector<BlockCollisionBox>& a,
                       const std::vector<BlockCollisionBox>& b) {
    for (const BlockCollisionBox& aBox : a) {
        for (const BlockCollisionBox& bBox : b) {
            if (boxesIntersect(aBox, bBox)) {
                return true;
            }
        }
    }
    return false;
}

bool containsBox(const BlockCollisionBox& container, const BlockCollisionBox& box) {
    return container.min.x <= box.min.x + kPlacementEpsilon &&
           container.min.y <= box.min.y + kPlacementEpsilon &&
           container.min.z <= box.min.z + kPlacementEpsilon &&
           container.max.x + kPlacementEpsilon >= box.max.x &&
           container.max.y + kPlacementEpsilon >= box.max.y &&
           container.max.z + kPlacementEpsilon >= box.max.z;
}

bool boxesCoverAll(const std::vector<BlockCollisionBox>& containers,
                   const std::vector<BlockCollisionBox>& boxes) {
    for (const BlockCollisionBox& box : boxes) {
        bool covered = false;
        for (const BlockCollisionBox& container : containers) {
            if (containsBox(container, box)) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            return false;
        }
    }
    return true;
}

uint16_t halfFromHit(const PlacementContext& ctx) {
    if (PropIndices::HALF == PropIndices::INVALID ||
        PropIndices::HALF_TOP == PropIndices::INVALID ||
        PropIndices::HALF_BOTTOM == PropIndices::INVALID) {
        throw std::runtime_error("Half-block placement requires registered half=top/bottom properties");
    }

    uint16_t halfValue = PropIndices::HALF_BOTTOM;
    if (ctx.hitNormal.y < 0) {
        halfValue = PropIndices::HALF_TOP;
    } else if (ctx.hitNormal.y == 0) {
        const float localY = ctx.hitPosition.y - std::floor(ctx.hitPosition.y);
        halfValue = localY >= 0.5f ? PropIndices::HALF_TOP : PropIndices::HALF_BOTTOM;
    }

    if (ctx.isSneaking) {
        halfValue = (halfValue == PropIndices::HALF_TOP) ? PropIndices::HALF_BOTTOM : PropIndices::HALF_TOP;
    }
    return halfValue;
}

uint16_t halfFromHorizontalFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_NORTH) {
        return PropIndices::HALF_NORTH;
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return PropIndices::HALF_SOUTH;
    }
    if (facing == PropIndices::FACING_EAST) {
        return PropIndices::HALF_EAST;
    }
    if (facing == PropIndices::FACING_WEST) {
        return PropIndices::HALF_WEST;
    }
    throw std::runtime_error("Vertical slab placement requires a horizontal facing value");
}

uint16_t oppositeVerticalHalf(const uint16_t halfValue) {
    if (halfValue == PropIndices::HALF_NORTH) {
        return PropIndices::HALF_SOUTH;
    }
    if (halfValue == PropIndices::HALF_SOUTH) {
        return PropIndices::HALF_NORTH;
    }
    if (halfValue == PropIndices::HALF_EAST) {
        return PropIndices::HALF_WEST;
    }
    if (halfValue == PropIndices::HALF_WEST) {
        return PropIndices::HALF_EAST;
    }
    throw std::runtime_error("Vertical slab placement requires a vertical half value");
}

uint16_t verticalHalfFromHitPosition(const PlacementContext& ctx) {
    constexpr float kCenterEpsilon = 0.001f;
    const float localX = ctx.hitPosition.x - std::floor(ctx.hitPosition.x);
    const float localZ = ctx.hitPosition.z - std::floor(ctx.hitPosition.z);
    const float offsetX = localX - 0.5f;
    const float offsetZ = localZ - 0.5f;

    if (std::max(std::abs(offsetX), std::abs(offsetZ)) < kCenterEpsilon) {
        return halfFromHorizontalFacing(horizontalFacingFromYaw(ctx.playerYaw));
    }
    if (std::abs(offsetX) >= std::abs(offsetZ)) {
        return offsetX >= 0.0f ? PropIndices::HALF_EAST : PropIndices::HALF_WEST;
    }
    return offsetZ >= 0.0f ? PropIndices::HALF_SOUTH : PropIndices::HALF_NORTH;
}

uint16_t verticalHalfFromHit(const PlacementContext& ctx) {
    if (PropIndices::HALF == PropIndices::INVALID ||
        PropIndices::HALF_NORTH == PropIndices::INVALID ||
        PropIndices::HALF_SOUTH == PropIndices::INVALID ||
        PropIndices::HALF_EAST == PropIndices::INVALID ||
        PropIndices::HALF_WEST == PropIndices::INVALID) {
        throw std::runtime_error("Vertical slab placement requires registered horizontal half properties");
    }

    uint16_t halfValue = PropIndices::HALF_NORTH;
    if (ctx.hitNormal.y == 0) {
        halfValue = halfFromHorizontalFacing(oppositeHorizontalFacing(facingFromSideNormal(ctx.hitNormal)));
    } else {
        halfValue = verticalHalfFromHitPosition(ctx);
    }

    if (ctx.isSneaking) {
        halfValue = oppositeVerticalHalf(halfValue);
    }
    return halfValue;
}

BlockStateId strategyHorizontalFacing(const PlacementContext& ctx) {
    return BlockStateRegistry::getState(
        ctx.blockId,
        PropIndices::FACING,
        applyPlacementFacingRevert(ctx.blockId, horizontalFacingFromYaw(ctx.playerYaw)));
}

BlockStateId strategySixWayFacing(const PlacementContext& ctx) {
    requireSixWayFacingPlacementProperties();
    if (ctx.hitNormal.y > 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_UP);
    }
    if (ctx.hitNormal.y < 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_DOWN);
    }
    return BlockStateRegistry::getState(
        ctx.blockId,
        PropIndices::FACING,
        applyPlacementFacingRevert(ctx.blockId, horizontalFacingFromYaw(ctx.playerYaw)));
}

BlockStateId strategyHopperFacing(const PlacementContext& ctx) {
    requireHopperFacingPlacementProperties();
    if (ctx.hitNormal.y != 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_DOWN);
    }
    return BlockStateRegistry::getState(
        ctx.blockId,
        PropIndices::FACING,
        oppositeHorizontalFacing(facingFromSideNormal(ctx.hitNormal)));
}

BlockStateId strategyAxisOriented(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y != 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_Y);
    }
    if (n.x != 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_X);
    }
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_Z);
}

BlockStateId strategyStairs(const PlacementContext& ctx) {
    if (PropIndices::SHAPE == PropIndices::INVALID ||
        PropIndices::SHAPE_STRAIGHT == PropIndices::INVALID) {
        throw std::runtime_error("Stair placement requires registered shape=straight property");
    }

    const uint16_t facingValue = (ctx.hitNormal.y == 0)
        ? stairFacingFromSideNormal(ctx.blockId, ctx.hitNormal)
        : horizontalFacingFromYaw(ctx.playerYaw);
    const uint16_t halfValue = halfFromHit(ctx);

    return BlockStateRegistry::getState(
        ctx.blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facingValue},
            {PropIndices::HALF, halfValue},
            {PropIndices::SHAPE, PropIndices::SHAPE_STRAIGHT}
        });
}

BlockStateId strategySlab(const PlacementContext& ctx) {
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::HALF, halfFromHit(ctx));
}

BlockStateId strategyVerticalSlab(const PlacementContext& ctx) {
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::HALF, verticalHalfFromHit(ctx));
}

BlockStateId strategyBed(const PlacementContext& ctx) {
    return BedBlockLogic::makeBedState(ctx.blockId, horizontalFacingFromYaw(ctx.playerYaw), PropIndices::PART_FOOT);
}

BlockStateId strategyDoor(const PlacementContext& ctx) {
    return DoorBlockLogic::makeDoorState(
        ctx.blockId,
        horizontalFacingFromYaw(ctx.playerYaw),
        PropIndices::HALF_LOWER,
        PropIndices::HINGE_LEFT,
        false,
        false);
}

BlockStateId strategyTrapdoor(const PlacementContext& ctx) {
    if (PropIndices::OPEN == PropIndices::INVALID ||
        PropIndices::OPEN_FALSE == PropIndices::INVALID ||
        PropIndices::POWERED == PropIndices::INVALID ||
        PropIndices::POWERED_FALSE == PropIndices::INVALID) {
        throw std::runtime_error("Trapdoor placement requires open=false and powered=false properties");
    }

    const uint16_t facing = ctx.hitNormal.y == 0
        ? facingFromSideNormal(ctx.hitNormal)
        : horizontalFacingFromYaw(ctx.playerYaw);
    return BlockStateRegistry::getState(
        ctx.blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::HALF, halfFromHit(ctx)},
            {PropIndices::OPEN, PropIndices::OPEN_FALSE},
            {PropIndices::POWERED, PropIndices::POWERED_FALSE}
        });
}

BlockStateId strategyFence(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

BlockStateId strategyWall(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

BlockStateId strategyFacePlaneWall(const PlacementContext& ctx) {
    requireWallFacePlanePlacementProperties();
    if (ctx.hitNormal.y != 0) {
        return NULL_BLOCK_STATE;
    }
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, facePlaneFacingFromWallNormal(ctx.hitNormal));
}

BlockStateId strategyFacePlaneFloor(const PlacementContext& ctx) {
    requireFloorFacePlanePlacementProperties();
    if (ctx.hitNormal.y != 1) {
        return NULL_BLOCK_STATE;
    }
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_FLOOR);
}

BlockStateId strategyRedstoneWireFace(const PlacementContext& ctx) {
    requireRedstoneWireFacePlacementProperties();

    uint16_t facing = PropIndices::FACING_FLOOR;
    if (ctx.hitNormal.y > 0) {
        facing = PropIndices::FACING_FLOOR;
    } else if (ctx.hitNormal.y < 0) {
        facing = PropIndices::FACING_CEILING;
    } else {
        facing = facePlaneFacingFromWallNormal(ctx.hitNormal);
    }

    if (!WireFaceGeometry::isWireFacing(facing)) {
        throw std::runtime_error("Redstone wire placement produced an unsupported facing value");
    }
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, facing);
}

} // namespace

bool tryMergePlacementStates(const BlockStateId existingState,
                             const BlockStateId incomingState,
                             BlockStateId& mergedState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(existingState);
    if (blockId == RUNTIME_ID_NULL || blockId != BlockStateRegistry::getBlockId(incomingState)) {
        return false;
    }

    const std::vector<BlockCollisionBox> existingBoxes = BlockCollision::getBoxes(existingState);
    const std::vector<BlockCollisionBox> incomingBoxes = BlockCollision::getBoxes(incomingState);
    if (existingBoxes.empty() || incomingBoxes.empty() || anyBoxesIntersect(existingBoxes, incomingBoxes)) {
        return false;
    }

    const float mergedVolume = totalBoxVolume(existingBoxes) + totalBoxVolume(incomingBoxes);
    for (const BlockStateId candidateState : BlockStateRegistry::getStatesForBlock(blockId)) {
        const std::vector<BlockCollisionBox> candidateBoxes = BlockCollision::getBoxes(candidateState);
        if (candidateBoxes.empty()) {
            continue;
        }
        if (std::abs(totalBoxVolume(candidateBoxes) - mergedVolume) > kPlacementEpsilon) {
            continue;
        }
        if (!boxesCoverAll(candidateBoxes, existingBoxes) ||
            !boxesCoverAll(candidateBoxes, incomingBoxes)) {
            continue;
        }

        mergedState = candidateState;
        return true;
    }

    return false;
}

bool canReplaceWithMergedPlacementResult(const BlockStateId existingState,
                                         const BlockStateId resultState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(existingState);
    if (blockId == RUNTIME_ID_NULL || blockId != BlockStateRegistry::getBlockId(resultState)) {
        return false;
    }

    for (const BlockStateId incomingState : BlockStateRegistry::getStatesForBlock(blockId)) {
        BlockStateId mergedState = NULL_BLOCK_STATE;
        if (tryMergePlacementStates(existingState, incomingState, mergedState) &&
            mergedState == resultState) {
            return true;
        }
    }
    return false;
}

void PlacementStrategyRegistry::initBuiltinStrategies() {
    registerStrategy("simple", strategySimple);
    registerStrategy("attach_wall", strategyAttachWall);
    registerStrategy("horizontal_facing", strategyHorizontalFacing);
    registerStrategy("six_way_facing", strategySixWayFacing);
    registerStrategy("hopper_facing", strategyHopperFacing);
    registerStrategy("axis_oriented", strategyAxisOriented);
    registerStrategy("stairs", strategyStairs);
    registerStrategy("slab", strategySlab);
    registerStrategy("vertical_slab", strategyVerticalSlab);
    registerStrategy("bed", strategyBed);
    registerStrategy("door", strategyDoor);
    registerStrategy("trapdoor", strategyTrapdoor);
    registerStrategy("fence", strategyFence);
    registerStrategy("wall", strategyWall);
    registerStrategy("face_plane_wall", strategyFacePlaneWall);
    registerStrategy("face_plane_floor", strategyFacePlaneFloor);
    registerStrategy("redstone_wire_face", strategyRedstoneWireFace);
}
