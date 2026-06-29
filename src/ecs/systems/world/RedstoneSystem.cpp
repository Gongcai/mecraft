#include "RedstoneSystem.h"

#include "../../GameplayRegistry.h"
#include "../../components/Components.h"
#include "../../entity/EntityFactory.h"
#include "../../util/RedstoneEventBuffer.h"
#include "../../../game/inventory/BlockEntityInventoryStore.h"
#include "../../../game/inventory/ContainerBehaviorRegistry.h"
#include "../../../game/inventory/MachineInventoryStore.h"
#include "../../../item/Item.h"
#include "../../../ui/inventory/ContainerUiRegistry.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockCollision.h"
#include "../../../world/block/DoorBlock.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/RedstoneUpdateQueue.h"
#include "../../../world/redstone/WireFaceGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

namespace {

constexpr uint8_t kMaxRedstonePower = 15;
constexpr uint64_t kObserverPulseDelayTicks = 1;
constexpr uint64_t kObserverPulseDurationTicks = 1;
constexpr uint64_t kTorchBurnoutWindowTicks = 30;
constexpr uint64_t kTorchBurnoutCooldownTicks = 80;
constexpr size_t kTorchBurnoutTurnOffLimit = 8;
constexpr size_t kMaxPistonPushBlocks = 12;
constexpr float kPistonMovementDurationSeconds = 0.1f;
constexpr float kPistonEntityPushEpsilon = 0.001f;
constexpr float kPistonEntitySupportContactTolerance = 0.02f;
constexpr int kMaxPistonEntityDepenetrationIterations = 12;
constexpr glm::ivec3 kDirections[6] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  1,  0},
    { 0, -1,  0},
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

using PositionSet = std::unordered_set<glm::ivec3, IVec3Hash>;

struct WirePowerMap {
    using Map = std::unordered_map<glm::ivec3, uint8_t, IVec3Hash>;

    Map powers;
    const PositionSet* evaluatedWires = nullptr;

    void reserve(const size_t count) {
        powers.reserve(count);
    }

    auto emplace(const glm::ivec3& position, const uint8_t power) {
        return powers.emplace(position, power);
    }

    [[nodiscard]] auto find(const glm::ivec3& position) {
        return powers.find(position);
    }

    [[nodiscard]] auto find(const glm::ivec3& position) const {
        return powers.find(position);
    }

    [[nodiscard]] auto end() {
        return powers.end();
    }

    [[nodiscard]] auto end() const {
        return powers.end();
    }

    [[nodiscard]] bool wasEvaluated(const glm::ivec3& position) const {
        return evaluatedWires != nullptr && evaluatedWires->find(position) != evaluatedWires->end();
    }
};

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

struct WireSearchNode {
    glm::ivec3 position;
    uint8_t distance = 0;
};

struct ComparatorEvaluation {
    glm::ivec3 position;
    uint8_t outputPower = 0;
};

struct PistonMovedBlock {
    glm::ivec3 position;
    BlockStateId state = NULL_BLOCK_STATE;
};

struct PistonPushPlan {
    std::vector<PistonMovedBlock> movedBlocks;
};

struct CollisionAabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct PistonMovementCollision {
    CollisionAabb finalBox;
    CollisionAabb sourceBox;
    CollisionAabb sweptBox;
};

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

bool isWireState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "wire";
}

uint16_t redstoneWireChannelIdForState(const BlockStateId stateId) {
    if (!isWireState(stateId)) {
        throw std::runtime_error("Redstone wire channel requires a wire state");
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneWireChannelId == 0) {
        throw std::runtime_error("Redstone wire state is missing redstoneWireChannelId");
    }
    return def.redstoneWireChannelId;
}

bool isMatchingWireState(const BlockStateId stateId, const uint16_t wireChannelId) {
    if (!isWireState(stateId)) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneWireChannelId == wireChannelId;
}

uint16_t getRequiredProperty(const BlockStateId stateId, const uint16_t property, const char* propertyName) {
    if (property == PropIndices::INVALID) {
        throw std::runtime_error(std::string("Redstone requires registered property: ") + propertyName);
    }
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string("Redstone state is missing property: ") + propertyName);
    }
    return value;
}

uint16_t wireFacingForState(const BlockStateId stateId) {
    const uint16_t facing = getRequiredProperty(stateId, PropIndices::FACING, "facing");
    if (!WireFaceGeometry::isWireFacing(facing)) {
        throw std::runtime_error("Redstone wire state contains an unsupported facing value");
    }
    return facing;
}

bool isMatchingWireStateWithFacing(const BlockStateId stateId,
                                   const uint16_t wireChannelId,
                                   const uint16_t wireFacing) {
    if (!isMatchingWireState(stateId, wireChannelId)) {
        return false;
    }
    return wireFacingForState(stateId) == wireFacing;
}

template <typename Fn>
void forEachCornerWireNeighbor(const World& world,
                               const glm::ivec3& pos,
                               const uint16_t wireChannelId,
                               const uint16_t wireFacing,
                               Fn&& fn) {
    const glm::ivec3 support = WireFaceGeometry::supportPosition(pos, wireFacing);
    for (const uint16_t neighborFacing : WireFaceGeometry::wireFacings()) {
        if (!WireFaceGeometry::arePerpendicularFacings(wireFacing, neighborFacing)) {
            continue;
        }

        const glm::ivec3 neighbor = WireFaceGeometry::wirePositionOnSupportFace(support, neighborFacing);
        if (isMatchingWireStateWithFacing(
                world.getBlockState(neighbor.x, neighbor.y, neighbor.z),
                wireChannelId,
                neighborFacing)) {
            fn(neighbor);
        }
    }
}

BlockStateId withRequiredProperty(const BlockStateId stateId,
                             const uint16_t property,
                             const uint16_t value,
                             const char* propertyName) {
    const uint16_t current = getRequiredProperty(stateId, property, propertyName);
    if (current == value) {
        return stateId;
    }

    const BlockStateId updated = BlockStateRegistry::withProperty(stateId, property, value);
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

uint8_t redstonePowerFromState(const BlockStateId stateId) {
    const uint16_t value = getRequiredProperty(stateId, PropIndices::POWER, "power");
    const auto values = powerPropertyValues();
    for (uint8_t power = 0; power < values.size(); ++power) {
        if (value == values[power]) {
            return power;
        }
    }
    throw std::runtime_error("Redstone state contains an unknown power value");
}

BlockStateId withRedstonePower(const BlockStateId stateId, const uint8_t power) {
    return withRequiredProperty(stateId, PropIndices::POWER, powerToPropertyValue(power), "power");
}

BlockStateId withLit(const BlockStateId stateId, const bool lit) {
    const uint16_t value = lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered lit boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::LIT, value, "lit");
}

BlockStateId withPowered(const BlockStateId stateId, const bool powered) {
    const uint16_t value = powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered powered boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::POWERED, value, "powered");
}

BlockStateId withLocked(const BlockStateId stateId, const bool locked) {
    const uint16_t value = locked ? PropIndices::LOCKED_TRUE : PropIndices::LOCKED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered locked boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::LOCKED, value, "locked");
}

bool hasBooleanPropertyValue(const BlockStateId stateId,
                             const uint16_t property,
                             const uint16_t expectedValue,
                             const char* propertyName) {
    return getRequiredProperty(stateId, property, propertyName) == expectedValue;
}

uint8_t sourceOutputPower(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
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

bool sourceCanPowerConductiveBlocks(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
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
    const BlockStateId wireState = world.getBlockState(wirePosition.x, wirePosition.y, wirePosition.z);
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

uint8_t containerSignalPowerAt(const World& world,
                               const GameplayRegistry* registry,
                               const glm::ivec3& position) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (stateId == NULL_BLOCK_STATE) {
        return 0;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return 0;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (!behavior.comparatorSignal) {
        return 0;
    }

    if (behavior.handler == "storage") {
        if (registry == nullptr || !registry->ctxHas<BlockEntityInventoryStore>()) {
            return 0;
        }
        const BlockEntityInventoryStore& store = registry->ctxGet<BlockEntityInventoryStore>();
        const BlockEntityInventory* inventory = store.find(position);
        if (inventory == nullptr) {
            return 0;
        }
        return inventorySignalPower(behavior.storage.slots, [inventory](const int slot) {
            return inventory->getSlotStack(slot);
        });
    }

    if (behavior.handler == "smelting") {
        if (registry == nullptr || !registry->ctxHas<MachineInventoryStore>()) {
            return 0;
        }
        const MachineInventoryStore& store = registry->ctxGet<MachineInventoryStore>();
        const MachineInventory* machine = store.find(position);
        if (machine == nullptr) {
            return 0;
        }
        return inventorySignalPower(behavior.storage.slots, [machine](const int slot) {
            return machine->getSlotStack(slot);
        });
    }

    throw std::runtime_error("Comparator signal is not implemented for container behavior handler: " +
                             behavior.handler);
}

bool isPotentialSourceState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstonePowerSource;
}

bool isConductiveState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstoneConductor;
}

bool isConductiveBlockAt(const World& world, const glm::ivec3& position) {
    return isConductiveState(world.getBlockState(position.x, position.y, position.z));
}

// Calls fn for every wire position connected to the wire at pos. Wires connect
// within their own face plane and can meet matching-color wires around a shared
// support-block edge.
template <typename Fn>
void forEachWireNeighbor(const World& world, const glm::ivec3& pos, Fn&& fn) {
    const BlockStateId selfState = world.getBlockState(pos.x, pos.y, pos.z);
    if (!isWireState(selfState)) {
        return;
    }
    const uint16_t wireChannelId = redstoneWireChannelIdForState(selfState);
    const uint16_t wireFacing = wireFacingForState(selfState);

    if (wireFacing == PropIndices::FACING_FLOOR) {
        for (const WireFaceGeometry::ConnectionDirection& connection :
             WireFaceGeometry::connectionDirections(wireFacing)) {
            const glm::ivec3 neighbor = pos + connection.offset;
            if (isMatchingWireStateWithFacing(
                    world.getBlockState(neighbor.x, neighbor.y, neighbor.z),
                    wireChannelId,
                    PropIndices::FACING_FLOOR)) {
                fn(neighbor);
            }
        }
        forEachCornerWireNeighbor(world, pos, wireChannelId, wireFacing, fn);
        return;
    }

    for (const WireFaceGeometry::ConnectionDirection& connection :
         WireFaceGeometry::connectionDirections(wireFacing)) {
        const glm::ivec3 neighbor = pos + connection.offset;
        if (isMatchingWireStateWithFacing(
                world.getBlockState(neighbor.x, neighbor.y, neighbor.z),
                wireChannelId,
                wireFacing)) {
            fn(neighbor);
        }
    }
    forEachCornerWireNeighbor(world, pos, wireChannelId, wireFacing, fn);
}

bool isRedstoneControlledState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.respondsToRedstone && !def.redstoneControlledProperty.empty();
}

bool isTorchState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "torch";
}

bool isButtonState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "button";
}

bool isRepeaterState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "repeater";
}

bool isObserverState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "observer";
}

bool isComparatorState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "comparator";
}

bool isPistonState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "piston";
}

bool isEdgeTriggeredDeviceState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.respondsToRedstone &&
           (def.redstoneBehavior == "note_block" ||
            def.redstoneBehavior == "dispenser" ||
            def.redstoneBehavior == "dropper");
}

bool isTargetState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.redstoneBehavior == "target";
}

bool isPistonHeadState(const BlockStateId stateId) {
    return stateId != NULL_BLOCK_STATE &&
           BlockStateRegistry::getBlockId(stateId) == pistonHeadBlockId();
}

uint64_t buttonPulseTicks(const BlockStateId stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior != "button") {
        throw std::runtime_error("Redstone pulse duration requires a button block");
    }
    if (def.redstonePulseTicks == 0) {
        throw std::runtime_error("Redstone button has no configured pulse duration: " +
                                 def.namespacedId.full());
    }
    return def.redstonePulseTicks;
}

uint64_t repeaterDelayTicks(const BlockStateId stateId) {
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

glm::ivec3 repeaterOutputDirection(const BlockStateId stateId) {
    return horizontalDirectionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

std::array<glm::ivec3, 2> repeaterSideDirections(const BlockStateId stateId) {
    const glm::ivec3 outputDirection = repeaterOutputDirection(stateId);
    if (outputDirection.x != 0) {
        return {glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)};
    }
    return {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0)};
}

glm::ivec3 comparatorOutputDirection(const BlockStateId stateId) {
    return horizontalDirectionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

std::array<glm::ivec3, 2> comparatorSideDirections(const BlockStateId stateId) {
    const glm::ivec3 outputDirection = comparatorOutputDirection(stateId);
    if (outputDirection.x != 0) {
        return {glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)};
    }
    return {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0)};
}

bool comparatorUsesSubtractMode(const BlockStateId stateId) {
    const uint16_t mode = getRequiredProperty(stateId, PropIndices::MODE, "mode");
    if (mode == PropIndices::MODE_COMPARE) {
        return false;
    }
    if (mode == PropIndices::MODE_SUBTRACT) {
        return true;
    }
    throw std::runtime_error("Comparator state contains an unknown mode value");
}

glm::ivec3 observerFacingDirection(const BlockStateId stateId) {
    return directionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

glm::ivec3 observerOutputDirection(const BlockStateId stateId) {
    return -observerFacingDirection(stateId);
}

bool isPoweredPropertyTrue(const BlockStateId stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered");
}

bool isLitPropertyTrue(const BlockStateId stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::LIT, PropIndices::LIT_TRUE, "lit");
}

bool isLockedPropertyTrue(const BlockStateId stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::LOCKED, PropIndices::LOCKED_TRUE, "locked");
}

uint16_t redstoneControlledPropertyIndex(const BlockStateId stateId) {
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

uint16_t requireBooleanStateProperty(const BlockStateId stateId,
                                     const std::string& propertyName,
                                     const char* context) {
    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(propertyName);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string(context) + " property is not registered: " + propertyName);
    }

    const uint16_t currentValue = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (currentValue == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string(context) + " state is missing property: " + propertyName);
    }

    const uint16_t falseValue = BlockStateRegistry::getPropertyValueIndex(property, "false");
    const uint16_t trueValue = BlockStateRegistry::getPropertyValueIndex(property, "true");
    if (falseValue == BlockStateRegistry::INVALID_INDEX || trueValue == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error(std::string(context) + " property must define false and true values: " + propertyName);
    }
    if (currentValue != falseValue && currentValue != trueValue) {
        throw std::runtime_error(std::string(context) + " property state must be false or true: " + propertyName);
    }
    return property;
}

BlockStateId withRedstoneControlledPower(const BlockStateId stateId, const bool powered) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    const uint16_t property = redstoneControlledPropertyIndex(stateId);
    const bool propertyValue = powered != def.redstoneControlledPowerInverted;
    const uint16_t value = BlockStateRegistry::getPropertyValueIndex(property, propertyValue ? "true" : "false");
    if (BlockStateRegistry::getPropertyIndex(stateId, property) == value) {
        return stateId;
    }

    const BlockStateId updatedState = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updatedState, property) != value) {
        throw std::runtime_error("Redstone controlled state transition failed");
    }

    BlockStateId mirroredState = updatedState;
    for (const std::string& mirrorPropertyName : def.redstoneControlledMirrorProperties) {
        const uint16_t mirrorProperty = requireBooleanStateProperty(
            mirroredState,
            mirrorPropertyName,
            "Redstone controlled mirror");
        const uint16_t mirrorValue =
            BlockStateRegistry::getPropertyValueIndex(mirrorProperty, propertyValue ? "true" : "false");
        mirroredState = BlockStateRegistry::withProperty(mirroredState, mirrorProperty, mirrorValue);
        if (BlockStateRegistry::getPropertyIndex(mirroredState, mirrorProperty) != mirrorValue) {
            throw std::runtime_error("Redstone controlled mirror state transition failed");
        }
    }
    return mirroredState;
}

bool isExtendedPropertyTrue(const BlockStateId stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::EXTENDED, PropIndices::EXTENDED_TRUE, "extended");
}

BlockStateId withExtended(const BlockStateId stateId, const bool extended) {
    const uint16_t value = extended ? PropIndices::EXTENDED_TRUE : PropIndices::EXTENDED_FALSE;
    if (value == PropIndices::INVALID) {
        throw std::runtime_error("Redstone requires registered extended boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::EXTENDED, value, "extended");
}

glm::ivec3 pistonFacingDirection(const BlockStateId stateId) {
    return directionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

uint16_t pistonHeadType(const BlockStateId pistonState) {
    const BlockID blockId = BlockStateRegistry::getBlockId(pistonState);
    if (blockId == pistonBlockId()) {
        return PropIndices::TYPE_NORMAL;
    }
    if (blockId == stickyPistonBlockId()) {
        return PropIndices::TYPE_STICKY;
    }
    throw std::runtime_error("Piston head type requested for a non-piston block");
}

BlockStateId pistonHeadState(const BlockStateId pistonState) {
    return BlockStateRegistry::getState(
        pistonHeadBlockId(),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, getRequiredProperty(pistonState, PropIndices::FACING, "facing")},
            {PropIndices::TYPE, pistonHeadType(pistonState)}
        });
}

bool isMatchingPistonHead(const BlockStateId headState, const BlockStateId pistonState) {
    return isPistonHeadState(headState) &&
           getRequiredProperty(headState, PropIndices::FACING, "facing") ==
               getRequiredProperty(pistonState, PropIndices::FACING, "facing") &&
           getRequiredProperty(headState, PropIndices::TYPE, "type") == pistonHeadType(pistonState);
}

bool isImmovablePistonBlock(const BlockStateId stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.pistonPushReaction == "block") {
        return true;
    }
    return isPistonState(stateId) && isExtendedPropertyTrue(stateId);
}

bool isMovablePistonBlock(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    if (isImmovablePistonBlock(stateId)) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isSolid;
}

CollisionAabb physicsBodyAabb(const PhysicsBody& body) {
    const glm::vec3 center = body.position + body.colliderOffset;
    return {center - body.halfExtents, center + body.halfExtents};
}

bool aabbIntersects(const CollisionAabb& lhs, const CollisionAabb& rhs) {
    return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x &&
           lhs.min.y < rhs.max.y && lhs.max.y > rhs.min.y &&
           lhs.min.z < rhs.max.z && lhs.max.z > rhs.min.z;
}

CollisionAabb translatedAabb(const CollisionAabb& box, const glm::vec3& delta) {
    return {box.min + delta, box.max + delta};
}

glm::vec3 minVec3(const glm::vec3& lhs, const glm::vec3& rhs) {
    return {
        std::min(lhs.x, rhs.x),
        std::min(lhs.y, rhs.y),
        std::min(lhs.z, rhs.z),
    };
}

glm::vec3 maxVec3(const glm::vec3& lhs, const glm::vec3& rhs) {
    return {
        std::max(lhs.x, rhs.x),
        std::max(lhs.y, rhs.y),
        std::max(lhs.z, rhs.z),
    };
}

CollisionAabb mergedAabb(const CollisionAabb& lhs, const CollisionAabb& rhs) {
    return {minVec3(lhs.min, rhs.min), maxVec3(lhs.max, rhs.max)};
}

float axisTranslationMagnitude(const glm::vec3& delta) {
    return std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z);
}

bool rangesOverlap(const float aMin, const float aMax, const float bMin, const float bMax) {
    return aMin < bMax && aMax > bMin;
}

template <typename Fn>
void forEachWorldCollisionBox(const World& world, const glm::ivec3& blockPosition, Fn&& fn) {
    const BlockStateId stateId = world.getBlockState(blockPosition.x, blockPosition.y, blockPosition.z);
    const glm::vec3 blockOffset(blockPosition);
    for (const BlockCollisionBox& localBox : BlockCollision::getBoxes(stateId)) {
        fn(CollisionAabb{blockOffset + localBox.min, blockOffset + localBox.max});
    }
}

std::vector<PistonMovementCollision> collectPistonMovementCollisions(const World& world,
                                                                     const glm::ivec3& blockPosition,
                                                                     const glm::ivec3& movementDirection) {
    std::vector<PistonMovementCollision> collisions;
    const glm::vec3 movement(movementDirection);
    forEachWorldCollisionBox(world, blockPosition, [&](const CollisionAabb& finalBox) {
        const CollisionAabb sourceBox = translatedAabb(finalBox, -movement);
        collisions.push_back({finalBox, sourceBox, mergedAabb(finalBox, sourceBox)});
    });
    return collisions;
}

template <typename Fn>
void forEachIntersectingWorldCollisionBox(const World& world, const CollisionAabb& box, Fn&& fn) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kPistonEntityPushEpsilon));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kPistonEntityPushEpsilon));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kPistonEntityPushEpsilon));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                forEachWorldCollisionBox(world, glm::ivec3(x, y, z), [&](const CollisionAabb& obstacle) {
                    if (aabbIntersects(box, obstacle)) {
                        fn(obstacle);
                    }
                });
            }
        }
    }
}

bool worldCollisionIntersects(const World& world, const CollisionAabb& box) {
    bool intersects = false;
    forEachIntersectingWorldCollisionBox(world, box, [&](const CollisionAabb&) {
        intersects = true;
    });
    return intersects;
}

void appendSeparationCandidates(const CollisionAabb& body,
                                const CollisionAabb& obstacle,
                                std::vector<glm::vec3>& candidates) {
    candidates.push_back(glm::vec3(obstacle.max.x - body.min.x + kPistonEntityPushEpsilon, 0.0f, 0.0f));
    candidates.push_back(glm::vec3(obstacle.min.x - body.max.x - kPistonEntityPushEpsilon, 0.0f, 0.0f));
    candidates.push_back(glm::vec3(0.0f, obstacle.max.y - body.min.y + kPistonEntityPushEpsilon, 0.0f));
    candidates.push_back(glm::vec3(0.0f, obstacle.min.y - body.max.y - kPistonEntityPushEpsilon, 0.0f));
    candidates.push_back(glm::vec3(0.0f, 0.0f, obstacle.max.z - body.min.z + kPistonEntityPushEpsilon));
    candidates.push_back(glm::vec3(0.0f, 0.0f, obstacle.min.z - body.max.z - kPistonEntityPushEpsilon));
}

bool findWorldDepenetration(const World& world, const CollisionAabb& body, glm::vec3& outDelta) {
    std::vector<glm::vec3> candidates;
    forEachIntersectingWorldCollisionBox(world, body, [&](const CollisionAabb& obstacle) {
        appendSeparationCandidates(body, obstacle, candidates);
    });

    if (candidates.empty()) {
        outDelta = glm::vec3(0.0f);
        return false;
    }

    bool foundAny = false;
    glm::vec3 bestAny(0.0f);
    float bestAnyMagnitude = std::numeric_limits<float>::max();
    bool foundValid = false;
    glm::vec3 bestValid(0.0f);
    float bestValidMagnitude = std::numeric_limits<float>::max();

    for (const glm::vec3& candidate : candidates) {
        const float magnitude = axisTranslationMagnitude(candidate);
        if (magnitude <= 0.0f) {
            continue;
        }

        if (magnitude < bestAnyMagnitude) {
            foundAny = true;
            bestAny = candidate;
            bestAnyMagnitude = magnitude;
        }

        if (magnitude >= bestValidMagnitude) {
            continue;
        }

        if (!worldCollisionIntersects(world, translatedAabb(body, candidate))) {
            foundValid = true;
            bestValid = candidate;
            bestValidMagnitude = magnitude;
        }
    }

    if (foundValid) {
        outDelta = bestValid;
        return true;
    }
    if (foundAny) {
        outDelta = bestAny;
        return true;
    }

    outDelta = glm::vec3(0.0f);
    return false;
}

void applyPhysicsBodyTranslation(PhysicsBody& body, const glm::vec3& delta) {
    body.position += delta;
    if ((delta.x > 0.0f && body.velocity.x < 0.0f) || (delta.x < 0.0f && body.velocity.x > 0.0f)) {
        body.velocity.x = 0.0f;
    }
    if ((delta.y > 0.0f && body.velocity.y < 0.0f) || (delta.y < 0.0f && body.velocity.y > 0.0f)) {
        body.velocity.y = 0.0f;
    }
    if ((delta.z > 0.0f && body.velocity.z < 0.0f) || (delta.z < 0.0f && body.velocity.z > 0.0f)) {
        body.velocity.z = 0.0f;
    }
    if (delta.y > 0.0f) {
        body.isGrounded = true;
    }
}

void depenetratePhysicsBodyFromWorld(const World& world, PhysicsBody& body) {
    for (int iteration = 0; iteration < kMaxPistonEntityDepenetrationIterations; ++iteration) {
        glm::vec3 delta(0.0f);
        if (!findWorldDepenetration(world, physicsBodyAabb(body), delta)) {
            return;
        }
        applyPhysicsBodyTranslation(body, delta);
    }
}

bool pushPhysicsBodyFromMovingCollision(PhysicsBody& body,
                                        const PistonMovementCollision& collision,
                                        const glm::ivec3& movementDirection) {
    const CollisionAabb bodyBox = physicsBodyAabb(body);
    if (!aabbIntersects(bodyBox, collision.sweptBox)) {
        return false;
    }

    glm::vec3 delta(0.0f);
    if (movementDirection.x > 0) {
        delta.x = collision.finalBox.max.x - bodyBox.min.x + kPistonEntityPushEpsilon;
    } else if (movementDirection.x < 0) {
        delta.x = collision.finalBox.min.x - bodyBox.max.x - kPistonEntityPushEpsilon;
    } else if (movementDirection.y > 0) {
        delta.y = collision.finalBox.max.y - bodyBox.min.y + kPistonEntityPushEpsilon;
    } else if (movementDirection.y < 0) {
        delta.y = collision.finalBox.min.y - bodyBox.max.y - kPistonEntityPushEpsilon;
    } else if (movementDirection.z > 0) {
        delta.z = collision.finalBox.max.z - bodyBox.min.z + kPistonEntityPushEpsilon;
    } else if (movementDirection.z < 0) {
        delta.z = collision.finalBox.min.z - bodyBox.max.z - kPistonEntityPushEpsilon;
    } else {
        throw std::runtime_error("Piston entity push requires a non-zero movement direction");
    }

    applyPhysicsBodyTranslation(body, delta);
    return true;
}

bool physicsBodyRidesMovingCollision(const PhysicsBody& body,
                                     const PistonMovementCollision& collision,
                                     const glm::ivec3& movementDirection) {
    if (movementDirection.y != 0) {
        return false;
    }

    const CollisionAabb bodyBox = physicsBodyAabb(body);
    if (std::abs(bodyBox.min.y - collision.sourceBox.max.y) > kPistonEntitySupportContactTolerance) {
        return false;
    }

    return rangesOverlap(bodyBox.min.x, bodyBox.max.x, collision.sourceBox.min.x, collision.sourceBox.max.x) &&
           rangesOverlap(bodyBox.min.z, bodyBox.max.z, collision.sourceBox.min.z, collision.sourceBox.max.z);
}

bool carryPhysicsBodyOnMovingCollision(PhysicsBody& body,
                                       const PistonMovementCollision& collision,
                                       const glm::ivec3& movementDirection) {
    if (!physicsBodyRidesMovingCollision(body, collision, movementDirection)) {
        return false;
    }

    applyPhysicsBodyTranslation(body, glm::vec3(movementDirection));
    return true;
}

void pushEntitiesFromMovedBlock(World& world,
                                GameplayRegistry* registry,
                                const glm::ivec3& blockPosition,
                                const glm::ivec3& movementDirection) {
    if (registry == nullptr) {
        return;
    }

    const std::vector<PistonMovementCollision> collisions =
        collectPistonMovementCollisions(world, blockPosition, movementDirection);
    if (collisions.empty()) {
        return;
    }

    auto view = registry->registry().view<TransformComponent, PhysicsBodyComponent>();
    for (const entt::entity entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& physicsBody = view.get<PhysicsBodyComponent>(entity);

        physicsBody.body.position = transform.position;
        bool moved = false;
        for (const PistonMovementCollision& collision : collisions) {
            if (pushPhysicsBodyFromMovingCollision(physicsBody.body, collision, movementDirection)) {
                moved = true;
            }
        }
        if (!moved) {
            for (const PistonMovementCollision& collision : collisions) {
                if (carryPhysicsBodyOnMovingCollision(physicsBody.body, collision, movementDirection)) {
                    moved = true;
                    break;
                }
            }
        }

        if (!moved) {
            continue;
        }

        depenetratePhysicsBodyFromWorld(world, physicsBody.body);
        transform.position = physicsBody.body.position;
        if (auto* velocity = registry->try_get<VelocityComponent>(entity)) {
            velocity->velocity = physicsBody.body.velocity;
        }
    }
}

bool buildPistonPushPlan(const World& world,
                         const glm::ivec3& pistonPosition,
                         const glm::ivec3& pushDirection,
                         PistonPushPlan& plan) {
    plan.movedBlocks.clear();

    for (int distance = 1; distance <= static_cast<int>(kMaxPistonPushBlocks) + 1; ++distance) {
        const glm::ivec3 position = pistonPosition + pushDirection * distance;
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (stateId == NULL_BLOCK_STATE) {
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

void createMovingBlock(GameplayRegistry& registry,
                       const BlockStateId stateId,
                       const glm::ivec3& sourcePosition,
                       const glm::ivec3& targetPosition,
                       const glm::ivec3& direction,
                       const bool placeAtTarget) {
    MovingBlockSpawnParams params;
    params.stateId = stateId;
    params.sourcePosition = sourcePosition;
    params.targetPosition = targetPosition;
    params.direction = direction;
    params.durationSeconds = kPistonMovementDurationSeconds;
    params.placeAtTarget = placeAtTarget;
    EntityFactory::createMovingBlock(registry, params);
}

bool hasActivePistonMovement(const GameplayRegistry* registry,
                             const glm::ivec3& pistonPosition,
                             const glm::ivec3& facingDirection) {
    if (registry == nullptr) {
        return false;
    }

    const glm::ivec3 frontPosition = pistonPosition + facingDirection;
    const auto view = registry->registry().view<MovingBlockTag, MovingBlockComponent>();
    for (const entt::entity entity : view) {
        const auto& block = view.get<MovingBlockComponent>(entity);
        if (block.sourcePosition == pistonPosition ||
            block.targetPosition == pistonPosition ||
            block.sourcePosition == frontPosition ||
            block.targetPosition == frontPosition) {
            return true;
        }
    }
    return false;
}

size_t applyPistonPush(World& world,
                       GameplayRegistry* registry,
                       const glm::ivec3& pistonPosition,
                       const BlockStateId pistonState,
                       const glm::ivec3& pushDirection,
                       const PistonPushPlan& plan) {
    if (registry != nullptr) {
        size_t changed = 0;
        for (const PistonMovedBlock& movedBlock : plan.movedBlocks) {
            world.setBlockState(movedBlock.position.x, movedBlock.position.y, movedBlock.position.z, NULL_BLOCK_STATE);
            ++changed;
        }
        for (const PistonMovedBlock& movedBlock : plan.movedBlocks) {
            createMovingBlock(
                *registry,
                movedBlock.state,
                movedBlock.position,
                movedBlock.position + pushDirection,
                pushDirection,
                true);
        }

        const glm::ivec3 frontPosition = pistonPosition + pushDirection;
        createMovingBlock(*registry, pistonHeadState(pistonState), pistonPosition, frontPosition, pushDirection, true);
        world.setBlockState(pistonPosition.x, pistonPosition.y, pistonPosition.z, withExtended(pistonState, true));
        return changed + 1;
    }

    size_t changed = 0;
    for (auto it = plan.movedBlocks.rbegin(); it != plan.movedBlocks.rend(); ++it) {
        const glm::ivec3 target = it->position + pushDirection;
        world.setBlockState(target.x, target.y, target.z, it->state);
        ++changed;
    }

    const glm::ivec3 frontPosition = pistonPosition + pushDirection;
    world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, pistonHeadState(pistonState));
    world.setBlockState(pistonPosition.x, pistonPosition.y, pistonPosition.z, withExtended(pistonState, true));
    for (const PistonMovedBlock& movedBlock : plan.movedBlocks) {
        pushEntitiesFromMovedBlock(world, registry, movedBlock.position + pushDirection, pushDirection);
    }
    pushEntitiesFromMovedBlock(world, registry, frontPosition, pushDirection);
    return changed + 2;
}

bool isStickyPistonState(const BlockStateId stateId) {
    return BlockStateRegistry::getBlockId(stateId) == stickyPistonBlockId();
}

size_t applyPistonRetraction(World& world,
                             GameplayRegistry* registry,
                             const glm::ivec3& pistonPosition,
                             const BlockStateId pistonState,
                             const glm::ivec3& facingDirection) {
    size_t changed = 0;
    const glm::ivec3 frontPosition = pistonPosition + facingDirection;
    const BlockStateId frontState = world.getBlockState(frontPosition.x, frontPosition.y, frontPosition.z);
    const bool removedMatchingHead = isMatchingPistonHead(frontState, pistonState);
    if (removedMatchingHead) {
        world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, NULL_BLOCK_STATE);
        ++changed;
        if (registry != nullptr) {
            createMovingBlock(*registry, frontState, frontPosition, pistonPosition, -facingDirection, false);
        }
    }

    if (removedMatchingHead && isStickyPistonState(pistonState)) {
        const glm::ivec3 pullPosition = frontPosition + facingDirection;
        const BlockStateId pullState = world.getBlockState(pullPosition.x, pullPosition.y, pullPosition.z);
        if (isMovablePistonBlock(pullState)) {
            world.setBlockState(pullPosition.x, pullPosition.y, pullPosition.z, NULL_BLOCK_STATE);
            if (registry != nullptr) {
                createMovingBlock(*registry, pullState, pullPosition, frontPosition, -facingDirection, true);
            } else {
                world.setBlockState(frontPosition.x, frontPosition.y, frontPosition.z, pullState);
                pushEntitiesFromMovedBlock(world, registry, frontPosition, -facingDirection);
            }
            changed += 2;
        }
    }

    world.setBlockState(pistonPosition.x, pistonPosition.y, pistonPosition.z, withExtended(pistonState, false));
    return changed + 1;
}

void addRedstoneControlledIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isRedstoneControlledState(stateId)) {
        return;
    }
    if (workSet.redstoneControlledSet.insert(position).second) {
        workSet.redstoneControlledBlocks.push_back(position);
    }
}

void addTorchIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isTorchState(stateId)) {
        return;
    }
    if (workSet.torchSet.insert(position).second) {
        workSet.torches.push_back(position);
    }
}

void addRepeaterIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isRepeaterState(stateId)) {
        return;
    }
    if (workSet.repeaterSet.insert(position).second) {
        workSet.repeaters.push_back(position);
    }
}

void addObserverIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(stateId)) {
        return;
    }
    if (workSet.observerSet.insert(position).second) {
        workSet.observers.push_back(position);
    }
}

void addComparatorIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isComparatorState(stateId)) {
        return;
    }
    if (workSet.comparatorSet.insert(position).second) {
        workSet.comparators.push_back(position);
    }
}

void addPistonIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isPistonState(stateId)) {
        return;
    }
    if (workSet.pistonSet.insert(position).second) {
        workSet.pistons.push_back(position);
    }
}

void addEdgeTriggeredDeviceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isEdgeTriggeredDeviceState(stateId)) {
        return;
    }
    if (workSet.edgeTriggeredDeviceSet.insert(position).second) {
        workSet.edgeTriggeredDevices.push_back(position);
    }
}

void addSourceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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

    std::queue<WireSearchNode> frontier;
    frontier.push({start, 0});
    workSet.wires.push_back(start);

    while (!frontier.empty()) {
        const WireSearchNode node = frontier.front();
        frontier.pop();
        const glm::ivec3 position = node.position;

        // Redstone wire power cannot travel beyond fifteen wire steps from a
        // source. Dirty updates only need the local power radius; far-away
        // wires in the same physical dust network keep their stored state until
        // a nearby source or wire change dirties their own radius.
        if (node.distance < kMaxRedstonePower) {
            forEachWireNeighbor(world, position, [&](const glm::ivec3& wireNeighbor) {
                if (workSet.wireSet.insert(wireNeighbor).second) {
                    workSet.wires.push_back(wireNeighbor);
                    frontier.push({wireNeighbor, static_cast<uint8_t>(node.distance + 1)});
                }
            });
        }

        // Check cardinal direction neighbors for non-wire redstone components.
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
            const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
        const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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

        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
        const BlockStateId stateId = world.getBlockState(
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
    if (power == 0) {
        return;
    }
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
    const BlockStateId sourceState = world.getBlockState(source.position.x, source.position.y, source.position.z);
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
    wirePowers.evaluatedWires = &wires;
    std::priority_queue<PowerNode> frontier;

    // Redstone power can travel at most fifteen wire steps from each source.
    // Store only non-zero wire powers and keep the evaluated wire set beside
    // the map so readers can still distinguish evaluated zero power from
    // stable outside-of-work-set state.
    wirePowers.reserve(std::min(wires.size(), sources.size() * 512U + 64U));

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

glm::ivec3 attachedBlockForTorch(const BlockStateId stateId, const glm::ivec3& position) {
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
    const BlockStateId sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
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
    const BlockStateId signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState)) {
        const auto computedIt = wirePowers.find(signalPosition);
        if (computedIt != wirePowers.end()) {
            return computedIt->second;
        }
        return wirePowers.wasEvaluated(signalPosition) ? 0 : redstonePowerFromState(signalState);
    }
    return sourceOutputPowerToward(world, signalPosition, targetPosition);
}

uint8_t sourceStrongPowerTowardConductor(const World& world,
                                         const glm::ivec3& sourcePosition,
                                         const glm::ivec3& conductorPosition) {
    const BlockStateId sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
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
    if (computedIt != wirePowers.end()) {
        return computedIt->second;
    }
    return wirePowers.wasEvaluated(wirePosition)
        ? 0
        : redstonePowerFromState(world.getBlockState(wirePosition.x, wirePosition.y, wirePosition.z));
}

uint8_t conductiveBlockInputPowerToward(const World& world,
                                        const WirePowerMap& wirePowers,
                                        const glm::ivec3& signalPosition,
                                        const glm::ivec3& conductorPosition) {
    const BlockStateId signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
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

        const BlockStateId neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
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
                      const BlockStateId torchState) {
    const glm::ivec3 attachedBlock = attachedBlockForTorch(torchState, torchPosition);
    if (!isConductiveBlockAt(world, attachedBlock)) {
        return true;
    }
    return conductiveBlockPowerAt(world, inputWirePowers, attachedBlock, &torchPosition) == 0;
}

void pruneTorchTurnOffHistory(RedstoneTorchRuntimeState& torchRuntime, const uint64_t redstoneTick) {
    while (!torchRuntime.turnOffTicks.empty() &&
           redstoneTick - torchRuntime.turnOffTicks.front() > kTorchBurnoutWindowTicks) {
        torchRuntime.turnOffTicks.pop_front();
    }
}

bool recordTorchTurnOff(RedstoneTorchRuntimeState& torchRuntime, const uint64_t redstoneTick) {
    pruneTorchTurnOffHistory(torchRuntime, redstoneTick);
    torchRuntime.turnOffTicks.push_back(redstoneTick);
    return torchRuntime.turnOffTicks.size() >= kTorchBurnoutTurnOffLimit;
}

void compactTorchRuntimeState(RedstoneRuntimeState& runtime,
                              const glm::ivec3& position,
                              const uint64_t redstoneTick) {
    const auto it = runtime.torches.find(position);
    if (it == runtime.torches.end()) {
        return;
    }

    pruneTorchTurnOffHistory(it->second, redstoneTick);
    if (!it->second.burnedOut && it->second.turnOffTicks.empty()) {
        runtime.torches.erase(it);
    }
}

size_t applyTorchStates(World& world,
                        const std::vector<glm::ivec3>& torches,
                        const PositionSet& wires,
                        const std::vector<glm::ivec3>& sourcePositions,
                        const std::vector<glm::ivec3>& repeaterPositions,
                        const std::vector<glm::ivec3>& observerPositions,
                        const std::vector<glm::ivec3>& comparatorPositions,
                        const uint64_t redstoneTick) {
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
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
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

        bool shouldBeLit = shouldTorchBeLit(world, inputWirePowers, position, currentState);
        auto& runtime = world.redstoneRuntimeState();
        auto runtimeIt = runtime.torches.find(position);
        if (runtimeIt != runtime.torches.end() &&
            runtimeIt->second.burnedOut &&
            redstoneTick >= runtimeIt->second.cooldownEndsAtTick) {
            runtimeIt->second.burnedOut = false;
            pruneTorchTurnOffHistory(runtimeIt->second, redstoneTick);
        }

        runtimeIt = runtime.torches.find(position);
        if (runtimeIt != runtime.torches.end() && runtimeIt->second.burnedOut) {
            shouldBeLit = false;
        } else if (!shouldBeLit && isLitPropertyTrue(currentState)) {
            RedstoneTorchRuntimeState& torchRuntime = runtime.torches[position];
            if (recordTorchTurnOff(torchRuntime, redstoneTick)) {
                torchRuntime.burnedOut = true;
                torchRuntime.cooldownEndsAtTick = redstoneTick + kTorchBurnoutCooldownTicks;
                world.redstoneScheduledUpdateQueue().reschedule(
                    torchRuntime.cooldownEndsAtTick,
                    position,
                    RedstoneScheduledAction::ResetTorchBurnout);
            }
        }

        const BlockStateId updatedState = withLit(currentState, shouldBeLit);
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
        compactTorchRuntimeState(runtime, position, redstoneTick);
    }
    return changed;
}

uint8_t repeaterInputPower(const World& world,
                           const WirePowerMap& wirePowers,
                           const glm::ivec3& repeaterPosition,
                           const BlockStateId repeaterState) {
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

bool repeaterLockInputActive(const World& world,
                             const glm::ivec3& repeaterPosition,
                             const BlockStateId repeaterState) {
    for (const glm::ivec3& sideDirection : repeaterSideDirections(repeaterState)) {
        const glm::ivec3 sidePosition = repeaterPosition + sideDirection;
        const BlockStateId sideState = world.getBlockState(sidePosition.x, sidePosition.y, sidePosition.z);
        if (!isRepeaterState(sideState) && !isComparatorState(sideState)) {
            continue;
        }
        if (sourceOutputPowerToward(world, sidePosition, repeaterPosition) > 0) {
            return true;
        }
    }
    return false;
}

uint8_t comparatorOutputPower(const World& world,
                              const GameplayRegistry* registry,
                              const WirePowerMap& inputWirePowers,
                              const glm::ivec3& comparatorPosition,
                              const BlockStateId comparatorState) {
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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
        const BlockStateId currentState = world.getBlockState(
            evaluation.position.x,
            evaluation.position.y,
            evaluation.position.z);
        if (!isComparatorState(currentState)) {
            continue;
        }

        const BlockStateId updatedState = withPowered(currentState, evaluation.outputPower > 0);
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isRepeaterState(stateId)) {
            continue;
        }
        if (isLockedPropertyTrue(stateId)) {
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
            const BlockStateId observerState = world.getBlockState(
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
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
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

size_t applyRepeaterLockStates(World& world, const std::vector<glm::ivec3>& repeaterPositions) {
    size_t changed = 0;
    for (const glm::ivec3& position : repeaterPositions) {
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isRepeaterState(currentState)) {
            continue;
        }

        const bool shouldBeLocked = repeaterLockInputActive(world, position, currentState);
        if (isLockedPropertyTrue(currentState) == shouldBeLocked) {
            continue;
        }

        world.setBlockState(position.x, position.y, position.z, withLocked(currentState, shouldBeLocked));
        ++changed;
    }
    return changed;
}

bool applyButtonRelease(World& world, const glm::ivec3& position) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isButtonState(currentState)) {
        return false;
    }
    if (!hasBooleanPropertyValue(currentState, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered")) {
        return false;
    }

    const BlockStateId updatedState = withPowered(currentState, false);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyObserverPulseStart(World& world, const glm::ivec3& position, const uint64_t redstoneTick) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(currentState)) {
        return false;
    }
    if (isPoweredPropertyTrue(currentState)) {
        return false;
    }

    const BlockStateId updatedState = withPowered(currentState, true);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    world.redstoneScheduledUpdateQueue().schedule(
        redstoneTick + kObserverPulseDurationTicks,
        position,
        RedstoneScheduledAction::ReleaseObserverPulse);
    return true;
}

bool applyObserverPulseRelease(World& world, const glm::ivec3& position) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isObserverState(currentState)) {
        return false;
    }
    if (!isPoweredPropertyTrue(currentState)) {
        return false;
    }

    const BlockStateId updatedState = withPowered(currentState, false);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyTargetPulseRelease(World& world, const glm::ivec3& position) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isTargetState(currentState)) {
        return false;
    }
    if (redstonePowerFromState(currentState) == 0) {
        return false;
    }

    const BlockStateId updatedState = withRedstonePower(currentState, 0);
    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyTorchBurnoutReset(World& world, const glm::ivec3& position, const uint64_t redstoneTick) {
    auto& runtime = world.redstoneRuntimeState();
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isTorchState(currentState)) {
        runtime.eraseTorch(position);
        return false;
    }

    const auto runtimeIt = runtime.torches.find(position);
    if (runtimeIt == runtime.torches.end()) {
        return false;
    }

    RedstoneTorchRuntimeState& torchRuntime = runtimeIt->second;
    if (torchRuntime.burnedOut && redstoneTick < torchRuntime.cooldownEndsAtTick) {
        return false;
    }

    torchRuntime.burnedOut = false;
    torchRuntime.cooldownEndsAtTick = 0;
    pruneTorchTurnOffHistory(torchRuntime, redstoneTick);

    const WirePowerMap wirePowers;
    const BlockStateId updatedState = withLit(
        currentState,
        shouldTorchBeLit(world, wirePowers, position, currentState));
    compactTorchRuntimeState(runtime, position, redstoneTick);
    if (updatedState == currentState) {
        return false;
    }

    world.setBlockState(position.x, position.y, position.z, updatedState);
    return true;
}

bool applyRepeaterEvaluation(World& world, const glm::ivec3& position) {
    const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
    if (!isRepeaterState(currentState)) {
        return false;
    }

    const bool currentlyLocked = isLockedPropertyTrue(currentState);
    const bool shouldBeLocked = repeaterLockInputActive(world, position, currentState);
    if (currentlyLocked || shouldBeLocked) {
        if (currentlyLocked != shouldBeLocked) {
            world.setBlockState(position.x, position.y, position.z, withLocked(currentState, shouldBeLocked));
            return true;
        }
        return false;
    }

    const WirePowerMap wirePowers;
    const bool shouldBePowered = repeaterInputPower(world, wirePowers, position, currentState) > 0;
    const BlockStateId updatedState = withPowered(currentState, shouldBePowered);
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
        case RedstoneScheduledAction::ResetTorchBurnout:
            if (applyTorchBurnoutReset(world, update.position, update.executionTick)) {
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
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isWireState(currentState)) {
            continue;
        }

        const BlockStateId updatedState = withRedstonePower(currentState, computedWirePowerAt(wirePowers, position));
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

size_t applyPistonStates(World& world,
                         GameplayRegistry* registry,
                         const std::vector<glm::ivec3>& pistons,
                         const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : pistons) {
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isPistonState(currentState)) {
            continue;
        }

        const bool currentlyExtended = isExtendedPropertyTrue(currentState);
        const bool shouldExtend = receivedPowerAt(world, wirePowers, position) > 0;
        if (currentlyExtended == shouldExtend) {
            continue;
        }

        const glm::ivec3 facingDirection = pistonFacingDirection(currentState);
        if (hasActivePistonMovement(registry, position, facingDirection)) {
            continue;
        }

        if (shouldExtend) {
            PistonPushPlan pushPlan;
            if (!buildPistonPushPlan(world, position, facingDirection, pushPlan)) {
                continue;
            }

            changed += applyPistonPush(world, registry, position, currentState, facingDirection, pushPlan);
            continue;
        }

        changed += applyPistonRetraction(world, registry, position, currentState, facingDirection);
    }
    return changed;
}

size_t applyRedstoneControlledStates(World& world,
                                     const std::vector<glm::ivec3>& positions,
                                     const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : positions) {
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isRedstoneControlledState(currentState)) {
            continue;
        }

        const bool shouldBePowered = receivedPowerAt(world, wirePowers, position) > 0;
        const BlockStateId updatedState = withRedstoneControlledPower(currentState, shouldBePowered);
        if (updatedState != currentState) {
            if (DoorBlockLogic::isDoorState(currentState)) {
                const bool powered =
                    BlockStateRegistry::getPropertyIndex(updatedState, PropIndices::POWERED) ==
                    PropIndices::POWERED_TRUE;
                DoorBlockLogic::setDoorPoweredOpen(world, position, powered);
            } else {
                world.setBlockState(position.x, position.y, position.z, updatedState);
            }
            ++changed;
        }
    }
    return changed;
}

void emitEdgeTriggeredDeviceEvent(GameplayRegistry& registry,
                                  const BlockStateId stateId,
                                  const glm::ivec3& position,
                                  const uint64_t redstoneTick) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior != "note_block" &&
        def.redstoneBehavior != "dispenser" &&
        def.redstoneBehavior != "dropper") {
        throw std::runtime_error("Unsupported redstone edge-triggered behavior: " + def.redstoneBehavior);
    }
    ensureRedstoneDeviceActivationEventBus(registry).push({
        position,
        blockId,
        stateId,
        redstoneTick
    });
}

size_t applyEdgeTriggeredDeviceStates(World& world,
                                      GameplayRegistry* registry,
                                      const std::vector<glm::ivec3>& positions,
                                      const WirePowerMap& wirePowers,
                                      const uint64_t redstoneTick) {
    size_t changed = 0;
    for (const glm::ivec3& position : positions) {
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isEdgeTriggeredDeviceState(currentState)) {
            continue;
        }

        const bool currentlyPowered = isPoweredPropertyTrue(currentState);
        const bool shouldBePowered = receivedPowerAt(world, wirePowers, position) > 0;
        if (!currentlyPowered && shouldBePowered && registry != nullptr) {
            emitEdgeTriggeredDeviceEvent(*registry, currentState, position, redstoneTick);
        }

        const BlockStateId updatedState = withPowered(currentState, shouldBePowered);
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

size_t processWorldWithContext(World& world,
                               const GameplayRegistry* readRegistry,
                               GameplayRegistry* mutableRegistry,
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
        workSet.comparators,
        redstoneTick);
    scheduleButtonReleaseUpdates(world, redstoneTick, workSet.sourcePositions);

    std::vector<RedstoneSource> outputSources =
        collectActiveSources(world, workSet.sourcePositions, nullptr);
    appendSources(outputSources, collectPoweredRepeaterSources(world, workSet.repeaters));
    appendSources(outputSources, collectPoweredObserverSources(world, workSet.observers));
    std::vector<ComparatorEvaluation> comparatorEvaluations;
    if (!workSet.comparators.empty()) {
        const WirePowerMap inputWirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
        comparatorEvaluations = evaluateComparators(world, readRegistry, workSet.comparators, inputWirePowers);
        appendSources(outputSources, collectComparatorSources(world, comparatorEvaluations));
    }
    const WirePowerMap wirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
    changed += applyWirePowers(world, workSet.wires, wirePowers);
    changed += applyComparatorStates(world, comparatorEvaluations);
    changed += applyPistonStates(world, mutableRegistry, workSet.pistons, wirePowers);
    changed += applyRepeaterLockStates(world, workSet.repeaters);
    scheduleRepeaterEvaluationUpdates(world, redstoneTick, workSet.repeaters, wirePowers);
    changed += applyRedstoneControlledStates(world, workSet.redstoneControlledBlocks, wirePowers);
    changed += applyEdgeTriggeredDeviceStates(
        world,
        mutableRegistry,
        workSet.edgeTriggeredDevices,
        wirePowers,
        redstoneTick);
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

size_t RedstoneSystem::processWorld(World& world,
                                    const uint64_t redstoneTick,
                                    GameplayRegistry& registry,
                                    const size_t budget) {
    return processWorldWithContext(world, &registry, &registry, redstoneTick, budget);
}

} // namespace ecs
