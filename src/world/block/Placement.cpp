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

StateID strategyHorizontalFacing(const PlacementContext& ctx) {
    const float angle = std::fmod(ctx.playerYaw + 360.0f, 360.0f);

    uint16_t facingValue = PropIndices::FACING_SOUTH;
    if (angle >= 45.0f && angle < 135.0f) {
        facingValue = PropIndices::FACING_WEST;
    } else if (angle >= 135.0f && angle < 225.0f) {
        facingValue = PropIndices::FACING_NORTH;
    } else if (angle >= 225.0f && angle < 315.0f) {
        facingValue = PropIndices::FACING_EAST;
    }

    return BlockStateRegistry::getState(ctx.blockId, PropIndices::FACING, facingValue);
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
    const float angle = std::fmod(ctx.playerYaw + 360.0f, 360.0f);

    uint16_t facingValue = PropIndices::FACING_SOUTH;
    if (angle >= 45.0f && angle < 135.0f) {
        facingValue = PropIndices::FACING_WEST;
    } else if (angle >= 135.0f && angle < 225.0f) {
        facingValue = PropIndices::FACING_NORTH;
    } else if (angle >= 225.0f && angle < 315.0f) {
        facingValue = PropIndices::FACING_EAST;
    }

    uint16_t halfValue = (ctx.hitNormal.y == -1) ? PropIndices::HALF_TOP : PropIndices::HALF_BOTTOM;
    if (ctx.isSneaking) {
        halfValue = (halfValue == PropIndices::HALF_TOP) ? PropIndices::HALF_BOTTOM : PropIndices::HALF_TOP;
    }

    return BlockStateRegistry::getState(
        ctx.blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facingValue},
            {PropIndices::HALF, halfValue}
        });
}

StateID strategySlab(const PlacementContext& ctx) {
    if (PropIndices::HALF == PropIndices::INVALID ||
        PropIndices::HALF_TOP == PropIndices::INVALID ||
        PropIndices::HALF_BOTTOM == PropIndices::INVALID) {
        throw std::runtime_error("Slab placement requires registered half=top/bottom properties");
    }

    uint16_t halfValue = (ctx.hitNormal.y == -1) ? PropIndices::HALF_TOP : PropIndices::HALF_BOTTOM;
    if (ctx.isSneaking) {
        halfValue = (halfValue == PropIndices::HALF_TOP) ? PropIndices::HALF_BOTTOM : PropIndices::HALF_TOP;
    }

    return BlockStateRegistry::getState(ctx.blockId, PropIndices::HALF, halfValue);
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
