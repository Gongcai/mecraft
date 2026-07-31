#pragma once

#include <glm/glm.hpp>

#include "Item.h"
#include "../world/block/BlockStateRegistry.h"

class IWorldView;

namespace ItemUseDispatcher {
[[nodiscard]] bool isWithinReach(const glm::vec3& actorPosition, const glm::ivec3& blockPosition,
                                 float maxDistance = 6.5f);
[[nodiscard]] bool canApplyBlockRule(const IWorldView& worldView, const glm::ivec3& blockPosition,
                                     const ItemUseRule& rule);
[[nodiscard]] bool canPickupFluid(const IWorldView& worldView, const glm::ivec3& blockPosition,
                                  const ItemUseRule& rule);
[[nodiscard]] bool canPlaceFluid(const IWorldView& worldView, const glm::ivec3& blockPosition, const ItemUseRule& rule);
[[nodiscard]] BlockStateId makeSourceFluidState(BlockID fluidBlock);
} // namespace ItemUseDispatcher
