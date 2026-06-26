#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"

namespace ecs {
class GameplayRegistry;
}

/// Removes data-driven storage inventory data when a storage container block is destroyed.
/// Returns true when the destroyed block uses a storage container behavior.
[[nodiscard]] bool handleChestInventoryBreak(ecs::GameplayRegistry& registry,
                                             BlockID brokenBlock,
                                             const glm::ivec3& blockPos,
                                             bool dropContents);
