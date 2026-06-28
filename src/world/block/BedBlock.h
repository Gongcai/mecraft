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
    BlockStateId footState = NULL_BLOCK_STATE;
    BlockStateId headState = NULL_BLOCK_STATE;
};

[[nodiscard]] bool isBedBlock(BlockID blockId);
[[nodiscard]] bool isBedState(BlockStateId stateId);
[[nodiscard]] bool isFootState(BlockStateId stateId);
[[nodiscard]] bool isHeadState(BlockStateId stateId);
[[nodiscard]] glm::ivec3 headOffsetForFacing(uint16_t facingValue);
[[nodiscard]] BlockStateId makeBedState(BlockID blockId, uint16_t facingValue, uint16_t partValue);
[[nodiscard]] BedPlacement resolvePlacement(const IWorldView& worldView,
                                            const glm::ivec3& footPos,
                                            BlockStateId footState);
[[nodiscard]] bool tryGetOtherHalfPosition(const glm::ivec3& pos,
                                           BlockStateId stateId,
                                           glm::ivec3& outOtherPos);
[[nodiscard]] bool isMatchingOtherHalf(BlockStateId stateId, BlockStateId otherState);

void placeBed(World& world, const BedPlacement& placement);
BlockID removeBed(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions = nullptr);

} // namespace BedBlockLogic
