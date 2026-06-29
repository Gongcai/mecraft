#include "WireContainerPlacement.h"

#include <array>
#include <stdexcept>

#include "../World.h"
#include "../block/Block.h"
#include "../block/PropIndices.h"
#include "../fluid/FluidState.h"
#include "WireContainerParts.h"
#include "WireFaceGeometry.h"

namespace {

bool isPlainWireState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "wire";
}

bool isWireContainerState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE || FluidState::decode(stateId).kind != FluidKind::None) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isWireContainer;
}

uint16_t requiredProperty(const BlockStateId stateId,
                          const uint16_t property,
                          const char* propertyName) {
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string("Wire container placement requires property: ") + propertyName);
    }
    return value;
}

std::array<uint16_t, 16> powerPropertyValues() {
    return {
        PropIndices::POWER_0,
        PropIndices::POWER_1,
        PropIndices::POWER_2,
        PropIndices::POWER_3,
        PropIndices::POWER_4,
        PropIndices::POWER_5,
        PropIndices::POWER_6,
        PropIndices::POWER_7,
        PropIndices::POWER_8,
        PropIndices::POWER_9,
        PropIndices::POWER_10,
        PropIndices::POWER_11,
        PropIndices::POWER_12,
        PropIndices::POWER_13,
        PropIndices::POWER_14,
        PropIndices::POWER_15,
    };
}

uint8_t powerFromState(const BlockStateId stateId) {
    const uint16_t value = requiredProperty(stateId, PropIndices::POWER, "power");
    const auto values = powerPropertyValues();
    for (uint8_t power = 0; power < values.size(); ++power) {
        if (value == values[power]) {
            return power;
        }
    }
    throw std::runtime_error("Wire container placement received an unknown redstone power value");
}

glm::ivec3 axis1PositiveOffset(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST || facing == PropIndices::FACING_WEST) {
        return {0, 0, 1};
    }
    if (WireFaceGeometry::isWireFacing(facing)) {
        return {1, 0, 0};
    }
    throw std::runtime_error("Wire container placement received an unsupported wire facing");
}

glm::ivec3 axis2PositiveOffset(const uint16_t facing) {
    if (facing == PropIndices::FACING_FLOOR || facing == PropIndices::FACING_CEILING) {
        return {0, 0, 1};
    }
    if (WireFaceGeometry::isWireFacing(facing)) {
        return {0, 1, 0};
    }
    throw std::runtime_error("Wire container placement received an unsupported wire facing");
}

uint8_t connectionBitForOffset(const uint16_t facing, const glm::ivec3& offset) {
    const glm::ivec3 axis1 = axis1PositiveOffset(facing);
    const glm::ivec3 axis2 = axis2PositiveOffset(facing);
    if (offset == axis1) {
        return WireConnectionBits::AXIS1_POS;
    }
    if (offset == -axis1) {
        return WireConnectionBits::AXIS1_NEG;
    }
    if (offset == axis2) {
        return WireConnectionBits::AXIS2_POS;
    }
    if (offset == -axis2) {
        return WireConnectionBits::AXIS2_NEG;
    }
    throw std::runtime_error("Wire container placement received a connection outside the wire plane");
}

uint8_t connectionsFromState(const BlockStateId stateId, const uint16_t facing) {
    uint8_t connections = 0;
    for (const WireFaceGeometry::ConnectionDirection& direction : WireFaceGeometry::connectionDirections(facing)) {
        const uint16_t value = requiredProperty(stateId, direction.property, "wire connection");
        if (value == direction.noneValue) {
            continue;
        }
        if (value != direction.sideValue) {
            throw std::runtime_error("Wire container placement requires none/side wire connection values");
        }
        connections |= connectionBitForOffset(facing, direction.offset);
    }
    return connections;
}

uint16_t wireFacingFromState(const BlockStateId stateId) {
    const uint16_t facing = requiredProperty(stateId, PropIndices::FACING, "facing");
    if (!WireFaceGeometry::isWireFacing(facing)) {
        throw std::runtime_error("Wire container placement received an unsupported wire facing");
    }
    return facing;
}

WirePart partFromWireState(const BlockStateId stateId, const bool preserveRuntimeValues) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneWireChannelId == 0) {
        throw std::runtime_error("Wire container placement requires a registered wire channel");
    }

    WirePart part;
    part.channelId = def.redstoneWireChannelId;
    part.facing = wireFacingFromState(stateId);
    if (preserveRuntimeValues) {
        part.power = powerFromState(stateId);
        part.connections = connectionsFromState(stateId, part.facing);
    }
    return part;
}

BlockStateId wireContainerDefaultState() {
    const BlockID blockId = BlockRegistry::requireIdByName("minecraft:wire_container");
    return BlockStateRegistry::getDefaultState(blockId);
}

BlockID requireWireBlockForChannel(const uint16_t channelId) {
    if (channelId == 0) {
        throw std::runtime_error("Wire container part requires a non-zero redstone channel");
    }

    BlockID matchedBlock = RUNTIME_ID_NULL;
    for (std::size_t i = 0; i < BlockRegistry::getBlockCount(); ++i) {
        const BlockID blockId = static_cast<BlockID>(i);
        const BlockDef& def = BlockRegistry::getFast(blockId);
        if (def.redstoneBehavior != "wire" || def.redstoneWireChannelId != channelId) {
            continue;
        }
        if (matchedBlock != RUNTIME_ID_NULL) {
            throw std::runtime_error("Wire container part channel maps to multiple wire blocks");
        }
        matchedBlock = blockId;
    }

    if (matchedBlock == RUNTIME_ID_NULL) {
        throw std::runtime_error("Wire container part channel has no registered wire block");
    }
    return matchedBlock;
}

} // namespace

namespace WireContainerPlacement {

bool isContainerPlacementTarget(const BlockStateId existingState, const BlockStateId incomingWireState) {
    return isPlainWireState(incomingWireState) &&
           (isPlainWireState(existingState) || isWireContainerState(existingState));
}

bool canApplyToBlockState(const BlockStateId existingState, const BlockStateId incomingWireState) {
    if (!isContainerPlacementTarget(existingState, incomingWireState)) {
        return false;
    }

    if (isPlainWireState(existingState)) {
        const WirePart existingPart = partFromWireState(existingState, true);
        const WirePart incomingPart = partFromWireState(incomingWireState, false);
        return !existingPart.sameSlot(incomingPart.channelId, incomingPart.facing);
    }

    return true;
}

bool canApply(const World& world,
              const glm::ivec3& position,
              const BlockStateId incomingWireState) {
    if (!world.isChunkLoadedForBlock(position.x, position.y, position.z)) {
        return false;
    }

    const BlockStateId existingState = world.getBlockState(position.x, position.y, position.z);
    if (!canApplyToBlockState(existingState, incomingWireState)) {
        return false;
    }

    const WirePart incomingPart = partFromWireState(incomingWireState, false);
    if (isPlainWireState(existingState)) {
        WireContainerParts parts;
        if (!parts.addPart(partFromWireState(existingState, true))) {
            return false;
        }
        return parts.addPart(incomingPart);
    }

    WireContainerParts parts;
    if (const WireContainerParts* existingParts = world.wireContainerParts().find(position)) {
        parts = *existingParts;
    }
    return parts.addPart(incomingPart);
}

ApplyResult apply(World& world,
                  const glm::ivec3& position,
                  const BlockStateId incomingWireState) {
    if (!isPlainWireState(incomingWireState)) {
        return ApplyResult::NotWirePlacement;
    }
    if (!world.isChunkLoadedForBlock(position.x, position.y, position.z)) {
        return ApplyResult::Rejected;
    }

    const BlockStateId existingState = world.getBlockState(position.x, position.y, position.z);
    const WirePart incomingPart = partFromWireState(incomingWireState, false);
    if (isPlainWireState(existingState)) {
        WireContainerParts parts;
        if (!parts.addPart(partFromWireState(existingState, true)) || !parts.addPart(incomingPart)) {
            return ApplyResult::Rejected;
        }

        if (world.wireContainerParts().find(position) != nullptr) {
            throw std::runtime_error("Plain wire cell unexpectedly has wire container parts");
        }
        world.wireContainerParts().getOrCreate(position) = parts;
        world.setBlockState(position.x, position.y, position.z, wireContainerDefaultState());
        world.notifyWireContainerPartsChanged(position);
        return ApplyResult::Applied;
    }

    if (isWireContainerState(existingState)) {
        WireContainerParts& parts = world.wireContainerParts().getOrCreate(position);
        if (!parts.addPart(incomingPart)) {
            return ApplyResult::Rejected;
        }
        world.notifyWireContainerPartsChanged(position);
        return ApplyResult::Applied;
    }

    return ApplyResult::NotWirePlacement;
}

std::vector<BlockID> wireBlocksForParts(const WireContainerParts& parts) {
    std::vector<BlockID> blockIds;
    blockIds.reserve(parts.size());
    parts.forEach([&](const WirePart& part) {
        blockIds.push_back(requireWireBlockForChannel(part.channelId));
    });
    return blockIds;
}

} // namespace WireContainerPlacement
