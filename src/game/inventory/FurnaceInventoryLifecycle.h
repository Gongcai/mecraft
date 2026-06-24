#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"

namespace ecs {
class GameplayRegistry;
}

/// Removes furnace inventory data when a furnace block is destroyed.
/// Returns true when the destroyed block is a furnace, even when it has no stored inventory.
[[nodiscard]] bool handleFurnaceInventoryBreak(ecs::GameplayRegistry& registry,
                                               BlockID brokenBlock,
                                               const glm::ivec3& blockPos,
                                               bool dropContents);
