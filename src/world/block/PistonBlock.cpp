#include "PistonBlock.h"

#include <stdexcept>

#include "../World.h"
#include "Block.h"
#include "PropIndices.h"

namespace PistonBlockLogic {
namespace {

void requirePistonProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::EXTENDED == PropIndices::INVALID ||
        PropIndices::EXTENDED_TRUE == PropIndices::INVALID ||
        PropIndices::EXTENDED_FALSE == PropIndices::INVALID ||
        PropIndices::TYPE == PropIndices::INVALID ||
        PropIndices::TYPE_NORMAL == PropIndices::INVALID ||
        PropIndices::TYPE_STICKY == PropIndices::INVALID) {
        throw std::runtime_error("Piston blocks require facing, extended, and type properties");
    }
}

BlockID pistonBlockId() {
    static const BlockID blockId = BlockRegistry::requireIdByName("minecraft:piston");
    return blockId;
}

BlockID stickyPistonBlockId() {
    static const BlockID blockId = BlockRegistry::requireIdByName("minecraft:sticky_piston");
    return blockId;
}

BlockID pistonHeadBlockId() {
    static const BlockID blockId = BlockRegistry::requireIdByName("minecraft:piston_head");
    return blockId;
}

glm::ivec3 directionFromFacing(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    if (facing == PropIndices::FACING_UP) {
        return {0, 1, 0};
    }
    if (facing == PropIndices::FACING_DOWN) {
        return {0, -1, 0};
    }
    throw std::runtime_error("Piston state contains an unknown facing value");
}

uint16_t facingValue(const BlockStateId stateId) {
    requirePistonProperties();
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (facing == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error("Piston state is missing facing");
    }
    return facing;
}

bool isExtendedValueTrue(const BlockStateId stateId) {
    requirePistonProperties();
    const uint16_t extended = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::EXTENDED);
    if (extended != PropIndices::EXTENDED_TRUE && extended != PropIndices::EXTENDED_FALSE) {
        throw std::runtime_error("Piston state requires extended=true/false");
    }
    return extended == PropIndices::EXTENDED_TRUE;
}

uint16_t headTypeForBaseState(const BlockStateId baseState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(baseState);
    if (blockId == pistonBlockId()) {
        return PropIndices::TYPE_NORMAL;
    }
    if (blockId == stickyPistonBlockId()) {
        return PropIndices::TYPE_STICKY;
    }
    throw std::runtime_error("Piston head type requested for a non-piston base");
}

uint16_t headTypeValue(const BlockStateId headState) {
    requirePistonProperties();
    const uint16_t type = BlockStateRegistry::getPropertyIndex(headState, PropIndices::TYPE);
    if (type != PropIndices::TYPE_NORMAL && type != PropIndices::TYPE_STICKY) {
        throw std::runtime_error("Piston head state requires type=normal/sticky");
    }
    return type;
}

} // namespace

bool isPistonBaseState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return blockId == pistonBlockId() || blockId == stickyPistonBlockId();
}

bool isPistonHeadState(const BlockStateId stateId) {
    return stateId != NULL_BLOCK_STATE &&
           BlockStateRegistry::getBlockId(stateId) == pistonHeadBlockId();
}

bool isPistonAssemblyState(const BlockStateId stateId) {
    return isPistonBaseState(stateId) || isPistonHeadState(stateId);
}

bool tryGetOtherPartPosition(const glm::ivec3& pos,
                             const BlockStateId stateId,
                             glm::ivec3& outOtherPos) {
    if (isPistonHeadState(stateId)) {
        outOtherPos = pos - directionFromFacing(facingValue(stateId));
        return true;
    }
    if (isPistonBaseState(stateId) && isExtendedValueTrue(stateId)) {
        outOtherPos = pos + directionFromFacing(facingValue(stateId));
        return true;
    }
    return false;
}

bool isMatchingAssemblyPart(const BlockStateId stateId, const BlockStateId otherState) {
    if (isPistonHeadState(stateId)) {
        if (!isPistonBaseState(otherState) || !isExtendedValueTrue(otherState)) {
            return false;
        }
        return facingValue(stateId) == facingValue(otherState) &&
               headTypeValue(stateId) == headTypeForBaseState(otherState);
    }

    if (isPistonBaseState(stateId)) {
        if (!isExtendedValueTrue(stateId) || !isPistonHeadState(otherState)) {
            return false;
        }
        return facingValue(stateId) == facingValue(otherState) &&
               headTypeForBaseState(stateId) == headTypeValue(otherState);
    }

    return false;
}

BlockID removePistonAssembly(World& world,
                             const glm::ivec3& hitPos,
                             std::vector<glm::ivec3>* removedPositions) {
    const BlockStateId stateId = world.getBlockState(hitPos.x, hitPos.y, hitPos.z);
    if (!isPistonAssemblyState(stateId)) {
        return RUNTIME_ID_NULL;
    }

    BlockID droppedBlockId = BlockStateRegistry::getBlockId(stateId);
    glm::ivec3 otherPos{};
    const bool hasOtherPos = tryGetOtherPartPosition(hitPos, stateId, otherPos);
    const BlockStateId otherState = hasOtherPos
        ? world.getBlockState(otherPos.x, otherPos.y, otherPos.z)
        : NULL_BLOCK_STATE;
    const bool removeOther = hasOtherPos && isMatchingAssemblyPart(stateId, otherState);
    if (removeOther && isPistonHeadState(stateId)) {
        droppedBlockId = BlockStateRegistry::getBlockId(otherState);
    }

    world.setBlockState(hitPos.x, hitPos.y, hitPos.z, NULL_BLOCK_STATE);
    if (removedPositions != nullptr) {
        removedPositions->push_back(hitPos);
    }

    if (removeOther) {
        world.setBlockState(otherPos.x, otherPos.y, otherPos.z, NULL_BLOCK_STATE);
        if (removedPositions != nullptr) {
            removedPositions->push_back(otherPos);
        }
    }
    return droppedBlockId;
}

} // namespace PistonBlockLogic
