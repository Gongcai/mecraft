#include "DoorBlock.h"

#include <stdexcept>

#include "../IWorldView.h"
#include "../World.h"
#include "PropIndices.h"

namespace DoorBlockLogic {
namespace {

void requireDoorProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::HALF == PropIndices::INVALID ||
        PropIndices::HALF_LOWER == PropIndices::INVALID ||
        PropIndices::HALF_UPPER == PropIndices::INVALID ||
        PropIndices::HINGE == PropIndices::INVALID ||
        PropIndices::HINGE_LEFT == PropIndices::INVALID ||
        PropIndices::HINGE_RIGHT == PropIndices::INVALID ||
        PropIndices::OPEN == PropIndices::INVALID ||
        PropIndices::OPEN_TRUE == PropIndices::INVALID ||
        PropIndices::OPEN_FALSE == PropIndices::INVALID ||
        PropIndices::POWERED == PropIndices::INVALID ||
        PropIndices::POWERED_TRUE == PropIndices::INVALID ||
        PropIndices::POWERED_FALSE == PropIndices::INVALID) {
        throw std::runtime_error("Door blocks require facing, half=lower/upper, hinge, open, and powered properties");
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
        throw std::runtime_error("Door blocks require a horizontal facing value");
    }
}

bool isHingeValue(const uint16_t hingeValue) {
    return hingeValue == PropIndices::HINGE_LEFT || hingeValue == PropIndices::HINGE_RIGHT;
}

bool isBooleanValue(const uint16_t value, const uint16_t falseValue, const uint16_t trueValue) {
    return value == falseValue || value == trueValue;
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

uint16_t facingValue(const StateID stateId) {
    requireDoorProperties();
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    requireHorizontalFacingValue(facing);
    return facing;
}

uint16_t halfValue(const StateID stateId) {
    requireDoorProperties();
    const uint16_t half = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::HALF);
    if (half != PropIndices::HALF_LOWER && half != PropIndices::HALF_UPPER) {
        throw std::runtime_error("Door state requires half=lower or half=upper");
    }
    return half;
}

uint16_t hingeValue(const StateID stateId) {
    requireDoorProperties();
    const uint16_t hinge = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::HINGE);
    if (!isHingeValue(hinge)) {
        throw std::runtime_error("Door state requires hinge=left or hinge=right");
    }
    return hinge;
}

bool openValue(const StateID stateId) {
    requireDoorProperties();
    const uint16_t open = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::OPEN);
    if (!isBooleanValue(open, PropIndices::OPEN_FALSE, PropIndices::OPEN_TRUE)) {
        throw std::runtime_error("Door state requires open=false or open=true");
    }
    return open == PropIndices::OPEN_TRUE;
}

bool poweredValue(const StateID stateId) {
    requireDoorProperties();
    const uint16_t powered = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::POWERED);
    if (!isBooleanValue(powered, PropIndices::POWERED_FALSE, PropIndices::POWERED_TRUE)) {
        throw std::runtime_error("Door state requires powered=false or powered=true");
    }
    return powered == PropIndices::POWERED_TRUE;
}

uint16_t oppositeHalfValue(const uint16_t half) {
    if (half == PropIndices::HALF_LOWER) {
        return PropIndices::HALF_UPPER;
    }
    if (half == PropIndices::HALF_UPPER) {
        return PropIndices::HALF_LOWER;
    }
    throw std::runtime_error("Door state requires half=lower or half=upper");
}

bool isEmptyDoorCell(const IWorldView& worldView, const glm::ivec3& pos) {
    return worldView.isChunkLoadedForBlock(pos.x, pos.y, pos.z) &&
           worldView.getBlockState(pos.x, pos.y, pos.z) == BlockIds::AIR &&
           worldView.getFluidState(pos.x, pos.y, pos.z) == BlockIds::AIR;
}

bool tryGetDoorHalves(World& world,
                      const glm::ivec3& hitPos,
                      glm::ivec3& outLowerPos,
                      StateID& outLowerState,
                      glm::ivec3& outUpperPos,
                      StateID& outUpperState) {
    const StateID hitState = world.getBlockState(hitPos.x, hitPos.y, hitPos.z);
    if (!isDoorState(hitState)) {
        return false;
    }

    if (isLowerState(hitState)) {
        outLowerPos = hitPos;
        outLowerState = hitState;
        outUpperPos = hitPos + glm::ivec3(0, 1, 0);
        outUpperState = world.getBlockState(outUpperPos.x, outUpperPos.y, outUpperPos.z);
    } else {
        outUpperPos = hitPos;
        outUpperState = hitState;
        outLowerPos = hitPos + glm::ivec3(0, -1, 0);
        outLowerState = world.getBlockState(outLowerPos.x, outLowerPos.y, outLowerPos.z);
    }
    return isMatchingOtherHalf(outLowerState, outUpperState);
}

} // namespace

bool isDoorBlock(const BlockID blockId) {
    if (blockId == BlockIds::AIR) {
        return false;
    }
    return BlockRegistry::getFast(blockId).placementStrategy == "door";
}

bool isDoorState(const StateID stateId) {
    if (!isDoorBlock(BlockStateRegistry::getBlockId(stateId))) {
        return false;
    }
    requireDoorProperties();
    const uint16_t half = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::HALF);
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    const uint16_t hinge = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::HINGE);
    const uint16_t open = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::OPEN);
    const uint16_t powered = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::POWERED);
    return (half == PropIndices::HALF_LOWER || half == PropIndices::HALF_UPPER) &&
           isHorizontalFacingValue(facing) &&
           isHingeValue(hinge) &&
           isBooleanValue(open, PropIndices::OPEN_FALSE, PropIndices::OPEN_TRUE) &&
           isBooleanValue(powered, PropIndices::POWERED_FALSE, PropIndices::POWERED_TRUE);
}

bool isLowerState(const StateID stateId) {
    return isDoorState(stateId) && halfValue(stateId) == PropIndices::HALF_LOWER;
}

bool isUpperState(const StateID stateId) {
    return isDoorState(stateId) && halfValue(stateId) == PropIndices::HALF_UPPER;
}

StateID makeDoorState(const BlockID blockId,
                      const uint16_t facingValue,
                      const uint16_t halfValue,
                      const uint16_t hingeValue,
                      const bool open,
                      const bool powered) {
    requireDoorProperties();
    requireHorizontalFacingValue(facingValue);
    if (!isDoorBlock(blockId)) {
        throw std::runtime_error("Door state construction requires a door block id");
    }
    if (halfValue != PropIndices::HALF_LOWER && halfValue != PropIndices::HALF_UPPER) {
        throw std::runtime_error("Door state construction requires half=lower or half=upper");
    }
    if (!isHingeValue(hingeValue)) {
        throw std::runtime_error("Door state construction requires hinge=left or hinge=right");
    }

    StateID stateId = BlockStateRegistry::getDefaultState(blockId);
    stateId = withRequiredProperty(stateId, PropIndices::FACING, facingValue, "Door state is missing facing");
    stateId = withRequiredProperty(stateId, PropIndices::HALF, halfValue, "Door state is missing half");
    stateId = withRequiredProperty(stateId, PropIndices::HINGE, hingeValue, "Door state is missing hinge");
    stateId = withRequiredProperty(
        stateId,
        PropIndices::OPEN,
        open ? PropIndices::OPEN_TRUE : PropIndices::OPEN_FALSE,
        "Door state is missing open");
    stateId = withRequiredProperty(
        stateId,
        PropIndices::POWERED,
        powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE,
        "Door state is missing powered");
    return stateId;
}

DoorPlacement resolvePlacement(const IWorldView& worldView,
                               const glm::ivec3& lowerPos,
                               const StateID lowerState) {
    DoorPlacement placement;
    if (!isLowerState(lowerState)) {
        return placement;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(lowerState);
    const uint16_t facing = facingValue(lowerState);
    const uint16_t hinge = hingeValue(lowerState);
    const bool open = openValue(lowerState);
    const bool powered = poweredValue(lowerState);

    placement.lowerPos = lowerPos;
    placement.upperPos = lowerPos + glm::ivec3(0, 1, 0);
    placement.lowerState = lowerState;
    placement.upperState = makeDoorState(blockId, facing, PropIndices::HALF_UPPER, hinge, open, powered);
    placement.valid = isEmptyDoorCell(worldView, placement.lowerPos) &&
                      isEmptyDoorCell(worldView, placement.upperPos);
    return placement;
}

bool tryGetOtherHalfPosition(const glm::ivec3& pos,
                             const StateID stateId,
                             glm::ivec3& outOtherPos) {
    if (!isDoorState(stateId)) {
        return false;
    }

    outOtherPos = halfValue(stateId) == PropIndices::HALF_LOWER
        ? pos + glm::ivec3(0, 1, 0)
        : pos + glm::ivec3(0, -1, 0);
    return true;
}

bool isMatchingOtherHalf(const StateID stateId, const StateID otherState) {
    if (!isDoorState(stateId) || !isDoorState(otherState)) {
        return false;
    }
    if (BlockStateRegistry::getBlockId(stateId) != BlockStateRegistry::getBlockId(otherState)) {
        return false;
    }
    if (facingValue(stateId) != facingValue(otherState) ||
        hingeValue(stateId) != hingeValue(otherState) ||
        openValue(stateId) != openValue(otherState) ||
        poweredValue(stateId) != poweredValue(otherState)) {
        return false;
    }
    return halfValue(otherState) == oppositeHalfValue(halfValue(stateId));
}

void placeDoor(World& world, const DoorPlacement& placement) {
    if (!placement.valid) {
        throw std::runtime_error("Cannot place an invalid door placement");
    }
    world.setBlockState(placement.lowerPos.x, placement.lowerPos.y, placement.lowerPos.z, placement.lowerState);
    world.setBlockState(placement.upperPos.x, placement.upperPos.y, placement.upperPos.z, placement.upperState);
}

void setDoorOpen(World& world, const glm::ivec3& hitPos, const bool open) {
    glm::ivec3 lowerPos{};
    glm::ivec3 upperPos{};
    StateID lowerState = BlockIds::AIR;
    StateID upperState = BlockIds::AIR;
    if (!tryGetDoorHalves(world, hitPos, lowerPos, lowerState, upperPos, upperState)) {
        return;
    }

    const StateID updatedLower = makeDoorState(
        BlockStateRegistry::getBlockId(lowerState),
        facingValue(lowerState),
        PropIndices::HALF_LOWER,
        hingeValue(lowerState),
        open,
        poweredValue(lowerState));
    const StateID updatedUpper = makeDoorState(
        BlockStateRegistry::getBlockId(upperState),
        facingValue(upperState),
        PropIndices::HALF_UPPER,
        hingeValue(upperState),
        open,
        poweredValue(upperState));
    world.setBlockState(lowerPos.x, lowerPos.y, lowerPos.z, updatedLower);
    world.setBlockState(upperPos.x, upperPos.y, upperPos.z, updatedUpper);
}

void setDoorPoweredOpen(World& world, const glm::ivec3& hitPos, const bool powered) {
    glm::ivec3 lowerPos{};
    glm::ivec3 upperPos{};
    StateID lowerState = BlockIds::AIR;
    StateID upperState = BlockIds::AIR;
    if (!tryGetDoorHalves(world, hitPos, lowerPos, lowerState, upperPos, upperState)) {
        return;
    }

    const StateID updatedLower = makeDoorState(
        BlockStateRegistry::getBlockId(lowerState),
        facingValue(lowerState),
        PropIndices::HALF_LOWER,
        hingeValue(lowerState),
        powered,
        powered);
    const StateID updatedUpper = makeDoorState(
        BlockStateRegistry::getBlockId(upperState),
        facingValue(upperState),
        PropIndices::HALF_UPPER,
        hingeValue(upperState),
        powered,
        powered);
    world.setBlockState(lowerPos.x, lowerPos.y, lowerPos.z, updatedLower);
    world.setBlockState(upperPos.x, upperPos.y, upperPos.z, updatedUpper);
}

BlockID removeDoor(World& world, const glm::ivec3& hitPos, std::vector<glm::ivec3>* removedPositions) {
    const StateID stateId = world.getBlockState(hitPos.x, hitPos.y, hitPos.z);
    if (!isDoorState(stateId)) {
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

} // namespace DoorBlockLogic
