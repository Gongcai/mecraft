#pragma once

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

class World;

namespace game::interaction {

[[nodiscard]] bool hasBlockInteraction(BlockID blockId);
[[nodiscard]] BlockStateId nextBlockInteractionState(BlockStateId currentState);
bool applyBlockInteraction(World& world, const glm::ivec3& position);

} // namespace game::interaction
