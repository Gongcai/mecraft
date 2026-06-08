#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"

namespace ecs {
class GameplayRegistry;
}

/// Removes chest inventory data when a chest block is destroyed.
/// Returns true when the destroyed block is a chest, even if it had no stored inventory yet.
[[nodiscard]] bool handleChestInventoryBreak(ecs::GameplayRegistry& registry,
                                             BlockID brokenBlock,
                                             const glm::ivec3& blockPos,
                                             bool dropContents);
