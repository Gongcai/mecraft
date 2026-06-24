#include "BedBlock.h"

#include <stdexcept>

#include "../IWorldView.h"
#include "../World.h"
#include "PropIndices.h"

namespace BedBlockLogic {
namespace {

void requireBedProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::PART == PropIndices::INVALID ||
        PropIndices::PART_HEAD == PropIndices::INVALID ||
        PropIndices::PART_FOOT == PropIndices::INVALID) {
        throw std::runtime_error("Bed blocks require facing and part=head/foot properties");
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
        throw std::runtime_error("Bed blocks require a horizontal facing value");
    }
}

StateID withRequiredProperty(const StateID stateId,
                             const uint16_t property,
                             const uint16_t value,
                             const char* context) {
    const StateID updated = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updated, property) != value) {
        throw std::runtime_error(context);
    }
    return updated;
}

uint16_t partValue(const StateID stateId) {
    requireBedProperties();
    const uint16_t part = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::PART);
    if (part != PropIndices::PART_HEAD && part != PropIndices::PART_FOOT) {
        throw std::runtime_error("Bed state requires part=head or part=foot");
    }
    return part;
}

uint16_t facingValue(const StateID stateId) {
    requireBedProperties();
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    requireHorizontalFacingValue(facing);
    return facing;
}

bool isEmptyBedCell(const IWorldView& worldView, const glm::ivec3& pos) {
    return worldView.isChunkLoadedForBlock(pos.x, pos.y, pos.z) &&
           worldView.getBlockState(pos.x, pos.y, pos.z) == BlockIds::AIR &&
           worldView.getFluidState(pos.x, pos.y, pos.z) == BlockIds::AIR;
}

uint16_t oppositePartValue(const uint16_t part) {
    if (part == PropIndices::PART_FOOT) {
        return PropIndices::PART_HEAD;
    }
    if (part == PropIndices::PART_HEAD) {
        return PropIndices::PART_FOOT;
    }
    throw std::runtime_error("Bed state requires part=head or part=foot");
}

} // namespace

bool isBedBlock(const BlockID blockId) {
    if (blockId == BlockIds::AIR) {
        return false;
    }
    return BlockRegistry::getFast(blockId).placementStrategy == "bed";
}

bool isBedState(const StateID stateId) {
    if (!isBedBlock(BlockStateRegistry::getBlockId(stateId))) {
        return false;
    }
    requireBedProperties();
    const uint16_t part = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::PART);
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    return (part == PropIndices::PART_HEAD || part == PropIndices::PART_FOOT) &&
           isHorizontalFacingValue(facing);
}

bool isFootState(const StateID stateId) {
    return isBedState(stateId) && partValue(stateId) == PropIndices::PART_FOOT;
}

bool isHeadState(const StateID stateId) {
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

StateID makeBedState(const BlockID blockId, const uint16_t facingValue, const uint16_t partValue) {
    requireBedProperties();
    requireHorizontalFacingValue(facingValue);
    if (!isBedBlock(blockId)) {
        throw std::runtime_error("Bed state construction requires a bed block id");
    }
    if (partValue != PropIndices::PART_HEAD && partValue != PropIndices::PART_FOOT) {
        throw std::runtime_error("Bed state construction requires part=head or part=foot");
    }

    StateID stateId = BlockStateRegistry::getDefaultState(blockId);
    stateId = withRequiredProperty(stateId, PropIndices::FACING, facingValue, "Bed state is missing facing");
    stateId = withRequiredProperty(stateId, PropIndices::PART, partValue, "Bed state is missing part");
    return stateId;
}

BedPlacement resolvePlacement(const IWorldView& worldView,
                              const glm::ivec3& footPos,
                              const StateID footState) {
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
                             const StateID stateId,
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

bool isMatchingOtherHalf(const StateID stateId, const StateID otherState) {
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
        throw std::runtime_error("Cannot place an invalid bed placement");
    }
    world.setBlockState(placement.footPos.x, placement.footPos.y, placement.footPos.z, placement.footState);
    world.setBlockState(placement.headPos.x, placement.headPos.y, placement.headPos.z, placement.headState);
}

BlockID removeBed(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions) {
    const StateID stateId = world.getBlockState(hitPos.x, hitPos.y, hitPos.z);
    if (!isBedState(stateId)) {
        return BlockIds::AIR;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    glm::ivec3 otherPos{};
    const bool hasOtherPos = tryGetOtherHalfPosition(hitPos, stateId, otherPos);
    const StateID otherState = hasOtherPos
        ? world.getBlockState(otherPos.x, otherPos.y, otherPos.z)
        : BlockIds::AIR;
    const bool removeOther = hasOtherPos && isMatchingOtherHalf(stateId, otherState);

    world.setBlockState(hitPos.x, hitPos.y, hitPos.z, BlockIds::AIR);
    if (removedPositions != nullptr) {
        removedPositions->push_back(hitPos);
    }

    if (removeOther) {
        world.setBlockState(otherPos.x, otherPos.y, otherPos.z, BlockIds::AIR);
        if (removedPositions != nullptr) {
            removedPositions->push_back(otherPos);
        }
    }
    return blockId;
}

} // namespace BedBlockLogic
