#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "BlockStateRegistry.h"

class IWorldView;
class World;

namespace BedBlockLogic {

struct BedPlacement {
    bool valid = false;
    glm::ivec3 footPos{};
    glm::ivec3 headPos{};
    StateID footState = RUNTIME_ID_NULL;
    StateID headState = RUNTIME_ID_NULL;
};

[[nodiscard]] bool isBedBlock(BlockID blockId);
[[nodiscard]] bool isBedState(StateID stateId);
[[nodiscard]] bool isFootState(StateID stateId);
[[nodiscard]] bool isHeadState(StateID stateId);
[[nodiscard]] glm::ivec3 headOffsetForFacing(uint16_t facingValue);
[[nodiscard]] StateID makeBedState(BlockID blockId, uint16_t facingValue, uint16_t partValue);
[[nodiscard]] BedPlacement resolvePlacement(const IWorldView& worldView,
                                            const glm::ivec3& footPos,
                                            StateID footState);
[[nodiscard]] bool tryGetOtherHalfPosition(const glm::ivec3& pos,
                                           StateID stateId,
                                           glm::ivec3& outOtherPos);
[[nodiscard]] bool isMatchingOtherHalf(StateID stateId, StateID otherState);

void placeBed(World& world, const BedPlacement& placement);
BlockID removeBed(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions = nullptr);

} // namespace BedBlockLogic
