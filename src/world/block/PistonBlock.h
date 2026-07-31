#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "BlockStateRegistry.h"

class World;

namespace PistonBlockLogic {

[[nodiscard]] bool isPistonBaseState(BlockStateId stateId);
[[nodiscard]] bool isPistonHeadState(BlockStateId stateId);
[[nodiscard]] bool isPistonAssemblyState(BlockStateId stateId);
[[nodiscard]] bool tryGetOtherPartPosition(const glm::ivec3& pos, BlockStateId stateId, glm::ivec3& outOtherPos);
[[nodiscard]] bool isMatchingAssemblyPart(BlockStateId stateId, BlockStateId otherState);

BlockID removePistonAssembly(World& world, const glm::ivec3& hitPos,
                             std::vector<glm::ivec3>* removedPositions = nullptr);

} // namespace PistonBlockLogic
