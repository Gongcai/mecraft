#include "RedstoneSystem.h"

#include "../../GameplayRegistry.h"
#include "../../util/AudioEventBuffer.h"
#include "../../../game/inventory/ChestInventoryStore.h"
#include "../../../game/inventory/FurnaceInventoryStore.h"
#include "../../../item/Item.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/RedstoneUpdateQueue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

namespace {

constexpr uint8_t kMaxRedstonePower = 15;
constexpr uint64_t kStoneButtonPulseTicks = 10;
constexpr uint64_t kWoodButtonPulseTicks = 15;
constexpr uint64_t kObserverPulseDelayTicks = 1;
constexpr uint64_t kObserverPulseDurationTicks = 1;
constexpr size_t kMaxPistonPushBlocks = 12;
constexpr glm::ivec3 kDirections[6] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  1,  0},
    { 0, -1,  0},
    { 0,  0,  1},
    { 0,  0, -1},
};

// The four horizontal directions used for redstone wire climbing checks.
constexpr glm::ivec3 kHorizontalDirections[4] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  0,  1},
    { 0,  0, -1},
};

struct IVec3Hash {
    std::size_t operator()(const glm::ivec3& value) const noexcept {
        std::size_t seed = 0;
        const auto mix = [&seed](const int component) {
            seed ^= std::hash<int>{}(component) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
        mix(value.x);
        mix(value.y);
        mix(value.z);
        return seed;
    }
};

using WirePowerMap = std::unordered_map<glm::ivec3, uint8_t, IVec3Hash>;
using PositionSet = std::unordered_set<glm::ivec3, IVec3Hash>;

struct RedstoneSource {
    glm::ivec3 position;
    uint8_t power = 0;
    glm::ivec3 outputDirection{};
    bool directional = false;
};

struct RedstoneWorkSet {
    std::vector<glm::ivec3> wires;
    std::vector<glm::ivec3> redstoneControlledBlocks;
    std::vector<glm::ivec3> torches;
    std::vector<glm::ivec3> repeaters;
    std::vector<glm::ivec3> observers;
    std::vector<glm::ivec3> comparators;
    std::vector<glm::ivec3> pistons;
    std::vector<glm::ivec3> edgeTriggeredDevices;
    std::vector<glm::ivec3> sourcePositions;
    PositionSet wireSet;
    PositionSet redstoneControlledSet;
    PositionSet torchSet;
    PositionSet repeaterSet;
    PositionSet observerSet;
    PositionSet comparatorSet;
    PositionSet pistonSet;
    PositionSet edgeTriggeredDeviceSet;
    PositionSet sourceSet;
};

struct PowerNode {
    glm::ivec3 position;
    uint8_t power = 0;

    bool operator<(const PowerNode& other) const {
        return power < other.power;
    }
};

struct ComparatorEvaluation {
    glm::ivec3 position;
    uint8_t outputPower = 0;
};

struct PistonMovedBlock {
    glm::ivec3 position;
    StateID state = BlockIds::AIR;
};

struct PistonPushPlan {
    std::vector<PistonMovedBlock> movedBlocks;
};

bool isWireState(const StateID stateId) {
    return stateId != BlockIds::AIR &&
           BlockStateRegistry::getBlockId(stateId) == BlockIds::REDSTONE_WIRE;
}

uint16_t getRequiredProperty(const StateID stateId, const uint16_t property, const char* propertyName) {
    if (property == PropIndices::INVALID) {
        throw std::runtime_error(std::string("Redstone requires registered property: ") + propertyName);
    }
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string("Redstone state is missing property: ") + propertyName);
    }
    return value;
}

StateID withRequiredProperty(const StateID stateId,
                             const uint16_t property,
                             const uint16_t value,
                             const char* propertyName) {
    const uint16_t current = getRequiredProperty(stateId, property, propertyName);
    if (current == value) {
        return stateId;
    }

    const StateID updated = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updated, property) != value) {
        throw std::runtime_error(std::string("Redstone failed to update property: ") + propertyName);
    }
    return updated;
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

uint16_t powerToPropertyValue(const uint8_t power) {
    if (power > kMaxRedstonePower) {
        throw std::runtime_error("Redstone wire power exceeds 15");
    }
    const auto values = powerPropertyValues();
    const uint16_t value = values[power];
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered power values 0 through 15");
    }
    return value;
}

uint8_t redstonePowerFromState(const StateID stateId) {
    const uint16_t value = getRequiredProperty(stateId, PropIndices::POWER, "power");
    const auto values = powerPropertyValues();
    for (uint8_t power = 0; power < values.size(); ++power) {
        if (value == values[power]) {
            return power;
        }
    }
    throw std::runtime_error("Redstone state contains an unknown power value");
}

StateID withRedstonePower(const StateID stateId, const uint8_t power) {
    return withRequiredProperty(stateId, PropIndices::POWER, powerToPropertyValue(power), "power");
}

StateID withLit(const StateID stateId, const bool lit) {
    const uint16_t value = lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered lit boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::LIT, value, "lit");
}

StateID withPowered(const StateID stateId, const bool powered) {
    const uint16_t value = powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered powered boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::POWERED, value, "powered");
}

bool hasBooleanPropertyValue(const StateID stateId,
                             const uint16_t property,
                             const uint16_t expectedValue,
                             const char* propertyName) {
    return getRequiredProperty(stateId, property, propertyName) == expectedValue;
}

uint8_t sourceOutputPower(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return 0;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!def.isRedstonePowerSource) {
        return 0;
    }

    if (def.redstoneBehavior == "lever" ||
        def.redstoneBehavior == "button" ||
        def.redstoneBehavior == "plate") {
        return hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered")
            ? def.redstonePowerOutput
            : 0;
    }

    if (def.redstoneBehavior == "torch") {
        return hasBooleanPropertyValue(stateId, PropIndices::LIT, PropIndices::LIT_TRUE, "lit")
            ? def.redstonePowerOutput
            : 0;
    }

    if (def.redstoneBehavior == "power_block") {
        return def.redstonePowerOutput;
    }

    if (def.redstoneBehavior == "target") {
        return redstonePowerFromState(stateId);
    }

    throw std::runtime_error("Unsupported redstone power source behavior: " + def.redstoneBehavior);
}

bool sourceCanPowerConductiveBlocks(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!def.isRedstonePowerSource) {
        return false;
    }

    if (def.redstoneBehavior == "lever" ||
        def.redstoneBehavior == "button" ||
        def.redstoneBehavior == "plate" ||
        def.redstoneBehavior == "power_block" ||
        def.redstoneBehavior == "target") {
        return true;
    }

    if (def.redstoneBehavior == "torch") {
        return false;
    }

    throw std::runtime_error("Unsupported redstone power source behavior: " + def.redstoneBehavior);
}

bool isHorizontalDirection(const glm::ivec3& direction) {
    return (direction == glm::ivec3(1, 0, 0)) ||
           (direction == glm::ivec3(-1, 0, 0)) ||
           (direction == glm::ivec3(0, 0, 1)) ||
           (direction == glm::ivec3(0, 0, -1));
}

bool wirePowersConductorToward(const World& world,
                               const glm::ivec3& wirePosition,
                               const glm::ivec3& conductorPosition) {
    const StateID wireState = world.getBlockState(wirePosition.x, wirePosition.y, wirePosition.z);
    if (!isWireState(wireState)) {
        return false;
    }

    const glm::ivec3 offset = conductorPosition - wirePosition;
    if (offset == glm::ivec3(0, -1, 0)) {
        return true;
    }
    if (!isHorizontalDirection(offset)) {
        return false;
    }
    return true;
}

template <typename StackReader>
uint8_t inventorySignalPower(const int slotCount, StackReader&& stackReader) {
    if (slotCount <= 0) {
        throw std::runtime_error("Redstone container signal requires at least one inventory slot");
    }

    double occupiedFractionSum = 0.0;
    bool hasItems = false;
    for (int slot = 0; slot < slotCount; ++slot) {
        const ItemStack stack = stackReader(slot);
        if (stack.isEmpty()) {
            continue;
        }

        const ItemDef& itemDef = ItemRegistry::get(stack.itemId);
        if (itemDef.maxStack == 0) {
            throw std::runtime_error("Redstone container signal found an item with no valid stack size");
        }
        if (stack.count > itemDef.maxStack) {
            throw std::runtime_error("Redstone container signal found a stack above its maximum size");
        }

        occupiedFractionSum += static_cast<double>(stack.count) / static_cast<double>(itemDef.maxStack);
        hasItems = true;
    }

    if (!hasItems) {
        return 0;
    }

    const double fullness = occupiedFractionSum / static_cast<double>(slotCount);
    return static_cast<uint8_t>(std::floor(fullness * 14.0) + 1.0);
}

BlockID furnaceBlockId() {
    static const BlockID blockId = BlockRegistry::findByName("furnace");
    return blockId;
}

uint8_t containerSignalPowerAt(const World& world,
                               const GameplayRegistry* registry,
                               const glm::ivec3& position) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (stateId == BlockIds::AIR) {
        return 0;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    if (blockId == BlockIds::CHEST) {
        if (registry == nullptr || !registry->ctxHas<ChestInventoryStore>()) {
            return 0;
        }
        const ChestInventoryStore& store = registry->ctxGet<ChestInventoryStore>();
        const ChestInventory* chest = store.find(position);
        if (chest == nullptr) {
            return 0;
        }
        return inventorySignalPower(ChestInventory::SLOT_COUNT, [chest](const int slot) {
            return chest->getSlotStack(slot);
        });
    }

    if (blockId == furnaceBlockId()) {
        if (registry == nullptr || !registry->ctxHas<FurnaceInventoryStore>()) {
            return 0;
        }
        const FurnaceInventoryStore& store = registry->ctxGet<FurnaceInventoryStore>();
        const FurnaceInventory* furnace = store.find(position);
        if (furnace == nullptr) {
            return 0;
        }
        return inventorySignalPower(FurnaceInventory::SLOT_COUNT, [furnace](const int slot) {
            return furnace->getSlotStack(slot);
        });
    }

    return 0;
}

bool isPotentialSourceState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstonePowerSource;
}

bool isConductiveState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstoneConductor;
}

bool isConductiveBlockAt(const World& world, const glm::ivec3& position) {
    return isConductiveState(world.getBlockState(position.x, position.y, position.z));
}

// Returns true when the given state is a solid block that redstone wire can
// climb over. This mirrors the World.cpp climbing check: air and non-solid
// blocks (including redstone wire itself) do not support climbing.
bool isSolidBlockState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isSolid;
}

// Calls fn for every wire position connected to the wire at pos, including
// diagonal up/down connections where the wire climbs over a solid block or
// descends through a non-solid block. The 6 cardinal directions cover same-
// level and directly-above/below wire neighbors; the horizontal loop adds the
// diagonal climbing/descending connections that vanilla redstone supports.
template <typename Fn>
void forEachWireNeighbor(const World& world, const glm::ivec3& pos, Fn&& fn) {
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = pos + direction;
        if (isWireState(world.getBlockState(neighbor.x, neighbor.y, neighbor.z))) {
            fn(neighbor);
        }
    }
    for (const glm::ivec3& hDir : kHorizontalDirections) {
        const glm::ivec3 neighbor = pos + hDir;
        const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        if (isSolidBlockState(neighborState)) {
            const glm::ivec3 upPos = neighbor + glm::ivec3(0, 1, 0);
            if (isWireState(world.getBlockState(upPos.x, upPos.y, upPos.z))) {
                fn(upPos);
            }
        } else {
            const glm::ivec3 downPos = neighbor + glm::ivec3(0, -1, 0);
            if (isWireState(world.getBlockState(downPos.x, downPos.y, downPos.z))) {
                fn(downPos);
            }
        }
    }
}

bool isRedstoneControlledState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.respondsToRedstone && !def.redstoneControlledProperty.empty();
}

bool isTorchState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "torch";
}

bool isButtonState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "button";
}

bool isRepeaterState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "repeater";
}

bool isObserverState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "observer";
}

bool isComparatorState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "comparator";
}

bool isPistonState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "piston";
}

bool isEdgeTriggeredDeviceState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "note_block";
}

bool isTargetState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "target";
}

bool isPistonHeadState(const StateID stateId) {
    return stateId != BlockIds::AIR &&
           BlockStateRegistry::getBlockId(stateId) == BlockIds::PISTON_HEAD;
}

uint64_t buttonPulseTicks(const StateID stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    if (blockId == BlockIds::STONE_BUTTON) {
        return kStoneButtonPulseTicks;
    }
    if (blockId == BlockIds::OAK_BUTTON) {
        return kWoodButtonPulseTicks;
    }
    throw std::runtime_error("Redstone button has no registered pulse duration");
}

uint64_t repeaterDelayTicks(const StateID stateId) {
    const uint16_t delay = getRequiredProperty(stateId, PropIndices::DELAY, "delay");
    if (delay == PropIndices::DELAY_1) {
        return 1;
    }
    if (delay == PropIndices::DELAY_2) {
        return 2;
    }
    if (delay == PropIndices::DELAY_3) {
        return 3;
    }
    if (delay == PropIndices::DELAY_4) {
        return 4;
    }
    throw std::runtime_error("Repeater state contains an unknown delay value");
}

glm::ivec3 horizontalDirectionFromFacing(const uint16_t facing) {
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
    throw std::runtime_error("Horizontal redstone device state contains a non-horizontal facing value");
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
    throw std::runtime_error("Observer state contains an unknown facing value");
}

glm::ivec3 repeaterOutputDirection(const StateID stateId) {
    return horizontalDirectionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

glm::ivec3 comparatorOutputDirection(const StateID stateId) {
    return horizontalDirectionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

std::array<glm::ivec3, 2> comparatorSideDirections(const StateID stateId) {
    const glm::ivec3 outputDirection = comparatorOutputDirection(stateId);
    if (outputDirection.x != 0) {
        return {glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)};
    }
    return {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0)};
}

bool comparatorUsesSubtractMode(const StateID stateId) {
    const uint16_t mode = getRequiredProperty(stateId, PropIndices::MODE, "mode");
    if (mode == PropIndices::MODE_COMPARE) {
        return false;
    }
    if (mode == PropIndices::MODE_SUBTRACT) {
        return true;
    }
    throw std::runtime_error("Comparator state contains an unknown mode value");
}

glm::ivec3 observerFacingDirection(const StateID stateId) {
    return directionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

glm::ivec3 observerOutputDirection(const StateID stateId) {
    return -observerFacingDirection(stateId);
}

bool isPoweredPropertyTrue(const StateID stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered");
}

uint16_t redstoneControlledPropertyIndex(const StateID stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!def.respondsToRedstone || def.redstoneControlledProperty.empty()) {
        throw std::runtime_error(
            "Redstone controlled state requires respondsToRedstone and redstoneControlledProperty");
    }

    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(def.redstoneControlledProperty);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(
            "Redstone controlled property is not registered: " + def.redstoneControlledProperty);
    }

    const uint16_t currentValue = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (currentValue == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(
            "Redstone controlled block state is missing property: " + def.redstoneControlledProperty);
    }

    const uint16_t falseValue = BlockStateRegistry::getPropertyValueIndex(property, "false");
    const uint16_t trueValue = BlockStateRegistry::getPropertyValueIndex(property, "true");
    if (falseValue == BlockStateRegistry::INVALID_INDEX || trueValue == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(
            "Redstone controlled property must define false and true values: " + def.redstoneControlledProperty);
    }
    if (currentValue != falseValue && currentValue != trueValue) {
        throw std::runtime_error(
            "Redstone controlled property state must be false or true: " + def.redstoneControlledProperty);
    }
    return property;
}

StateID withRedstoneControlledPower(const StateID stateId, const bool powered) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    const uint16_t property = redstoneControlledPropertyIndex(stateId);
    const bool propertyValue = powered != def.redstoneControlledPowerInverted;
    const uint16_t value = BlockStateRegistry::getPropertyValueIndex(property, propertyValue ? "true" : "false");
    const StateID updatedState = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updatedState, property) != value) {
        throw std::runtime_error("Redstone controlled state transition failed");
    }
    return updatedState;
}

bool isExtendedPropertyTrue(const StateID stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::EXTENDED, PropIndices::EXTENDED_TRUE, "extended");
}

StateID withExtended(const StateID stateId, const bool extended) {
    const uint16_t value = extended ? PropIndices::EXTENDED_TRUE : PropIndices::EXTENDED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered extended boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::EXTENDED, value, "extended");
}

glm::ivec3 pistonFacingDirection(const StateID stateId) {
    return directionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

uint16_t pistonHeadType(const StateID pistonState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(pistonState);
    if (blockId == BlockIds::PISTON) {
        return PropIndices::TYPE_NORMAL;
    }
    if (blockId == BlockIds::STICKY_PISTON) {
        return PropIndices::TYPE_STICKY;
    }
    throw std::runtime_error("Piston head type requested for a non-piston block");
}

StateID pistonHeadState(const StateID pistonState) {
    return BlockStateRegistry::getState(
        BlockIds::PISTON_HEAD,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, getRequiredProperty(pistonState, PropIndices::FACING, "facing")},
            {PropIndices::TYPE, pistonHeadType(pistonState)}
        });
}

bool isMatchingPistonHead(const StateID headState, const StateID pistonState) {
    return isPistonHeadState(headState) &&
           getRequiredProperty(headState, PropIndices::FACING, "facing") ==
               getRequiredProperty(pistonState, PropIndices::FACING, "facing") &&
           getRequiredProperty(headState, PropIndices::TYPE, "type") == pistonHeadType(pistonState);
}

BlockID obsidianBlockId() {
    static const BlockID blockId = BlockRegistry::findByName("obsidian");
    return blockId;
}

bool isImmovablePistonBlock(const StateID stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    if (blockId == BlockIds::BEDROCK ||
        blockId == obsidianBlockId() ||
        blockId == BlockIds::PISTON_HEAD) {
        return true;
    }
    return isPistonState(stateId) && isExtendedPropertyTrue(stateId);
}

bool isMovablePistonBlock(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    if (isImmovablePistonBlock(stateId)) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isSolid;
}

bool buildPistonPushPlan(const World& world,
                         const glm::ivec3& pistonPosition,
                         const glm::ivec3& pushDirection,
                         PistonPushPlan& plan) {
    plan.movedBlocks.clear();

    for (int distance = 1; distance <= static_cast<int>(kMaxPistonPushBlocks) + 1; ++distance) {
        const glm::ivec3 position = pistonPosition + pushDirection * distance;
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (stateId == BlockIds::AIR) {
            return true;
        }
        if (!isMovablePistonBlock(stateId)) {
            return false;
        }
        if (plan.movedBlocks.size() == kMaxPistonPushBlocks) {
            return false;
        }
        plan.movedBlocks.push_back({position, stateId});
    }

    throw std::runtime_error("Piston push scan exceeded its configured maximum distance");
}

size_t applyPistonPush(World& world,
                       const glm::ivec3& pistonPosition,
                       const StateID pistonState,
                       const glm::ivec3& pushDirection,
                       const PistonPushPlan& plan) {
    size_t changed = 0;
    for (auto it = plan.movedBlocks.rbegin(); it != plan.movedBlocks.rend(); ++it) {
        const glm::ivec3 target = it->position + pushDirection;
        world.setBlockState(target.x, target.y, target.z, it->state);
        ++changed;
    }

    const glm::ivec3 frontPosition = pistonPosition + pushDirection;
    world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, pistonHeadState(pistonState));
    world.setBlockState(pistonPosition.x, pistonPosition.y, pistonPosition.z, withExtended(pistonState, true));
    return changed + 2;
}

bool isStickyPistonState(const StateID stateId) {
    return BlockStateRegistry::getBlockId(stateId) == BlockIds::STICKY_PISTON;
}

size_t applyPistonRetraction(World& world,
                             const glm::ivec3& pistonPosition,
                             const StateID pistonState,
                             const glm::ivec3& facingDirection) {
    size_t changed = 0;
    const glm::ivec3 frontPosition = pistonPosition + facingDirection;
    const StateID frontState = world.getBlockState(frontPosition.x, frontPosition.y, frontPosition.z);
    const bool removedMatchingHead = isMatchingPistonHead(frontState, pistonState);
    if (removedMatchingHead) {
        world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, BlockIds::AIR);
        ++changed;
    }

    if (removedMatchingHead && isStickyPistonState(pistonState)) {
        const glm::ivec3 pullPosition = frontPosition + facingDirection;
        const StateID pullState = world.getBlockState(pullPosition.x, pullPosition.y, pullPosition.z);
        if (isMovablePistonBlock(pullState)) {
            world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, pullState);
            world.setBlockState(pullPosition.x, pullPosition.y, pullPosition.z, BlockIds::AIR);
            changed += 2;
        }
    }

    world.setBlockState(pistonPosition.x, pistonPosition.y, pistonPosition.z, withExtended(pistonState, false));
    return changed + 1;
}

void addRedstoneControlledIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isRedstoneControlledState(stateId)) {
        return;
    }
    if (workSet.redstoneControlledSet.insert(position).second) {
        workSet.redstoneControlledBlocks.push_back(position);
    }
}

void addTorchIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isTorchState(stateId)) {
        return;
    }
    if (workSet.torchSet.insert(position).second) {
        workSet.torches.push_back(position);
    }
}

void addRepeaterIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isRepeaterState(stateId)) {
        return;
    }
    if (workSet.repeaterSet.insert(position).second) {
        workSet.repeaters.push_back(position);
    }
}

void addObserverIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(stateId)) {
        return;
    }
    if (workSet.observerSet.insert(position).second) {
        workSet.observers.push_back(position);
    }
}

void addComparatorIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isComparatorState(stateId)) {
        return;
    }
    if (workSet.comparatorSet.insert(position).second) {
        workSet.comparators.push_back(position);
    }
}

void addPistonIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isPistonState(stateId)) {
        return;
    }
    if (workSet.pistonSet.insert(position).second) {
        workSet.pistons.push_back(position);
    }
}

void addEdgeTriggeredDeviceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isEdgeTriggeredDeviceState(stateId)) {
        return;
    }
    if (workSet.edgeTriggeredDeviceSet.insert(position).second) {
        workSet.edgeTriggeredDevices.push_back(position);
    }
}

void addSourceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isPotentialSourceState(stateId)) {
        return;
    }
    if (workSet.sourceSet.insert(position).second) {
        workSet.sourcePositions.push_back(position);
    }
}

void collectEndpointsAround(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet);

void collectWireComponent(const World& world, const glm::ivec3& start, RedstoneWorkSet& workSet) {
    if (!isWireState(world.getBlockState(start.x, start.y, start.z))) {
        return;
    }
    if (!workSet.wireSet.insert(start).second) {
        return;
    }

    std::queue<glm::ivec3> frontier;
    frontier.push(start);
    workSet.wires.push_back(start);

    while (!frontier.empty()) {
        const glm::ivec3 position = frontier.front();
        frontier.pop();

        // Discover all connected wires, including diagonal up/down climbing
        // connections that vanilla redstone wire supports.
        forEachWireNeighbor(world, position, [&](const glm::ivec3& wireNeighbor) {
            if (workSet.wireSet.insert(wireNeighbor).second) {
                workSet.wires.push_back(wireNeighbor);
                frontier.push(wireNeighbor);
            }
        });

        // Check cardinal direction neighbors for non-wire redstone components.
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                continue;
            }
            if (isConductiveState(neighborState)) {
                collectEndpointsAround(world, neighbor, workSet);
            }

            addRedstoneControlledIfPresent(world, neighbor, workSet);
            if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            }
            if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            }
            if (isObserverState(neighborState)) {
                addObserverIfPresent(world, neighbor, workSet);
            }
            if (isComparatorState(neighborState)) {
                addComparatorIfPresent(world, neighbor, workSet);
            }
            if (isPistonState(neighborState)) {
                addPistonIfPresent(world, neighbor, workSet);
            }
            if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            }
            if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
    }
}

void collectEndpointIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (isWireState(stateId)) {
        collectWireComponent(world, position, workSet);
    } else if (isRedstoneControlledState(stateId)) {
        addRedstoneControlledIfPresent(world, position, workSet);
    } else if (isTorchState(stateId)) {
        addTorchIfPresent(world, position, workSet);
    } else if (isRepeaterState(stateId)) {
        addRepeaterIfPresent(world, position, workSet);
    } else if (isObserverState(stateId)) {
        addObserverIfPresent(world, position, workSet);
    } else if (isComparatorState(stateId)) {
        addComparatorIfPresent(world, position, workSet);
    } else if (isPistonState(stateId)) {
        addPistonIfPresent(world, position, workSet);
    } else if (isEdgeTriggeredDeviceState(stateId)) {
        addEdgeTriggeredDeviceIfPresent(world, position, workSet);
    } else if (isPotentialSourceState(stateId)) {
        addSourceIfPresent(world, position, workSet);
    }
}

void collectEndpointsAround(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    for (const glm::ivec3& direction : kDirections) {
        collectEndpointIfPresent(world, position + direction, workSet);
    }
}

void collectEndpointsAroundNeighborConductors(const World& world,
                                              const glm::ivec3& position,
                                              RedstoneWorkSet& workSet) {
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 conductor = position + direction;
        if (isConductiveBlockAt(world, conductor)) {
            collectEndpointsAround(world, conductor, workSet);
        }
    }
}

void collectPosition(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (isWireState(stateId)) {
        collectWireComponent(world, position, workSet);
        return;
    }

    if (isConductiveState(stateId)) {
        if (isPotentialSourceState(stateId)) {
            addSourceIfPresent(world, position, workSet);
        }
        collectEndpointsAround(world, position, workSet);
    }

    addRedstoneControlledIfPresent(world, position, workSet);

    if (isTorchState(stateId)) {
        addTorchIfPresent(world, position, workSet);
    }

    if (isRepeaterState(stateId)) {
        addRepeaterIfPresent(world, position, workSet);
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                collectWireComponent(world, neighbor, workSet);
            } else if (isRedstoneControlledState(neighborState)) {
                addRedstoneControlledIfPresent(world, neighbor, workSet);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isObserverState(neighborState)) {
                addObserverIfPresent(world, neighbor, workSet);
            } else if (isComparatorState(neighborState)) {
                addComparatorIfPresent(world, neighbor, workSet);
            } else if (isPistonState(neighborState)) {
                addPistonIfPresent(world, neighbor, workSet);
            } else if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
        collectEndpointsAroundNeighborConductors(world, position, workSet);
        return;
    }

    if (isObserverState(stateId)) {
        addObserverIfPresent(world, position, workSet);
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                collectWireComponent(world, neighbor, workSet);
            } else if (isRedstoneControlledState(neighborState)) {
                addRedstoneControlledIfPresent(world, neighbor, workSet);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            } else if (isComparatorState(neighborState)) {
                addComparatorIfPresent(world, neighbor, workSet);
            } else if (isPistonState(neighborState)) {
                addPistonIfPresent(world, neighbor, workSet);
            } else if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
        collectEndpointsAroundNeighborConductors(world, position, workSet);
        return;
    }

    if (isComparatorState(stateId)) {
        addComparatorIfPresent(world, position, workSet);
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                collectWireComponent(world, neighbor, workSet);
            } else if (isRedstoneControlledState(neighborState)) {
                addRedstoneControlledIfPresent(world, neighbor, workSet);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            } else if (isObserverState(neighborState)) {
                addObserverIfPresent(world, neighbor, workSet);
            } else if (isPistonState(neighborState)) {
                addPistonIfPresent(world, neighbor, workSet);
            } else if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
        collectEndpointsAroundNeighborConductors(world, position, workSet);
        return;
    }

    if (isPistonState(stateId)) {
        addPistonIfPresent(world, position, workSet);
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                collectWireComponent(world, neighbor, workSet);
            } else if (isRedstoneControlledState(neighborState)) {
                addRedstoneControlledIfPresent(world, neighbor, workSet);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            } else if (isObserverState(neighborState)) {
                addObserverIfPresent(world, neighbor, workSet);
            } else if (isComparatorState(neighborState)) {
                addComparatorIfPresent(world, neighbor, workSet);
            } else if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
        collectEndpointsAroundNeighborConductors(world, position, workSet);
        return;
    }

    if (isEdgeTriggeredDeviceState(stateId)) {
        addEdgeTriggeredDeviceIfPresent(world, position, workSet);
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                collectWireComponent(world, neighbor, workSet);
            } else if (isRedstoneControlledState(neighborState)) {
                addRedstoneControlledIfPresent(world, neighbor, workSet);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            } else if (isObserverState(neighborState)) {
                addObserverIfPresent(world, neighbor, workSet);
            } else if (isComparatorState(neighborState)) {
                addComparatorIfPresent(world, neighbor, workSet);
            } else if (isPistonState(neighborState)) {
                addPistonIfPresent(world, neighbor, workSet);
            } else if (isEdgeTriggeredDeviceState(neighborState)) {
                addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
        collectEndpointsAroundNeighborConductors(world, position, workSet);
        return;
    }

    if (!isPotentialSourceState(stateId)) {
        return;
    }

    addSourceIfPresent(world, position, workSet);
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = position + direction;
        const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        if (isWireState(neighborState)) {
            collectWireComponent(world, neighbor, workSet);
        } else if (isRedstoneControlledState(neighborState)) {
            addRedstoneControlledIfPresent(world, neighbor, workSet);
        } else if (isTorchState(neighborState)) {
            addTorchIfPresent(world, neighbor, workSet);
        } else if (isRepeaterState(neighborState)) {
            addRepeaterIfPresent(world, neighbor, workSet);
        } else if (isObserverState(neighborState)) {
            addObserverIfPresent(world, neighbor, workSet);
        } else if (isComparatorState(neighborState)) {
            addComparatorIfPresent(world, neighbor, workSet);
        } else if (isPistonState(neighborState)) {
            addPistonIfPresent(world, neighbor, workSet);
        } else if (isEdgeTriggeredDeviceState(neighborState)) {
            addEdgeTriggeredDeviceIfPresent(world, neighbor, workSet);
        }
    }
    collectEndpointsAroundNeighborConductors(world, position, workSet);
}

RedstoneWorkSet collectRedstoneWorkSet(const World& world, const std::vector<glm::ivec3>& dirtyPositions) {
    RedstoneWorkSet workSet;
    for (const glm::ivec3& dirtyPosition : dirtyPositions) {
        collectPosition(world, dirtyPosition, workSet);
        for (const glm::ivec3& direction : kDirections) {
            collectPosition(world, dirtyPosition + direction, workSet);
        }
    }
    return workSet;
}

std::vector<RedstoneSource> collectActiveSources(const World& world,
                                                 const std::vector<glm::ivec3>& sourcePositions,
                                                 const PositionSet* excludedPositions) {
    std::vector<RedstoneSource> sources;
    sources.reserve(sourcePositions.size());
    for (const glm::ivec3& position : sourcePositions) {
        if (excludedPositions != nullptr && excludedPositions->find(position) != excludedPositions->end()) {
            continue;
        }

        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isPotentialSourceState(stateId)) {
            continue;
        }

        const uint8_t power = sourceOutputPower(stateId);
        if (power > 0) {
            sources.push_back({position, power, {}, false});
        }
    }
    return sources;
}

std::vector<RedstoneSource> collectPoweredRepeaterSources(const World& world,
                                                          const std::vector<glm::ivec3>& repeaterPositions) {
    std::vector<RedstoneSource> sources;
    sources.reserve(repeaterPositions.size());
    for (const glm::ivec3& position : repeaterPositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isRepeaterState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, repeaterOutputDirection(stateId), true});
    }
    return sources;
}

std::vector<RedstoneSource> collectPoweredObserverSources(const World& world,
                                                          const std::vector<glm::ivec3>& observerPositions) {
    std::vector<RedstoneSource> sources;
    sources.reserve(observerPositions.size());
    for (const glm::ivec3& position : observerPositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isObserverState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, observerOutputDirection(stateId), true});
    }
    return sources;
}

std::vector<RedstoneSource> collectPoweredComparatorStateSources(
    const World& world,
    const std::vector<glm::ivec3>& comparatorPositions) {
    std::vector<RedstoneSource> sources;
    sources.reserve(comparatorPositions.size());
    for (const glm::ivec3& position : comparatorPositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isComparatorState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, comparatorOutputDirection(stateId), true});
    }
    return sources;
}

std::vector<RedstoneSource> collectComparatorSources(const World& world,
                                                     const std::vector<ComparatorEvaluation>& evaluations) {
    std::vector<RedstoneSource> sources;
    sources.reserve(evaluations.size());
    for (const ComparatorEvaluation& evaluation : evaluations) {
        if (evaluation.outputPower == 0) {
            continue;
        }
        const StateID stateId = world.getBlockState(
            evaluation.position.x,
            evaluation.position.y,
            evaluation.position.z);
        if (!isComparatorState(stateId)) {
            continue;
        }
        sources.push_back({
            evaluation.position,
            evaluation.outputPower,
            comparatorOutputDirection(stateId),
            true
        });
    }
    return sources;
}

void appendSources(std::vector<RedstoneSource>& target, std::vector<RedstoneSource> sources) {
    target.reserve(target.size() + sources.size());
    for (const RedstoneSource& source : sources) {
        target.push_back(source);
    }
}

void setBestWirePower(WirePowerMap& wirePowers,
                      std::priority_queue<PowerNode>& frontier,
                      const glm::ivec3& position,
                      const uint8_t power) {
    auto [it, inserted] = wirePowers.emplace(position, power);
    if (!inserted && it->second >= power) {
        return;
    }
    it->second = power;
    frontier.push({position, power});
}

void seedTrackedWirePower(WirePowerMap& wirePowers,
                          std::priority_queue<PowerNode>& frontier,
                          const PositionSet& wires,
                          const glm::ivec3& position,
                          const uint8_t power) {
    if (wires.find(position) != wires.end()) {
        setBestWirePower(wirePowers, frontier, position, power);
    }
}

void seedWiresPoweredByConductor(const World& world,
                                 WirePowerMap& wirePowers,
                                 std::priority_queue<PowerNode>& frontier,
                                 const PositionSet& wires,
                                 const glm::ivec3& conductor,
                                 const glm::ivec3& signalPosition,
                                 const uint8_t power) {
    if (!isConductiveBlockAt(world, conductor)) {
        return;
    }

    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 wirePosition = conductor + direction;
        if (wirePosition == signalPosition) {
            continue;
        }
        seedTrackedWirePower(wirePowers, frontier, wires, wirePosition, power);
    }
}

void seedSourcePowerToward(const World& world,
                           WirePowerMap& wirePowers,
                           std::priority_queue<PowerNode>& frontier,
                           const PositionSet& wires,
                           const RedstoneSource& source,
                           const glm::ivec3& target) {
    seedTrackedWirePower(wirePowers, frontier, wires, target, source.power);
    const StateID sourceState = world.getBlockState(source.position.x, source.position.y, source.position.z);
    const bool stronglyPowersTarget =
        source.directional ||
        (isTorchState(sourceState) && target == source.position + glm::ivec3(0, 1, 0)) ||
        sourceCanPowerConductiveBlocks(sourceState);
    if (stronglyPowersTarget) {
        seedWiresPoweredByConductor(world, wirePowers, frontier, wires, target, source.position, source.power);
    }
}

WirePowerMap propagateWirePower(const World& world,
                                const PositionSet& wires,
                                const std::vector<RedstoneSource>& sources) {
    WirePowerMap wirePowers;
    std::priority_queue<PowerNode> frontier;

    // Pre-seed every wire in the work set with 0 so the map fully covers the
    // connected component being evaluated. Wires not reached by any source are
    // unambiguously at 0 power; downstream readers can then distinguish "in the
    // work set, carries 0" from "outside the work set, stable stored state" by
    // map membership alone. Readers use stored wire power only for positions
    // outside the evaluated component boundary.
    wirePowers.reserve(wires.size());
    for (const glm::ivec3& wire : wires) {
        wirePowers.emplace(wire, uint8_t{0});
    }

    for (const RedstoneSource& source : sources) {
        if (source.directional) {
            seedSourcePowerToward(
                world,
                wirePowers,
                frontier,
                wires,
                source,
                source.position + source.outputDirection);
        } else {
            for (const glm::ivec3& direction : kDirections) {
                seedSourcePowerToward(world, wirePowers, frontier, wires, source, source.position + direction);
            }
        }
    }

    while (!frontier.empty()) {
        const PowerNode node = frontier.top();
        frontier.pop();

        const auto currentIt = wirePowers.find(node.position);
        if (currentIt == wirePowers.end() || currentIt->second != node.power) {
            continue;
        }
        if (node.power == 0) {
            continue;
        }

        const uint8_t nextPower = static_cast<uint8_t>(node.power - 1);
        forEachWireNeighbor(world, node.position, [&](const glm::ivec3& neighbor) {
            if (wires.find(neighbor) != wires.end()) {
                setBestWirePower(wirePowers, frontier, neighbor, nextPower);
            }
        });
    }

    return wirePowers;
}

glm::ivec3 attachedBlockForTorch(const StateID stateId, const glm::ivec3& position) {
    const uint16_t facing = getRequiredProperty(stateId, PropIndices::FACING, "facing");
    if (facing == PropIndices::FACING_FLOOR) {
        return position + glm::ivec3(0, -1, 0);
    }
    if (facing == PropIndices::FACING_NORTH) {
        return position + glm::ivec3(0, 0, 1);
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return position + glm::ivec3(0, 0, -1);
    }
    if (facing == PropIndices::FACING_EAST) {
        return position + glm::ivec3(-1, 0, 0);
    }
    if (facing == PropIndices::FACING_WEST) {
        return position + glm::ivec3(1, 0, 0);
    }
    throw std::runtime_error("Redstone torch state contains an unknown facing value");
}

uint8_t computedWirePowerAt(const WirePowerMap& wirePowers, const glm::ivec3& position) {
    const auto it = wirePowers.find(position);
    return it == wirePowers.end() ? 0 : it->second;
}

uint8_t sourceOutputPowerToward(const World& world,
                                const glm::ivec3& sourcePosition,
                                const glm::ivec3& targetPosition) {
    const StateID sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
    if (isRepeaterState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + repeaterOutputDirection(sourceState)
            ? kMaxRedstonePower
            : 0;
    }
    if (isObserverState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + observerOutputDirection(sourceState)
            ? kMaxRedstonePower
            : 0;
    }
    if (isComparatorState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + comparatorOutputDirection(sourceState)
            ? kMaxRedstonePower
            : 0;
    }
    if (isTorchState(sourceState) && targetPosition == attachedBlockForTorch(sourceState, sourcePosition)) {
        return 0;
    }
    return sourceOutputPower(sourceState);
}

uint8_t directSignalPowerToward(const World& world,
                                const WirePowerMap& wirePowers,
                                const glm::ivec3& signalPosition,
                                const glm::ivec3& targetPosition) {
    const StateID signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState)) {
        const auto computedIt = wirePowers.find(signalPosition);
        return computedIt != wirePowers.end()
            ? computedIt->second
            : redstonePowerFromState(signalState);
    }
    return sourceOutputPowerToward(world, signalPosition, targetPosition);
}

uint8_t sourceStrongPowerTowardConductor(const World& world,
                                         const glm::ivec3& sourcePosition,
                                         const glm::ivec3& conductorPosition) {
    const StateID sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
    if (isWireState(sourceState) || isConductiveState(sourceState)) {
        return 0;
    }
    if (isTorchState(sourceState)) {
        if (conductorPosition != sourcePosition + glm::ivec3(0, 1, 0)) {
            return 0;
        }
        return sourceOutputPower(sourceState);
    }
    if (isRepeaterState(sourceState) ||
        isObserverState(sourceState) ||
        isComparatorState(sourceState)) {
        return sourceOutputPowerToward(world, sourcePosition, conductorPosition);
    }
    if (!sourceCanPowerConductiveBlocks(sourceState)) {
        return 0;
    }
    return sourceOutputPowerToward(world, sourcePosition, conductorPosition);
}

uint8_t wirePowerTowardConductor(const World& world,
                                 const WirePowerMap& wirePowers,
                                 const glm::ivec3& wirePosition,
                                 const glm::ivec3& conductorPosition) {
    if (!wirePowersConductorToward(world, wirePosition, conductorPosition)) {
        return 0;
    }
    const auto computedIt = wirePowers.find(wirePosition);
    return computedIt != wirePowers.end()
        ? computedIt->second
        : redstonePowerFromState(world.getBlockState(wirePosition.x, wirePosition.y, wirePosition.z));
}

uint8_t conductiveBlockInputPowerToward(const World& world,
                                        const WirePowerMap& wirePowers,
                                        const glm::ivec3& signalPosition,
                                        const glm::ivec3& conductorPosition) {
    const StateID signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState)) {
        return wirePowerTowardConductor(world, wirePowers, signalPosition, conductorPosition);
    }

    return sourceStrongPowerTowardConductor(world, signalPosition, conductorPosition);
}

uint8_t conductedSignalPowerToward(const World& world,
                                   const WirePowerMap& wirePowers,
                                   const glm::ivec3& conductor,
                                   const glm::ivec3& targetPosition) {
    if (!isConductiveBlockAt(world, conductor)) {
        return 0;
    }

    uint8_t conductedPower = 0;
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 signalPosition = conductor + direction;
        if (signalPosition == targetPosition) {
            continue;
        }
        conductedPower = std::max(
            conductedPower,
            conductiveBlockInputPowerToward(world, wirePowers, signalPosition, conductor));
    }
    return conductedPower;
}

uint8_t conductiveBlockPowerAt(const World& world,
                               const WirePowerMap& wirePowers,
                               const glm::ivec3& conductor,
                               const glm::ivec3* excludedPosition = nullptr) {
    if (!isConductiveBlockAt(world, conductor)) {
        return 0;
    }

    uint8_t receivedPower = 0;
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = conductor + direction;
        if (excludedPosition != nullptr && neighbor == *excludedPosition) {
            continue;
        }

        const uint8_t neighborPower = conductiveBlockInputPowerToward(world, wirePowers, neighbor, conductor);
        receivedPower = std::max(receivedPower, neighborPower);
    }
    return receivedPower;
}

uint8_t receivedPowerAt(const World& world,
                        const WirePowerMap& wirePowers,
                        const glm::ivec3& position,
                        const glm::ivec3* excludedPosition = nullptr) {
    uint8_t receivedPower = 0;
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = position + direction;
        if (excludedPosition != nullptr && neighbor == *excludedPosition) {
            continue;
        }

        const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        uint8_t neighborPower = directSignalPowerToward(world, wirePowers, neighbor, position);
        if (isConductiveState(neighborState)) {
            neighborPower = std::max(
                neighborPower,
                conductedSignalPowerToward(world, wirePowers, neighbor, position));
        }
        receivedPower = std::max(receivedPower, neighborPower);
    }
    return receivedPower;
}

bool shouldTorchBeLit(const World& world,
                      const WirePowerMap& inputWirePowers,
                      const glm::ivec3& torchPosition,
                      const StateID torchState) {
    const glm::ivec3 attachedBlock = attachedBlockForTorch(torchState, torchPosition);
    if (!isConductiveBlockAt(world, attachedBlock)) {
        return true;
    }
    return conductiveBlockPowerAt(world, inputWirePowers, attachedBlock, &torchPosition) == 0;
}

size_t applyTorchStates(World& world,
                        const std::vector<glm::ivec3>& torches,
                        const PositionSet& wires,
                        const std::vector<glm::ivec3>& sourcePositions,
                        const std::vector<glm::ivec3>& repeaterPositions,
                        const std::vector<glm::ivec3>& observerPositions,
                        const std::vector<glm::ivec3>& comparatorPositions) {
    std::vector<glm::ivec3> orderedTorches = torches;
    std::sort(orderedTorches.begin(), orderedTorches.end(), [](const glm::ivec3& lhs, const glm::ivec3& rhs) {
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        if (lhs.z != rhs.z) {
            return lhs.z < rhs.z;
        }
        return lhs.x < rhs.x;
    });

    size_t changed = 0;
    for (const glm::ivec3& position : orderedTorches) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isTorchState(currentState)) {
            continue;
        }

        PositionSet excludedSources;
        excludedSources.insert(position);
        std::vector<RedstoneSource> inputSources =
            collectActiveSources(world, sourcePositions, &excludedSources);
        appendSources(inputSources, collectPoweredRepeaterSources(world, repeaterPositions));
        appendSources(inputSources, collectPoweredObserverSources(world, observerPositions));
        appendSources(inputSources, collectPoweredComparatorStateSources(world, comparatorPositions));
        const WirePowerMap inputWirePowers = propagateWirePower(world, wires, inputSources);

        const StateID updatedState = withLit(
            currentState,
            shouldTorchBeLit(world, inputWirePowers, position, currentState));
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

uint8_t repeaterInputPower(const World& world,
                           const WirePowerMap& wirePowers,
                           const glm::ivec3& repeaterPosition,
                           const StateID repeaterState) {
    const glm::ivec3 inputPosition = repeaterPosition - repeaterOutputDirection(repeaterState);
    uint8_t inputPower = directSignalPowerToward(world, wirePowers, inputPosition, repeaterPosition);
    if (isConductiveBlockAt(world, inputPosition)) {
        inputPower = std::max(
            inputPower,
            conductedSignalPowerToward(world, wirePowers, inputPosition, repeaterPosition));
    }
    return inputPower;
}

uint8_t signalPowerFromNeighbor(const World& world,
                                const WirePowerMap& wirePowers,
                                const glm::ivec3& signalPosition,
                                const glm::ivec3& targetPosition) {
    uint8_t signalPower = directSignalPowerToward(world, wirePowers, signalPosition, targetPosition);
    if (isConductiveBlockAt(world, signalPosition)) {
        signalPower = std::max(
            signalPower,
            conductedSignalPowerToward(world, wirePowers, signalPosition, targetPosition));
    }
    return signalPower;
}

uint8_t comparatorOutputPower(const World& world,
                              const GameplayRegistry* registry,
                              const WirePowerMap& inputWirePowers,
                              const glm::ivec3& comparatorPosition,
                              const StateID comparatorState) {
    const glm::ivec3 outputDirection = comparatorOutputDirection(comparatorState);
    const glm::ivec3 rearInputPosition = comparatorPosition - outputDirection;
    const uint8_t containerPower = containerSignalPowerAt(world, registry, rearInputPosition);
    const uint8_t rearPower = containerPower > 0
        ? containerPower
        : signalPowerFromNeighbor(
            world,
            inputWirePowers,
            rearInputPosition,
            comparatorPosition);

    uint8_t sidePower = 0;
    for (const glm::ivec3& sideDirection : comparatorSideDirections(comparatorState)) {
        sidePower = std::max(
            sidePower,
            signalPowerFromNeighbor(
                world,
                inputWirePowers,
                comparatorPosition + sideDirection,
                comparatorPosition));
    }

    if (comparatorUsesSubtractMode(comparatorState)) {
        return rearPower > sidePower
            ? static_cast<uint8_t>(rearPower - sidePower)
            : 0;
    }
    return rearPower >= sidePower ? rearPower : 0;
}

std::vector<ComparatorEvaluation> evaluateComparators(const World& world,
                                                      const GameplayRegistry* registry,
                                                      const std::vector<glm::ivec3>& comparatorPositions,
                                                      const WirePowerMap& inputWirePowers) {
    std::vector<ComparatorEvaluation> evaluations;
    evaluations.reserve(comparatorPositions.size());
    for (const glm::ivec3& position : comparatorPositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isComparatorState(stateId)) {
            continue;
        }
        evaluations.push_back({position, comparatorOutputPower(world, registry, inputWirePowers, position, stateId)});
    }
    return evaluations;
}

size_t applyComparatorStates(World& world, const std::vector<ComparatorEvaluation>& evaluations) {
    size_t changed = 0;
    for (const ComparatorEvaluation& evaluation : evaluations) {
        const StateID currentState = world.getBlockState(
            evaluation.position.x,
            evaluation.position.y,
            evaluation.position.z);
        if (!isComparatorState(currentState)) {
            continue;
        }

        const StateID updatedState = withPowered(currentState, evaluation.outputPower > 0);
        if (updatedState == currentState) {
            continue;
        }

        world.setBlockState(evaluation.position.x, evaluation.position.y, evaluation.position.z, updatedState);
        ++changed;
    }
    return changed;
}

void scheduleRepeaterEvaluationUpdates(World& world,
                                       const uint64_t redstoneTick,
                                       const std::vector<glm::ivec3>& repeaterPositions,
                                       const WirePowerMap& wirePowers) {
    for (const glm::ivec3& position : repeaterPositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isRepeaterState(stateId)) {
            continue;
        }

        const bool currentPowered = isPoweredPropertyTrue(stateId);
        const bool shouldBePowered = repeaterInputPower(world, wirePowers, position, stateId) > 0;
        if (currentPowered == shouldBePowered) {
            continue;
        }

        world.redstoneScheduledUpdateQueue().schedule(
            redstoneTick + repeaterDelayTicks(stateId),
            position,
            RedstoneScheduledAction::EvaluateRepeater);
    }
}

void scheduleObserverPulseUpdates(World& world,
                                  const uint64_t redstoneTick,
                                  const std::vector<glm::ivec3>& changedPositions) {
    for (const glm::ivec3& changedPosition : changedPositions) {
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 observerPosition = changedPosition - direction;
            const StateID observerState = world.getBlockState(
                observerPosition.x,
                observerPosition.y,
                observerPosition.z);
            if (!isObserverState(observerState)) {
                continue;
            }
            if (observerPosition + observerFacingDirection(observerState) != changedPosition) {
                continue;
            }

            world.redstoneScheduledUpdateQueue().schedule(
                redstoneTick + kObserverPulseDelayTicks,
                observerPosition,
                RedstoneScheduledAction::StartObserverPulse);
        }
    }
}

void scheduleButtonReleaseUpdates(World& world,
                                  const uint64_t redstoneTick,
                                  const std::vector<glm::ivec3>& sourcePositions) {
    for (const glm::ivec3& position : sourcePositions) {
        const StateID stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isButtonState(stateId)) {
            continue;
        }
        if (!hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered")) {
            continue;
        }

        world.redstoneScheduledUpdateQueue().schedule(
            redstoneTick + buttonPulseTicks(stateId),
            position,
            RedstoneScheduledAction::ReleaseButton);
    }
}

bool applyButtonRelease(World& world, const glm::ivec3& position) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isButtonState(currentState)) {
        return false;
    }
    if (!hasBooleanPropertyValue(currentState, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered")) {
        return false;
    }

    const StateID updatedState = withPowered(currentState, false);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyObserverPulseStart(World& world, const glm::ivec3& position, const uint64_t redstoneTick) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(currentState)) {
        return false;
    }
    if (isPoweredPropertyTrue(currentState)) {
        return false;
    }

    const StateID updatedState = withPowered(currentState, true);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    world.redstoneScheduledUpdateQueue().schedule(
        redstoneTick + kObserverPulseDurationTicks,
        position,
        RedstoneScheduledAction::ReleaseObserverPulse);
    return true;
}

bool applyObserverPulseRelease(World& world, const glm::ivec3& position) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(currentState)) {
        return false;
    }
    if (!isPoweredPropertyTrue(currentState)) {
        return false;
    }

    const StateID updatedState = withPowered(currentState, false);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyTargetPulseRelease(World& world, const glm::ivec3& position) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isTargetState(currentState)) {
        return false;
    }
    if (redstonePowerFromState(currentState) == 0) {
        return false;
    }

    const StateID updatedState = withRedstonePower(currentState, 0);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyRepeaterEvaluation(World& world, const glm::ivec3& position) {
    const StateID currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isRepeaterState(currentState)) {
        return false;
    }

    const WirePowerMap wirePowers;
    const bool shouldBePowered = repeaterInputPower(world, wirePowers, position, currentState) > 0;
    const StateID updatedState = withPowered(currentState, shouldBePowered);
    if (updatedState == currentState) {
        return false;
    }

    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

size_t applyScheduledUpdates(World& world, const std::vector<RedstoneScheduledUpdate>& updates) {
    size_t changed = 0;
    for (const RedstoneScheduledUpdate& update : updates) {
        switch (update.action) {
        case RedstoneScheduledAction::ReleaseButton:
            if (applyButtonRelease(world, update.position)) {
                ++changed;
            }
            break;
        case RedstoneScheduledAction::EvaluateRepeater:
            if (applyRepeaterEvaluation(world, update.position)) {
                ++changed;
            }
            break;
        case RedstoneScheduledAction::StartObserverPulse:
            if (applyObserverPulseStart(world, update.position, update.executionTick)) {
                ++changed;
            }
            break;
        case RedstoneScheduledAction::ReleaseObserverPulse:
            if (applyObserverPulseRelease(world, update.position)) {
                ++changed;
            }
            break;
        case RedstoneScheduledAction::ReleaseTargetPulse:
            if (applyTargetPulseRelease(world, update.position)) {
                ++changed;
            }
            break;
        }
    }
    return changed;
}

size_t applyWirePowers(World& world, const std::vector<glm::ivec3>& wires, const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : wires) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isWireState(currentState)) {
            continue;
        }

        const StateID updatedState = withRedstonePower(currentState, computedWirePowerAt(wirePowers, position));
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

size_t applyPistonStates(World& world, const std::vector<glm::ivec3>& pistons, const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : pistons) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isPistonState(currentState)) {
            continue;
        }

        const bool currentlyExtended = isExtendedPropertyTrue(currentState);
        const bool shouldExtend = receivedPowerAt(world, wirePowers, position) > 0;
        if (currentlyExtended == shouldExtend) {
            continue;
        }

        const glm::ivec3 facingDirection = pistonFacingDirection(currentState);
        if (shouldExtend) {
            PistonPushPlan pushPlan;
            if (!buildPistonPushPlan(world, position, facingDirection, pushPlan)) {
                continue;
            }

            changed += applyPistonPush(world, position, currentState, facingDirection, pushPlan);
            continue;
        }

        changed += applyPistonRetraction(world, position, currentState, facingDirection);
    }
    return changed;
}

size_t applyRedstoneControlledStates(World& world,
                                     const std::vector<glm::ivec3>& positions,
                                     const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : positions) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isRedstoneControlledState(currentState)) {
            continue;
        }

        const bool shouldBePowered = receivedPowerAt(world, wirePowers, position) > 0;
        const StateID updatedState = withRedstoneControlledPower(currentState, shouldBePowered);
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

void emitEdgeTriggeredDeviceEvent(GameplayRegistry& registry,
                                  const StateID stateId,
                                  const glm::ivec3& position) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior == "note_block") {
        ensureAudioEventBus(registry).push({
            "block.note_block.harp",
            glm::vec3(position) + glm::vec3(0.5f),
            true,
            1.0f
        });
        return;
    }
    throw std::runtime_error("Unsupported redstone edge-triggered behavior: " + def.redstoneBehavior);
}

size_t applyEdgeTriggeredDeviceStates(World& world,
                                      GameplayRegistry* registry,
                                      const std::vector<glm::ivec3>& positions,
                                      const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : positions) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isEdgeTriggeredDeviceState(currentState)) {
            continue;
        }

        const bool currentlyPowered = isPoweredPropertyTrue(currentState);
        const bool shouldBePowered = receivedPowerAt(world, wirePowers, position) > 0;
        if (!currentlyPowered && shouldBePowered && registry != nullptr) {
            emitEdgeTriggeredDeviceEvent(*registry, currentState, position);
        }

        const StateID updatedState = withPowered(currentState, shouldBePowered);
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

size_t processWorldWithContext(World& world,
                               const GameplayRegistry* registry,
                               GameplayRegistry* eventRegistry,
                               const uint64_t redstoneTick,
                               const size_t budget) {
    if (budget == 0) {
        return 0;
    }

    world.setLastProcessedRedstoneTick(redstoneTick);

    size_t changed = 0;
    std::vector<RedstoneScheduledUpdate> scheduledUpdates;
    scheduledUpdates.reserve(std::min(budget, world.redstoneScheduledUpdateQueue().size()));
    world.redstoneScheduledUpdateQueue().drainDue(redstoneTick, scheduledUpdates, budget);
    changed += applyScheduledUpdates(world, scheduledUpdates);

    std::vector<glm::ivec3> changedPositions;
    changedPositions.reserve(budget);
    world.redstoneChangedBlockQueue().drain(changedPositions, budget);
    scheduleObserverPulseUpdates(world, redstoneTick, changedPositions);

    if (world.redstoneUpdateQueue().size() == 0) {
        return changed;
    }

    std::vector<glm::ivec3> dirtyPositions;
    dirtyPositions.reserve(budget);
    world.redstoneUpdateQueue().drain(dirtyPositions, budget);

    const RedstoneWorkSet workSet = collectRedstoneWorkSet(world, dirtyPositions);

    changed += applyTorchStates(
        world,
        workSet.torches,
        workSet.wireSet,
        workSet.sourcePositions,
        workSet.repeaters,
        workSet.observers,
        workSet.comparators);
    scheduleButtonReleaseUpdates(world, redstoneTick, workSet.sourcePositions);

    std::vector<RedstoneSource> outputSources =
        collectActiveSources(world, workSet.sourcePositions, nullptr);
    appendSources(outputSources, collectPoweredRepeaterSources(world, workSet.repeaters));
    appendSources(outputSources, collectPoweredObserverSources(world, workSet.observers));
    const WirePowerMap inputWirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
    const std::vector<ComparatorEvaluation> comparatorEvaluations =
        evaluateComparators(world, registry, workSet.comparators, inputWirePowers);
    appendSources(outputSources, collectComparatorSources(world, comparatorEvaluations));
    const WirePowerMap wirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
    changed += applyWirePowers(world, workSet.wires, wirePowers);
    changed += applyComparatorStates(world, comparatorEvaluations);
    changed += applyPistonStates(world, workSet.pistons, wirePowers);
    scheduleRepeaterEvaluationUpdates(world, redstoneTick, workSet.repeaters, wirePowers);
    changed += applyRedstoneControlledStates(world, workSet.redstoneControlledBlocks, wirePowers);
    changed += applyEdgeTriggeredDeviceStates(world, eventRegistry, workSet.edgeTriggeredDevices, wirePowers);
    return changed;
}

} // namespace

void RedstoneSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }
    if ((ctx.tickIndex % 2u) != 0u) {
        return;
    }

    processWorldWithContext(*ctx.services.world, &ctx.registry, &ctx.registry, ctx.tickIndex / 2u, 4096);
}

size_t RedstoneSystem::processWorld(World& world, const uint64_t redstoneTick, const size_t budget) {
    return processWorldWithContext(world, nullptr, nullptr, redstoneTick, budget);
}

size_t RedstoneSystem::processWorld(World& world,
                                    const uint64_t redstoneTick,
                                    const GameplayRegistry& registry,
                                    const size_t budget) {
    return processWorldWithContext(world, &registry, nullptr, redstoneTick, budget);
}

} // namespace ecs
