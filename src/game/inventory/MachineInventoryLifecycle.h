#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"

namespace ecs {
class GameplayRegistry;
}

/// Removes machine inventory data when a processor-backed block entity is destroyed.
/// Returns true when the destroyed block owns a machine inventory.
[[nodiscard]] bool handleMachineInventoryBreak(ecs::GameplayRegistry& registry,
                                               BlockID brokenBlock,
                                               const glm::ivec3& blockPos,
                                               bool dropContents);
