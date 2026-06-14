#ifndef MECRAFT_ECS_DROP_COMPONENTS_H
#define MECRAFT_ECS_DROP_COMPONENTS_H

#include <cstdint>

#include <glm/glm.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"

namespace ecs {

struct DropEntityIdComponent {
    std::size_t dropId = 0;
};

struct ItemComponent {
    ItemID itemId = 0;
    uint32_t stackCount = 0;
};

struct LifetimeComponent {
    float ageSeconds = 0.0f;
    float lifeTimeSeconds = 0.0f;
};

struct SpinVisualComponent {
    float yawRadians = 0.0f;
    float spinSpeedRadians = 0.0f;
};

/// Falling block entity — a block that lost support and is now falling.
/// Tick-rate logic moves gridPosition one cell down per tick (Minecraft semantics);
/// FallingBlockInterpolateSystem lerps TransformComponent.position between ticks for rendering.
struct FallingBlockComponent {
    BlockID blockId = 0;             ///< Block placed back into the world on landing
    glm::ivec3 gridPosition{};       ///< Current logical (integer) grid cell
    glm::ivec3 prevGridPosition{};   ///< Grid cell at the start of the current tick (for interpolation)
    float tickAccumulator = 0.0f;    ///< Seconds accumulated since the last tick (render alpha source)
};

} // namespace ecs

#endif // MECRAFT_ECS_DROP_COMPONENTS_H
