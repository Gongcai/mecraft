#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"

namespace ecs {
class GameplayRegistry;
}

/// Removes data-driven storage inventory data when a storage container block is destroyed.
/// Returns true when the destroyed block uses a storage container behavior.
[[nodiscard]] bool handleBlockEntityInventoryBreak(ecs::GameplayRegistry& registry,
                                                   BlockID brokenBlock,
                                                   const glm::ivec3& blockPos,
                                                   bool dropContents);

/// Creates the storage inventory entry for a placed data-driven storage container.
/// Returns true when the placed block uses a storage container behavior.
[[nodiscard]] bool ensureBlockEntityInventoryForPlacedBlock(ecs::GameplayRegistry& registry,
                                                            BlockID placedBlock,
                                                            const glm::ivec3& blockPos);
