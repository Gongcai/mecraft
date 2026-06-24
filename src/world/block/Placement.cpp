#include "Placement.h"

#include "PropIndices.h"

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

StateID strategySimple(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

StateID strategyAttachWall(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y == 1) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, PropIndices::FACING_FLOOR);
    }
    if (n.y == -1) {
        return 0;
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
    return 0;
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

StateID strategyHorizontalFacing(const PlacementContext& ctx) {
    return BlockStateRegistry::getState(
        ctx.blockId,
        PropIndices::FACING,
        applyPlacementFacingRevert(ctx.blockId, horizontalFacingFromYaw(ctx.playerYaw)));
}

StateID strategyAxisOriented(const PlacementContext& ctx) {
    const auto& n = ctx.hitNormal;
    if (n.y != 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_Y);
    }
    if (n.x != 0) {
        return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_X);
    }
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::AXIS, PropIndices::AXIS_Z);
}

StateID strategyStairs(const PlacementContext& ctx) {
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

StateID strategySlab(const PlacementContext& ctx) {
    return BlockStateRegistry::getState(ctx.blockId, PropIndices::HALF, halfFromHit(ctx));
}

StateID strategyFence(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

StateID strategyWall(const PlacementContext& ctx) {
    return BlockStateRegistry::getDefaultState(ctx.blockId);
}

} // namespace

void PlacementStrategyRegistry::initBuiltinStrategies() {
    registerStrategy("simple", strategySimple);
    registerStrategy("attach_wall", strategyAttachWall);
    registerStrategy("horizontal_facing", strategyHorizontalFacing);
    registerStrategy("axis_oriented", strategyAxisOriented);
    registerStrategy("stairs", strategyStairs);
    registerStrategy("slab", strategySlab);
    registerStrategy("fence", strategyFence);
    registerStrategy("wall", strategyWall);
}
