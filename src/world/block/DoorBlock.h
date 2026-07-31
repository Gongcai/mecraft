#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "BlockStateRegistry.h"

class IWorldView;
class World;

namespace DoorBlockLogic {

struct DoorPlacement {
    bool valid = false;
    glm::ivec3 lowerPos{};
    glm::ivec3 upperPos{};
    BlockStateId lowerState = NULL_BLOCK_STATE;
    BlockStateId upperState = NULL_BLOCK_STATE;
};

[[nodiscard]] bool isDoorBlock(BlockID blockId);
[[nodiscard]] bool isDoorState(BlockStateId stateId);
[[nodiscard]] bool isLowerState(BlockStateId stateId);
[[nodiscard]] bool isUpperState(BlockStateId stateId);
[[nodiscard]] BlockStateId makeDoorState(BlockID blockId, uint16_t facingValue, uint16_t halfValue, uint16_t hingeValue,
                                         bool open, bool powered);
[[nodiscard]] DoorPlacement resolvePlacement(const IWorldView& worldView, const glm::ivec3& lowerPos,
                                             BlockStateId lowerState);
[[nodiscard]] bool tryGetOtherHalfPosition(const glm::ivec3& pos, BlockStateId stateId, glm::ivec3& outOtherPos);
[[nodiscard]] bool isMatchingOtherHalf(BlockStateId stateId, BlockStateId otherState);

void placeDoor(World& world, const DoorPlacement& placement);
void setDoorOpen(World& world, const glm::ivec3& hitPos, bool open);
void setDoorPoweredOpen(World& world, const glm::ivec3& hitPos, bool powered);
BlockID removeDoor(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions = nullptr);

} // namespace DoorBlockLogic
