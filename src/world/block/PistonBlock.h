#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "BlockStateRegistry.h"

class World;

namespace PistonBlockLogic {

[[nodiscard]] bool isPistonBaseState(StateID stateId);
[[nodiscard]] bool isPistonHeadState(StateID stateId);
[[nodiscard]] bool isPistonAssemblyState(StateID stateId);
[[nodiscard]] bool tryGetOtherPartPosition(const glm::ivec3& pos,
                                           StateID stateId,
                                           glm::ivec3& outOtherPos);
[[nodiscard]] bool isMatchingAssemblyPart(StateID stateId, StateID otherState);

BlockID removePistonAssembly(World& world,
                             const glm::ivec3& hitPos,
                             std::vector<glm::ivec3>* removedPositions = nullptr);

} // namespace PistonBlockLogic
