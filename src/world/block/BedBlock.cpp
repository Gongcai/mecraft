#include "BedBlock.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "../IWorldView.h"
#include "../World.h"
#include "PropIndices.h"

namespace BedBlockLogic {
namespace {

[[noreturn]] void failBedBlock(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

void requireBedProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::PART == PropIndices::INVALID ||
        PropIndices::PART_HEAD == PropIndices::INVALID ||
        PropIndices::PART_FOOT == PropIndices::INVALID) {
        failBedBlock("Bed blocks require facing and part=head/foot properties");
    }
}

bool isHorizontalFacingValue(const uint16_t facingValue) {
    return facingValue == PropIndices::FACING_NORTH ||
           facingValue == PropIndices::FACING_SOUTH ||
           facingValue == PropIndices::FACING_EAST ||
           facingValue == PropIndices::FACING_WEST;
}

void requireHorizontalFacingValue(const uint16_t facingValue) {
    if (!isHorizontalFacingValue(facingValue)) {
        failBedBlock("Bed blocks require a horizontal facing value");
    }
}

BlockStateId withRequiredProperty(const BlockStateId stateId,
                             const uint16_t property,
                             const uint16_t value,
                             const char* context) {
    const BlockStateId updated = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updated, property) != value) {
        failBedBlock(context);
    }
    return updated;
}

uint16_t partValue(const BlockStateId stateId) {
    requireBedProperties();
    const uint16_t part = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::PART);
    if (part != PropIndices::PART_HEAD && part != PropIndices::PART_FOOT) {
        failBedBlock("Bed state requires part=head or part=foot");
    }
    return part;
}

uint16_t facingValue(const BlockStateId stateId) {
    requireBedProperties();
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    requireHorizontalFacingValue(facing);
    return facing;
}

bool isEmptyBedCell(const IWorldView& worldView, const glm::ivec3& pos) {
    return worldView.isChunkLoadedForBlock(pos.x, pos.y, pos.z) &&
           worldView.getBlockState(pos.x, pos.y, pos.z) == NULL_BLOCK_STATE &&
           worldView.getFluidState(pos.x, pos.y, pos.z) == NULL_BLOCK_STATE;
}

uint16_t oppositePartValue(const uint16_t part) {
    if (part == PropIndices::PART_FOOT) {
        return PropIndices::PART_HEAD;
    }
    if (part == PropIndices::PART_HEAD) {
        return PropIndices::PART_FOOT;
    }
    failBedBlock("Bed state requires part=head or part=foot");
}

} // namespace

bool isBedBlock(const BlockID blockId) {
    if (blockId == RUNTIME_ID_NULL) {
        return false;
    }
    return BlockRegistry::getFast(blockId).placementStrategy == "bed";
}

bool isBedState(const BlockStateId stateId) {
    if (!isBedBlock(BlockStateRegistry::getBlockId(stateId))) {
        return false;
    }
    requireBedProperties();
    const uint16_t part = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::PART);
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    return (part == PropIndices::PART_HEAD || part == PropIndices::PART_FOOT) &&
           isHorizontalFacingValue(facing);
}

bool isFootState(const BlockStateId stateId) {
    return isBedState(stateId) && partValue(stateId) == PropIndices::PART_FOOT;
}

bool isHeadState(const BlockStateId stateId) {
    return isBedState(stateId) && partValue(stateId) == PropIndices::PART_HEAD;
}

glm::ivec3 headOffsetForFacing(const uint16_t facingValue) {
    requireHorizontalFacingValue(facingValue);
    if (facingValue == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facingValue == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    if (facingValue == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    return {0, 0, -1};
}

BlockStateId makeBedState(const BlockID blockId, const uint16_t facingValue, const uint16_t partValue) {
    requireBedProperties();
    requireHorizontalFacingValue(facingValue);
    if (!isBedBlock(blockId)) {
        failBedBlock("Bed state construction requires a bed block id");
    }
    if (partValue != PropIndices::PART_HEAD && partValue != PropIndices::PART_FOOT) {
        failBedBlock("Bed state construction requires part=head or part=foot");
    }

    BlockStateId stateId = BlockStateRegistry::getDefaultState(blockId);
    stateId = withRequiredProperty(stateId, PropIndices::FACING, facingValue, "Bed state is missing facing");
    stateId = withRequiredProperty(stateId, PropIndices::PART, partValue, "Bed state is missing part");
    return stateId;
}

BedPlacement resolvePlacement(const IWorldView& worldView,
                              const glm::ivec3& footPos,
                              const BlockStateId footState) {
    BedPlacement placement;
    if (!isFootState(footState)) {
        return placement;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(footState);
    const uint16_t facing = facingValue(footState);
    placement.footPos = footPos;
    placement.headPos = footPos + headOffsetForFacing(facing);
    placement.footState = footState;
    placement.headState = makeBedState(blockId, facing, PropIndices::PART_HEAD);
    placement.valid = isEmptyBedCell(worldView, placement.footPos) &&
                      isEmptyBedCell(worldView, placement.headPos);
    return placement;
}

bool tryGetOtherHalfPosition(const glm::ivec3& pos,
                             const BlockStateId stateId,
                             glm::ivec3& outOtherPos) {
    if (!isBedState(stateId)) {
        return false;
    }

    const glm::ivec3 headOffset = headOffsetForFacing(facingValue(stateId));
    outOtherPos = partValue(stateId) == PropIndices::PART_FOOT
        ? pos + headOffset
        : pos - headOffset;
    return true;
}

bool isMatchingOtherHalf(const BlockStateId stateId, const BlockStateId otherState) {
    if (!isBedState(stateId) || !isBedState(otherState)) {
        return false;
    }
    if (BlockStateRegistry::getBlockId(stateId) != BlockStateRegistry::getBlockId(otherState)) {
        return false;
    }
    if (facingValue(stateId) != facingValue(otherState)) {
        return false;
    }
    return partValue(otherState) == oppositePartValue(partValue(stateId));
}

void placeBed(World& world, const BedPlacement& placement) {
    if (!placement.valid) {
        failBedBlock("Cannot place an invalid bed placement");
    }
    world.setBlockState(placement.footPos.x, placement.footPos.y, placement.footPos.z, placement.footState);
    world.setBlockState(placement.headPos.x, placement.headPos.y, placement.headPos.z, placement.headState);
}

BlockID removeBed(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions) {
    const BlockStateId stateId = world.getBlockState(hitPos.x, hitPos.y, hitPos.z);
    if (!isBedState(stateId)) {
        return RUNTIME_ID_NULL;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    glm::ivec3 otherPos{};
    const bool hasOtherPos = tryGetOtherHalfPosition(hitPos, stateId, otherPos);
    const BlockStateId otherState = hasOtherPos
        ? world.getBlockState(otherPos.x, otherPos.y, otherPos.z)
        : NULL_BLOCK_STATE;
    const bool removeOther = hasOtherPos && isMatchingOtherHalf(stateId, otherState);

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
    return blockId;
}

} // namespace BedBlockLogic
