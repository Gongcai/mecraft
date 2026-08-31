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
#include "../../../world/block/AttachmentFaceGeometry.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockCollision.h"
#include "../../../world/block/DoorBlock.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/RedstoneTick.h"
#include "../../../world/redstone/RedstoneUpdateQueue.h"
#include "../../../world/redstone/WireContainerParts.h"
#include "../../../world/redstone/WireFaceGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
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
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

[[noreturn]] void failRedstoneSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

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

struct WireNode {
    glm::ivec3 position;
    uint16_t channelId = 0;
    uint16_t facing = PropIndices::INVALID;

    [[nodiscard]] bool operator==(const WireNode& other) const noexcept {
        return position == other.position && channelId == other.channelId && facing == other.facing;
    }
};

struct WireNodeHash {
    std::size_t operator()(const WireNode& node) const noexcept {
        std::size_t seed = IVec3Hash{}(node.position);
        const auto mix = [&seed](const uint16_t component) {
            seed ^= std::hash<uint16_t>{}(component) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
        mix(node.channelId);
        mix(node.facing);
        return seed;
    }
};

using WireNodeSet = std::unordered_set<WireNode, WireNodeHash>;

struct WirePowerMap {
    using Map = std::unordered_map<WireNode, uint8_t, WireNodeHash>;

    Map powers;
    const WireNodeSet* evaluatedWires = nullptr;

    void reserve(const size_t count) { powers.reserve(count); }

    auto emplace(const WireNode& node, const uint8_t power) { return powers.emplace(node, power); }

    [[nodiscard]] auto find(const WireNode& node) { return powers.find(node); }

    [[nodiscard]] auto find(const WireNode& node) const { return powers.find(node); }

    [[nodiscard]] auto end() { return powers.end(); }

    [[nodiscard]] auto end() const { return powers.end(); }

    [[nodiscard]] bool wasEvaluated(const WireNode& node) const {
        return evaluatedWires != nullptr && evaluatedWires->find(node) != evaluatedWires->end();
    }
};

struct RedstoneSource {
    glm::ivec3 position;
    uint8_t power = 0;
    glm::ivec3 outputDirection{};
    bool directional = false;
    uint16_t targetWireFacing = PropIndices::INVALID;
};

struct RedstoneWorkSet {
    std::vector<WireNode> wires;
    std::vector<glm::ivec3> redstoneControlledBlocks;
    std::vector<glm::ivec3> torches;
    std::vector<glm::ivec3> repeaters;
    std::vector<glm::ivec3> observers;
    std::vector<glm::ivec3> comparators;
    std::vector<glm::ivec3> pistons;
    std::vector<glm::ivec3> edgeTriggeredDevices;
    std::vector<glm::ivec3> sourcePositions;
    WireNodeSet wireSet;
    PositionSet redstoneControlledSet;
    PositionSet torchSet;
    PositionSet repeaterSet;
    PositionSet observerSet;
    PositionSet comparatorSet;
    PositionSet pistonSet;
    PositionSet edgeTriggeredDeviceSet;
    PositionSet sourceSet;
};

void clearRedstoneWorkSet(RedstoneWorkSet& workSet) {
    workSet.wires.clear();
    workSet.redstoneControlledBlocks.clear();
    workSet.torches.clear();
    workSet.repeaters.clear();
    workSet.observers.clear();
    workSet.comparators.clear();
    workSet.pistons.clear();
    workSet.edgeTriggeredDevices.clear();
    workSet.sourcePositions.clear();
    workSet.wireSet.clear();
    workSet.redstoneControlledSet.clear();
    workSet.torchSet.clear();
    workSet.repeaterSet.clear();
    workSet.observerSet.clear();
    workSet.comparatorSet.clear();
    workSet.pistonSet.clear();
    workSet.edgeTriggeredDeviceSet.clear();
    workSet.sourceSet.clear();
}

template <typename Set> void reserveSetCapacity(Set& set, const size_t count) {
    if (static_cast<float>(count) > static_cast<float>(set.bucket_count()) * set.max_load_factor()) {
        set.reserve(count);
    }
}

void reserveRedstoneWorkSet(RedstoneWorkSet& workSet, const size_t dirtyCount) {
    const size_t expectedPositions = dirtyCount;
    workSet.wires.reserve(expectedPositions);
    workSet.redstoneControlledBlocks.reserve(expectedPositions);
    workSet.torches.reserve(expectedPositions);
    workSet.repeaters.reserve(expectedPositions);
    workSet.observers.reserve(expectedPositions);
    workSet.comparators.reserve(expectedPositions);
    workSet.pistons.reserve(expectedPositions);
    workSet.edgeTriggeredDevices.reserve(expectedPositions);
    workSet.sourcePositions.reserve(expectedPositions);
    reserveSetCapacity(workSet.wireSet, expectedPositions);
    reserveSetCapacity(workSet.redstoneControlledSet, expectedPositions);
    reserveSetCapacity(workSet.torchSet, expectedPositions);
    reserveSetCapacity(workSet.repeaterSet, expectedPositions);
    reserveSetCapacity(workSet.observerSet, expectedPositions);
    reserveSetCapacity(workSet.comparatorSet, expectedPositions);
    reserveSetCapacity(workSet.pistonSet, expectedPositions);
    reserveSetCapacity(workSet.edgeTriggeredDeviceSet, expectedPositions);
    reserveSetCapacity(workSet.sourceSet, expectedPositions);
}

struct PowerNode {
    WireNode wire;
    uint8_t power = 0;

    bool operator<(const PowerNode& other) const { return power < other.power; }
};

struct WireSearchNode {
    WireNode wire;
    uint8_t distance = 0;
};

struct ComparatorEvaluation {
    glm::ivec3 position;
    uint8_t outputPower = 0;
};

struct RedstoneProcessBuffers {
    std::vector<RedstoneScheduledUpdate> scheduledUpdates;
    std::vector<glm::ivec3> changedPositions;
    std::vector<glm::ivec3> dirtyPositions;
    std::vector<RedstoneSource> outputSources;
    std::vector<ComparatorEvaluation> comparatorEvaluations;
    RedstoneWorkSet workSet;
};

thread_local RedstoneProcessBuffers t_redstoneBuffers;

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

bool isWireContainerState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isWireContainer;
}

uint16_t redstoneWireChannelIdForState(const BlockStateId stateId) {
    if (!isWireState(stateId)) {
        failRedstoneSystem("Redstone wire channel requires a wire state");
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneWireChannelId == 0) {
        failRedstoneSystem("Redstone wire state is missing redstoneWireChannelId");
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
        failRedstoneSystem(std::string("Redstone requires registered property: ") + propertyName);
    }
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem(std::string("Redstone state is missing property: ") + propertyName);
    }
    return value;
}

uint16_t wireFacingForState(const BlockStateId stateId) {
    const uint16_t facing = getRequiredProperty(stateId, PropIndices::FACING, "facing");
    if (!WireFaceGeometry::isWireFacing(facing)) {
        failRedstoneSystem("Redstone wire state contains an unsupported facing value");
    }
    return facing;
}

bool isMatchingWireStateWithFacing(const BlockStateId stateId, const uint16_t wireChannelId,
                                   const uint16_t wireFacing) {
    if (!isMatchingWireState(stateId, wireChannelId)) {
        return false;
    }
    return wireFacingForState(stateId) == wireFacing;
}

WireNode wireNodeFromState(const glm::ivec3& position, const BlockStateId stateId) {
    return {position, redstoneWireChannelIdForState(stateId), wireFacingForState(stateId)};
}

bool isMatchingWireNodeState(const BlockStateId stateId, const WireNode& node) {
    return isMatchingWireStateWithFacing(stateId, node.channelId, node.facing);
}

const WirePart* findWireContainerPart(const World& world, const WireNode& node) {
    const WireContainerParts* parts = world.wireContainerParts().find(node.position);
    return parts == nullptr ? nullptr : parts->find(node.channelId, node.facing);
}

bool hasMatchingWireContainerPart(const World& world, const WireNode& node) {
    return findWireContainerPart(world, node) != nullptr;
}

template <typename Fn> void forEachWireNodeAt(const World& world, const glm::ivec3& position, Fn&& fn) {
    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (isWireState(stateId)) {
        fn(wireNodeFromState(position, stateId));
        return;
    }
    if (!isWireContainerState(stateId)) {
        return;
    }

    const WireContainerParts* parts = world.wireContainerParts().find(position);
    if (parts == nullptr) {
        return;
    }
    parts->forEach([&](const WirePart& part) { fn(WireNode{position, part.channelId, part.facing}); });
}

bool wireNodeExists(const World& world, const WireNode& node) {
    const BlockStateId stateId = world.getBlockState(node.position.x, node.position.y, node.position.z);
    if (isWireState(stateId)) {
        return isMatchingWireNodeState(stateId, node);
    }
    if (isWireContainerState(stateId)) {
        return hasMatchingWireContainerPart(world, node);
    }
    return false;
}

bool isSolidBlockState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isSolid;
}

bool isOuterCornerWireConnectionBlocked(const World& world, const glm::ivec3& support, const uint16_t wireFacing,
                                        const uint16_t neighborFacing) {
    const glm::ivec3 blocker = WireFaceGeometry::outerCornerBlockingPosition(support, wireFacing, neighborFacing);
    return isSolidBlockState(world.getBlockState(blocker.x, blocker.y, blocker.z));
}

template <typename Fn> void forEachOuterCornerWireNeighbor(const World& world, const WireNode& wire, Fn&& fn) {
    const glm::ivec3 support = WireFaceGeometry::supportPosition(wire.position, wire.facing);
    for (const uint16_t neighborFacing : WireFaceGeometry::wireFacings()) {
        if (!WireFaceGeometry::arePerpendicularFacings(wire.facing, neighborFacing)) {
            continue;
        }
        if (isOuterCornerWireConnectionBlocked(world, support, wire.facing, neighborFacing)) {
            continue;
        }

        const glm::ivec3 neighborPosition = WireFaceGeometry::wirePositionOnSupportFace(support, neighborFacing);
        const WireNode neighbor{neighborPosition, wire.channelId, neighborFacing};
        const BlockStateId neighborState =
            world.getBlockState(neighborPosition.x, neighborPosition.y, neighborPosition.z);
        if (isWireState(neighborState) && isMatchingWireNodeState(neighborState, neighbor)) {
            fn(neighbor);
            continue;
        }
        if (isWireContainerState(neighborState) && hasMatchingWireContainerPart(world, neighbor)) {
            fn(neighbor);
        }
    }
}

BlockStateId withRequiredProperty(const BlockStateId stateId, const uint16_t property, const uint16_t value,
                                  const char* propertyName) {
    const uint16_t current = getRequiredProperty(stateId, property, propertyName);
    if (current == value) {
        return stateId;
    }

    const BlockStateId updated = BlockStateRegistry::withProperty(stateId, property, value);
    if (BlockStateRegistry::getPropertyIndex(updated, property) != value) {
        failRedstoneSystem(std::string("Redstone failed to update property: ") + propertyName);
    }
    return updated;
}

std::array<uint16_t, 16> powerPropertyValues() {
    return {
        PropIndices::POWER_0,  PropIndices::POWER_1,  PropIndices::POWER_2,  PropIndices::POWER_3,
        PropIndices::POWER_4,  PropIndices::POWER_5,  PropIndices::POWER_6,  PropIndices::POWER_7,
        PropIndices::POWER_8,  PropIndices::POWER_9,  PropIndices::POWER_10, PropIndices::POWER_11,
        PropIndices::POWER_12, PropIndices::POWER_13, PropIndices::POWER_14, PropIndices::POWER_15,
    };
}

uint16_t powerToPropertyValue(const uint8_t power) {
    if (power > kMaxRedstonePower) {
        failRedstoneSystem("Redstone wire power exceeds 15");
    }
    const auto values = powerPropertyValues();
    const uint16_t value = values[power];
    if (value == PropIndices::INVALID) {
        failRedstoneSystem("Redstone requires registered power values 0 through 15");
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
    failRedstoneSystem("Redstone state contains an unknown power value");
}

BlockStateId withRedstonePower(const BlockStateId stateId, const uint8_t power) {
    return withRequiredProperty(stateId, PropIndices::POWER, powerToPropertyValue(power), "power");
}

BlockStateId withLit(const BlockStateId stateId, const bool lit) {
    const uint16_t value = lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE;
    if (value == PropIndices::INVALID) {
        failRedstoneSystem("Redstone requires registered lit boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::LIT, value, "lit");
}

BlockStateId withPowered(const BlockStateId stateId, const bool powered) {
    const uint16_t value = powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE;
    if (value == PropIndices::INVALID) {
        failRedstoneSystem("Redstone requires registered powered boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::POWERED, value, "powered");
}

BlockStateId withLocked(const BlockStateId stateId, const bool locked) {
    const uint16_t value = locked ? PropIndices::LOCKED_TRUE : PropIndices::LOCKED_FALSE;
    if (value == PropIndices::INVALID) {
        failRedstoneSystem("Redstone requires registered locked boolean values");
    }
    return withRequiredProperty(stateId, PropIndices::LOCKED, value, "locked");
}

bool hasBooleanPropertyValue(const BlockStateId stateId, const uint16_t property, const uint16_t expectedValue,
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

    if (def.redstoneBehavior == "lever" || def.redstoneBehavior == "button" || def.redstoneBehavior == "plate") {
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

    failRedstoneSystem("Unsupported redstone power source behavior: " + def.redstoneBehavior);
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

    if (def.redstoneBehavior == "lever" || def.redstoneBehavior == "button" || def.redstoneBehavior == "plate" ||
        def.redstoneBehavior == "power_block" || def.redstoneBehavior == "target") {
        return true;
    }

    if (def.redstoneBehavior == "torch") {
        return false;
    }

    failRedstoneSystem("Unsupported redstone power source behavior: " + def.redstoneBehavior);
}

bool isHorizontalDirection(const glm::ivec3& direction) {
    return (direction == glm::ivec3(1, 0, 0)) || (direction == glm::ivec3(-1, 0, 0)) ||
           (direction == glm::ivec3(0, 0, 1)) || (direction == glm::ivec3(0, 0, -1));
}

bool wirePowersConductorToward(const World& world, const glm::ivec3& wirePosition,
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

template <typename StackReader> uint8_t inventorySignalPower(const int slotCount, StackReader&& stackReader) {
    if (slotCount <= 0) {
        failRedstoneSystem("Redstone container signal requires at least one inventory slot");
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
            failRedstoneSystem("Redstone container signal found an item with no valid stack size");
        }
        if (stack.count > itemDef.maxStack) {
            failRedstoneSystem("Redstone container signal found a stack above its maximum size");
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

uint8_t containerSignalPowerAt(const World& world, const GameplayRegistry* registry, const glm::ivec3& position) {
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
        return inventorySignalPower(behavior.storage.slots,
                                    [inventory](const int slot) { return inventory->getSlotStack(slot); });
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
        return inventorySignalPower(behavior.storage.slots,
                                    [machine](const int slot) { return machine->getSlotStack(slot); });
    }

    failRedstoneSystem("Comparator signal is not implemented for container behavior handler: " + behavior.handler);
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

// Calls fn for every wire node connected to the wire at pos. Wires connect
// within their own face plane and same-color wire_container faces connect
// across the shared edge inside one block space.
template <typename Fn> void forEachWireNeighbor(const World& world, const WireNode& wire, Fn&& fn) {
    if (!wireNodeExists(world, wire)) {
        return;
    }

    const auto tryNeighbor = [&](const glm::ivec3& position, const uint16_t facing) {
        const WireNode neighbor{position, wire.channelId, facing};
        const BlockStateId neighborState = world.getBlockState(position.x, position.y, position.z);
        if (isWireState(neighborState) && isMatchingWireNodeState(neighborState, neighbor)) {
            fn(neighbor);
            return;
        }
        if (isWireContainerState(neighborState) && hasMatchingWireContainerPart(world, neighbor)) {
            fn(neighbor);
        }
    };

    const auto trySameCellFaceNeighbor = [&](const uint16_t facing) {
        if (!WireFaceGeometry::arePerpendicularFacings(wire.facing, facing)) {
            return;
        }
        const WireNode neighbor{wire.position, wire.channelId, facing};
        if (hasMatchingWireContainerPart(world, neighbor)) {
            fn(neighbor);
        }
    };

    if (wire.facing == PropIndices::FACING_FLOOR) {
        for (const WireFaceGeometry::ConnectionDirection& connection :
             WireFaceGeometry::connectionDirections(wire.facing)) {
            tryNeighbor(wire.position + connection.offset, PropIndices::FACING_FLOOR);
        }
        for (const uint16_t facing : WireFaceGeometry::wireFacings()) {
            trySameCellFaceNeighbor(facing);
        }
        forEachOuterCornerWireNeighbor(world, wire, fn);
        return;
    }

    for (const WireFaceGeometry::ConnectionDirection& connection :
         WireFaceGeometry::connectionDirections(wire.facing)) {
        tryNeighbor(wire.position + connection.offset, wire.facing);
    }
    for (const uint16_t facing : WireFaceGeometry::wireFacings()) {
        trySameCellFaceNeighbor(facing);
    }
    forEachOuterCornerWireNeighbor(world, wire, fn);
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
    return def.respondsToRedstone && (def.redstoneBehavior == "note_block" || def.redstoneBehavior == "dispenser" ||
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
    return stateId != NULL_BLOCK_STATE && BlockStateRegistry::getBlockId(stateId) == pistonHeadBlockId();
}

uint64_t buttonPulseTicks(const BlockStateId stateId) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior != "button") {
        failRedstoneSystem("Redstone pulse duration requires a button block");
    }
    if (def.redstonePulseTicks == 0) {
        failRedstoneSystem("Redstone button has no configured pulse duration: " + def.namespacedId.full());
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
    failRedstoneSystem("Repeater state contains an unknown delay value");
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
    failRedstoneSystem("Observer state contains an unknown facing value");
}

uint16_t logicUnitAttachmentFace(const BlockStateId stateId, const char* deviceName) {
    const uint16_t face = getRequiredProperty(stateId, PropIndices::FACE, "face");
    if (!AttachmentFaceGeometry::isAttachmentFace(face)) {
        failRedstoneSystem(std::string(deviceName) + " state contains an unsupported face value");
    }
    return face;
}

glm::ivec3 logicUnitOutputDirection(const BlockStateId stateId, const char* deviceName) {
    const uint16_t face = logicUnitAttachmentFace(stateId, deviceName);
    const uint16_t facing = getRequiredProperty(stateId, PropIndices::FACING, "facing");
    const glm::ivec3 outputDirection = AttachmentFaceGeometry::directionFromFacing(facing);
    if (!AttachmentFaceGeometry::isDirectionInPlane(face, outputDirection)) {
        failRedstoneSystem(std::string(deviceName) + " output direction must lie on its attached face");
    }
    return outputDirection;
}

glm::ivec3 crossDirection(const glm::ivec3& lhs, const glm::ivec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

std::array<glm::ivec3, 2> logicUnitSideDirections(const BlockStateId stateId, const char* deviceName) {
    const uint16_t face = logicUnitAttachmentFace(stateId, deviceName);
    const glm::ivec3 normal = AttachmentFaceGeometry::surfaceNormal(face);
    const glm::ivec3 outputDirection = logicUnitOutputDirection(stateId, deviceName);
    const glm::ivec3 sideDirection = crossDirection(normal, outputDirection);
    if (sideDirection == glm::ivec3(0)) {
        failRedstoneSystem(std::string(deviceName) + " side direction requires perpendicular face and output");
    }
    return {sideDirection, -sideDirection};
}

uint16_t logicUnitWireFacing(const BlockStateId stateId, const char* deviceName) {
    return AttachmentFaceGeometry::facingValueForFace(logicUnitAttachmentFace(stateId, deviceName));
}

glm::ivec3 repeaterOutputDirection(const BlockStateId stateId) {
    return logicUnitOutputDirection(stateId, "Repeater");
}

std::array<glm::ivec3, 2> repeaterSideDirections(const BlockStateId stateId) {
    return logicUnitSideDirections(stateId, "Repeater");
}

glm::ivec3 comparatorOutputDirection(const BlockStateId stateId) {
    return logicUnitOutputDirection(stateId, "Comparator");
}

std::array<glm::ivec3, 2> comparatorSideDirections(const BlockStateId stateId) {
    return logicUnitSideDirections(stateId, "Comparator");
}

bool comparatorUsesSubtractMode(const BlockStateId stateId) {
    const uint16_t mode = getRequiredProperty(stateId, PropIndices::MODE, "mode");
    if (mode == PropIndices::MODE_COMPARE) {
        return false;
    }
    if (mode == PropIndices::MODE_SUBTRACT) {
        return true;
    }
    failRedstoneSystem("Comparator state contains an unknown mode value");
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
        failRedstoneSystem("Redstone controlled state requires respondsToRedstone and redstoneControlledProperty");
    }

    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(def.redstoneControlledProperty);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem("Redstone controlled property is not registered: " + def.redstoneControlledProperty);
    }

    const uint16_t currentValue = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (currentValue == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem("Redstone controlled block state is missing property: " + def.redstoneControlledProperty);
    }

    const uint16_t falseValue = BlockStateRegistry::getPropertyValueIndex(property, "false");
    const uint16_t trueValue = BlockStateRegistry::getPropertyValueIndex(property, "true");
    if (falseValue == BlockStateRegistry::INVALID_INDEX || trueValue == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem("Redstone controlled property must define false and true values: " +
                           def.redstoneControlledProperty);
    }
    if (currentValue != falseValue && currentValue != trueValue) {
        failRedstoneSystem("Redstone controlled property state must be false or true: " +
                           def.redstoneControlledProperty);
    }
    return property;
}

uint16_t requireBooleanStateProperty(const BlockStateId stateId, const std::string& propertyName, const char* context) {
    const uint16_t property = BlockStateRegistry::getPropertyNameIndex(propertyName);
    if (property == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem(std::string(context) + " property is not registered: " + propertyName);
    }

    const uint16_t currentValue = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (currentValue == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem(std::string(context) + " state is missing property: " + propertyName);
    }

    const uint16_t falseValue = BlockStateRegistry::getPropertyValueIndex(property, "false");
    const uint16_t trueValue = BlockStateRegistry::getPropertyValueIndex(property, "true");
    if (falseValue == BlockStateRegistry::INVALID_INDEX || trueValue == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneSystem(std::string(context) + " property must define false and true values: " + propertyName);
    }
    if (currentValue != falseValue && currentValue != trueValue) {
        failRedstoneSystem(std::string(context) + " property state must be false or true: " + propertyName);
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
        failRedstoneSystem("Redstone controlled state transition failed");
    }

    BlockStateId mirroredState = updatedState;
    for (const std::string& mirrorPropertyName : def.redstoneControlledMirrorProperties) {
        const uint16_t mirrorProperty =
            requireBooleanStateProperty(mirroredState, mirrorPropertyName, "Redstone controlled mirror");
        const uint16_t mirrorValue =
            BlockStateRegistry::getPropertyValueIndex(mirrorProperty, propertyValue ? "true" : "false");
        mirroredState = BlockStateRegistry::withProperty(mirroredState, mirrorProperty, mirrorValue);
        if (BlockStateRegistry::getPropertyIndex(mirroredState, mirrorProperty) != mirrorValue) {
            failRedstoneSystem("Redstone controlled mirror state transition failed");
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
        failRedstoneSystem("Redstone requires registered extended boolean values");
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
    failRedstoneSystem("Piston head type requested for a non-piston block");
}

BlockStateId pistonHeadState(const BlockStateId pistonState) {
    return BlockStateRegistry::getState(
        pistonHeadBlockId(), std::vector<std::pair<uint16_t, uint16_t>>{
                                 {PropIndices::FACING, getRequiredProperty(pistonState, PropIndices::FACING, "facing")},
                                 {PropIndices::TYPE, pistonHeadType(pistonState)}});
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
    return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x && lhs.min.y < rhs.max.y && lhs.max.y > rhs.min.y &&
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

template <typename Fn> void forEachWorldCollisionBox(const World& world, const glm::ivec3& blockPosition, Fn&& fn) {
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
    forEachIntersectingWorldCollisionBox(world, box, [&](const CollisionAabb&) { intersects = true; });
    return intersects;
}

void appendSeparationCandidates(const CollisionAabb& body, const CollisionAabb& obstacle,
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
    forEachIntersectingWorldCollisionBox(
        world, body, [&](const CollisionAabb& obstacle) { appendSeparationCandidates(body, obstacle, candidates); });

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

bool pushPhysicsBodyFromMovingCollision(PhysicsBody& body, const PistonMovementCollision& collision,
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
        failRedstoneSystem("Piston entity push requires a non-zero movement direction");
    }

    applyPhysicsBodyTranslation(body, delta);
    return true;
}

bool physicsBodyRidesMovingCollision(const PhysicsBody& body, const PistonMovementCollision& collision,
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

bool carryPhysicsBodyOnMovingCollision(PhysicsBody& body, const PistonMovementCollision& collision,
                                       const glm::ivec3& movementDirection) {
    if (!physicsBodyRidesMovingCollision(body, collision, movementDirection)) {
        return false;
    }

    applyPhysicsBodyTranslation(body, glm::vec3(movementDirection));
    return true;
}

void pushEntitiesFromMovedBlock(World& world, GameplayRegistry* registry, const glm::ivec3& blockPosition,
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

bool buildPistonPushPlan(const World& world, const glm::ivec3& pistonPosition, const glm::ivec3& pushDirection,
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

    failRedstoneSystem("Piston push scan exceeded its configured maximum distance");
}

void createMovingBlock(GameplayRegistry& registry, const BlockStateId stateId, const glm::ivec3& sourcePosition,
                       const glm::ivec3& targetPosition, const glm::ivec3& direction, const bool placeAtTarget) {
    MovingBlockSpawnParams params;
    params.stateId = stateId;
    params.sourcePosition = sourcePosition;
    params.targetPosition = targetPosition;
    params.direction = direction;
    params.durationSeconds = kPistonMovementDurationSeconds;
    params.placeAtTarget = placeAtTarget;
    EntityFactory::createMovingBlock(registry, params);
}

bool hasActivePistonMovement(const GameplayRegistry* registry, const glm::ivec3& pistonPosition,
                             const glm::ivec3& facingDirection) {
    if (registry == nullptr) {
        return false;
    }

    const glm::ivec3 frontPosition = pistonPosition + facingDirection;
    const auto view = registry->registry().view<MovingBlockTag, MovingBlockComponent>();
    for (const entt::entity entity : view) {
        const auto& block = view.get<MovingBlockComponent>(entity);
        if (block.sourcePosition == pistonPosition || block.targetPosition == pistonPosition ||
            block.sourcePosition == frontPosition || block.targetPosition == frontPosition) {
            return true;
        }
    }
    return false;
}

size_t applyPistonPush(World& world, GameplayRegistry* registry, const glm::ivec3& pistonPosition,
                       const BlockStateId pistonState, const glm::ivec3& pushDirection, const PistonPushPlan& plan) {
    if (registry != nullptr) {
        size_t changed = 0;
        for (const PistonMovedBlock& movedBlock : plan.movedBlocks) {
            world.setBlockState(movedBlock.position.x, movedBlock.position.y, movedBlock.position.z, NULL_BLOCK_STATE);
            ++changed;
        }
        for (const PistonMovedBlock& movedBlock : plan.movedBlocks) {
            createMovingBlock(*registry, movedBlock.state, movedBlock.position, movedBlock.position + pushDirection,
                              pushDirection, true);
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

size_t applyPistonRetraction(World& world, GameplayRegistry* registry, const glm::ivec3& pistonPosition,
                             const BlockStateId pistonState, const glm::ivec3& facingDirection) {
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

void collectWireComponent(const World& world, const WireNode& start, RedstoneWorkSet& workSet) {
    if (!wireNodeExists(world, start)) {
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
        const glm::ivec3 position = node.wire.position;

        // Redstone wire power cannot travel beyond fifteen wire steps from a
        // source. Dirty updates only need the local power radius; far-away
        // wires in the same physical dust network keep their stored state until
        // a nearby source or wire change dirties their own radius.
        if (node.distance < kMaxRedstonePower) {
            forEachWireNeighbor(world, node.wire, [&](const WireNode& wireNeighbor) {
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
        collectWireComponent(world, wireNodeFromState(position, stateId), workSet);
    } else if (isWireContainerState(stateId)) {
        forEachWireNodeAt(world, position, [&](const WireNode& wire) { collectWireComponent(world, wire, workSet); });
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

void collectWireComponentsAt(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    forEachWireNodeAt(world, position, [&](const WireNode& wire) { collectWireComponent(world, wire, workSet); });
}

void collectEndpointsAround(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    for (const glm::ivec3& direction : kDirections) {
        collectEndpointIfPresent(world, position + direction, workSet);
    }
}

void collectEndpointsAroundNeighborConductors(const World& world, const glm::ivec3& position,
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
    if (isWireState(stateId) || isWireContainerState(stateId)) {
        collectWireComponentsAt(world, position, workSet);
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
            if (isWireState(neighborState) || isWireContainerState(neighborState)) {
                collectWireComponentsAt(world, neighbor, workSet);
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
            if (isWireState(neighborState) || isWireContainerState(neighborState)) {
                collectWireComponentsAt(world, neighbor, workSet);
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
            if (isWireState(neighborState) || isWireContainerState(neighborState)) {
                collectWireComponentsAt(world, neighbor, workSet);
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
            if (isWireState(neighborState) || isWireContainerState(neighborState)) {
                collectWireComponentsAt(world, neighbor, workSet);
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
            if (isWireState(neighborState) || isWireContainerState(neighborState)) {
                collectWireComponentsAt(world, neighbor, workSet);
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
        if (isWireState(neighborState) || isWireContainerState(neighborState)) {
            collectWireComponentsAt(world, neighbor, workSet);
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

void collectRedstoneWorkSet(const World& world, const std::vector<glm::ivec3>& dirtyPositions,
                            RedstoneWorkSet& workSet) {
    clearRedstoneWorkSet(workSet);
    reserveRedstoneWorkSet(workSet, dirtyPositions.size());
    for (const glm::ivec3& dirtyPosition : dirtyPositions) {
        collectPosition(world, dirtyPosition, workSet);
        for (const glm::ivec3& direction : kDirections) {
            collectPosition(world, dirtyPosition + direction, workSet);
        }
    }
}

void appendActiveSources(const World& world, const std::vector<glm::ivec3>& sourcePositions,
                         const PositionSet* excludedPositions, std::vector<RedstoneSource>& sources) {
    sources.reserve(sources.size() + sourcePositions.size());
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
}

void appendPoweredRepeaterSources(const World& world, const std::vector<glm::ivec3>& repeaterPositions,
                                  std::vector<RedstoneSource>& sources) {
    sources.reserve(sources.size() + repeaterPositions.size());
    for (const glm::ivec3& position : repeaterPositions) {
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isRepeaterState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, repeaterOutputDirection(stateId), true,
                           logicUnitWireFacing(stateId, "Repeater")});
    }
}

void appendPoweredObserverSources(const World& world, const std::vector<glm::ivec3>& observerPositions,
                                  std::vector<RedstoneSource>& sources) {
    sources.reserve(sources.size() + observerPositions.size());
    for (const glm::ivec3& position : observerPositions) {
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isObserverState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, observerOutputDirection(stateId), true});
    }
}

void appendPoweredComparatorStateSources(const World& world, const std::vector<glm::ivec3>& comparatorPositions,
                                         std::vector<RedstoneSource>& sources) {
    sources.reserve(sources.size() + comparatorPositions.size());
    for (const glm::ivec3& position : comparatorPositions) {
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isComparatorState(stateId) || !isPoweredPropertyTrue(stateId)) {
            continue;
        }
        sources.push_back({position, kMaxRedstonePower, comparatorOutputDirection(stateId), true,
                           logicUnitWireFacing(stateId, "Comparator")});
    }
}

void appendComparatorSources(const World& world, const std::vector<ComparatorEvaluation>& evaluations,
                             std::vector<RedstoneSource>& sources) {
    sources.reserve(sources.size() + evaluations.size());
    for (const ComparatorEvaluation& evaluation : evaluations) {
        if (evaluation.outputPower == 0) {
            continue;
        }
        const BlockStateId stateId =
            world.getBlockState(evaluation.position.x, evaluation.position.y, evaluation.position.z);
        if (!isComparatorState(stateId)) {
            continue;
        }
        sources.push_back({evaluation.position, evaluation.outputPower, comparatorOutputDirection(stateId), true,
                           logicUnitWireFacing(stateId, "Comparator")});
    }
}

void setBestWirePower(WirePowerMap& wirePowers, std::priority_queue<PowerNode>& frontier, const WireNode& wire,
                      const uint8_t power) {
    if (power == 0) {
        return;
    }
    auto [it, inserted] = wirePowers.emplace(wire, power);
    if (!inserted && it->second >= power) {
        return;
    }
    it->second = power;
    frontier.push({wire, power});
}

void seedTrackedWirePower(WirePowerMap& wirePowers, std::priority_queue<PowerNode>& frontier, const WireNodeSet& wires,
                          const glm::ivec3& position, const uint8_t power,
                          const uint16_t targetWireFacing = PropIndices::INVALID) {
    for (const WireNode& wire : wires) {
        if (wire.position == position) {
            if (targetWireFacing != PropIndices::INVALID && wire.facing != targetWireFacing) {
                continue;
            }
            setBestWirePower(wirePowers, frontier, wire, power);
        }
    }
}

void seedWiresPoweredByConductor(const World& world, WirePowerMap& wirePowers, std::priority_queue<PowerNode>& frontier,
                                 const WireNodeSet& wires, const glm::ivec3& conductor,
                                 const glm::ivec3& signalPosition, const uint8_t power) {
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

void seedSourcePowerToward(const World& world, WirePowerMap& wirePowers, std::priority_queue<PowerNode>& frontier,
                           const WireNodeSet& wires, const RedstoneSource& source, const glm::ivec3& target) {
    seedTrackedWirePower(wirePowers, frontier, wires, target, source.power, source.targetWireFacing);
    const BlockStateId sourceState = world.getBlockState(source.position.x, source.position.y, source.position.z);
    const bool stronglyPowersTarget = source.directional ||
                                      (isTorchState(sourceState) && target == source.position + glm::ivec3(0, 1, 0)) ||
                                      sourceCanPowerConductiveBlocks(sourceState);
    if (stronglyPowersTarget) {
        seedWiresPoweredByConductor(world, wirePowers, frontier, wires, target, source.position, source.power);
    }
}

WirePowerMap propagateWirePower(const World& world, const WireNodeSet& wires,
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
            seedSourcePowerToward(world, wirePowers, frontier, wires, source, source.position + source.outputDirection);
        } else {
            for (const glm::ivec3& direction : kDirections) {
                seedSourcePowerToward(world, wirePowers, frontier, wires, source, source.position + direction);
            }
        }
    }

    while (!frontier.empty()) {
        const PowerNode node = frontier.top();
        frontier.pop();

        const auto currentIt = wirePowers.find(node.wire);
        if (currentIt == wirePowers.end() || currentIt->second != node.power) {
            continue;
        }
        if (node.power == 0) {
            continue;
        }

        const uint8_t nextPower = static_cast<uint8_t>(node.power - 1);
        forEachWireNeighbor(world, node.wire, [&](const WireNode& neighbor) {
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
    failRedstoneSystem("Redstone torch state contains an unknown facing value");
}

uint8_t computedWirePowerForNode(const WirePowerMap& wirePowers, const WireNode& wire) {
    const auto it = wirePowers.find(wire);
    return it == wirePowers.end() ? 0 : it->second;
}

uint8_t storedWireNodePower(const World& world, const WireNode& wire) {
    const BlockStateId stateId = world.getBlockState(wire.position.x, wire.position.y, wire.position.z);
    if (isWireState(stateId) && isMatchingWireNodeState(stateId, wire)) {
        return redstonePowerFromState(stateId);
    }
    if (isWireContainerState(stateId)) {
        const WirePart* part = findWireContainerPart(world, wire);
        if (part != nullptr) {
            return part->power;
        }
    }
    failRedstoneSystem("Redstone wire node power requires an existing wire node");
}

uint8_t wirePowerAtPosition(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& position) {
    uint8_t power = 0;
    forEachWireNodeAt(world, position, [&](const WireNode& wire) {
        const auto computedIt = wirePowers.find(wire);
        if (computedIt != wirePowers.end()) {
            power = std::max(power, computedIt->second);
            return;
        }
        power = std::max(power, wirePowers.wasEvaluated(wire) ? uint8_t{0} : storedWireNodePower(world, wire));
    });
    return power;
}

uint8_t wirePowerAtPositionForFacing(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& position,
                                     const uint16_t wireFacing) {
    uint8_t power = 0;
    forEachWireNodeAt(world, position, [&](const WireNode& wire) {
        if (wire.facing != wireFacing) {
            return;
        }
        const auto computedIt = wirePowers.find(wire);
        if (computedIt != wirePowers.end()) {
            power = std::max(power, computedIt->second);
            return;
        }
        power = std::max(power, wirePowers.wasEvaluated(wire) ? uint8_t{0} : storedWireNodePower(world, wire));
    });
    return power;
}

uint8_t sourceOutputPowerToward(const World& world, const glm::ivec3& sourcePosition,
                                const glm::ivec3& targetPosition) {
    const BlockStateId sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
    if (isRepeaterState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + repeaterOutputDirection(sourceState) ? kMaxRedstonePower : 0;
    }
    if (isObserverState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + observerOutputDirection(sourceState) ? kMaxRedstonePower : 0;
    }
    if (isComparatorState(sourceState)) {
        if (!isPoweredPropertyTrue(sourceState)) {
            return 0;
        }
        return targetPosition == sourcePosition + comparatorOutputDirection(sourceState) ? kMaxRedstonePower : 0;
    }
    if (isTorchState(sourceState) && targetPosition == attachedBlockForTorch(sourceState, sourcePosition)) {
        return 0;
    }
    return sourceOutputPower(sourceState);
}

uint8_t directSignalPowerToward(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& signalPosition,
                                const glm::ivec3& targetPosition) {
    const BlockStateId signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState) || isWireContainerState(signalState)) {
        return wirePowerAtPosition(world, wirePowers, signalPosition);
    }
    return sourceOutputPowerToward(world, signalPosition, targetPosition);
}

uint8_t directSignalPowerTowardWireFacing(const World& world, const WirePowerMap& wirePowers,
                                          const glm::ivec3& signalPosition, const glm::ivec3& targetPosition,
                                          const uint16_t targetWireFacing) {
    const BlockStateId signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState) || isWireContainerState(signalState)) {
        return wirePowerAtPositionForFacing(world, wirePowers, signalPosition, targetWireFacing);
    }
    return sourceOutputPowerToward(world, signalPosition, targetPosition);
}

uint8_t sourceStrongPowerTowardConductor(const World& world, const glm::ivec3& sourcePosition,
                                         const glm::ivec3& conductorPosition) {
    const BlockStateId sourceState = world.getBlockState(sourcePosition.x, sourcePosition.y, sourcePosition.z);
    if (isWireState(sourceState) || isWireContainerState(sourceState) || isConductiveState(sourceState)) {
        return 0;
    }
    if (isTorchState(sourceState)) {
        if (conductorPosition != sourcePosition + glm::ivec3(0, 1, 0)) {
            return 0;
        }
        return sourceOutputPower(sourceState);
    }
    if (isRepeaterState(sourceState) || isObserverState(sourceState) || isComparatorState(sourceState)) {
        return sourceOutputPowerToward(world, sourcePosition, conductorPosition);
    }
    if (!sourceCanPowerConductiveBlocks(sourceState)) {
        return 0;
    }
    return sourceOutputPowerToward(world, sourcePosition, conductorPosition);
}

uint8_t wirePowerTowardConductor(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& wirePosition,
                                 const glm::ivec3& conductorPosition) {
    const BlockStateId stateId = world.getBlockState(wirePosition.x, wirePosition.y, wirePosition.z);
    if (isWireState(stateId) && !wirePowersConductorToward(world, wirePosition, conductorPosition)) {
        return 0;
    }
    if (!isWireState(stateId) && !isWireContainerState(stateId)) {
        return 0;
    }
    return wirePowerAtPosition(world, wirePowers, wirePosition);
}

uint8_t conductiveBlockInputPowerToward(const World& world, const WirePowerMap& wirePowers,
                                        const glm::ivec3& signalPosition, const glm::ivec3& conductorPosition) {
    const BlockStateId signalState = world.getBlockState(signalPosition.x, signalPosition.y, signalPosition.z);
    if (isWireState(signalState) || isWireContainerState(signalState)) {
        return wirePowerTowardConductor(world, wirePowers, signalPosition, conductorPosition);
    }

    return sourceStrongPowerTowardConductor(world, signalPosition, conductorPosition);
}

uint8_t conductedSignalPowerToward(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& conductor,
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
        conductedPower =
            std::max(conductedPower, conductiveBlockInputPowerToward(world, wirePowers, signalPosition, conductor));
    }
    return conductedPower;
}

uint8_t conductiveBlockPowerAt(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& conductor,
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

uint8_t receivedPowerAt(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& position,
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
            neighborPower = std::max(neighborPower, conductedSignalPowerToward(world, wirePowers, neighbor, position));
        }
        receivedPower = std::max(receivedPower, neighborPower);
    }
    return receivedPower;
}

bool shouldTorchBeLit(const World& world, const WirePowerMap& inputWirePowers, const glm::ivec3& torchPosition,
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

void compactTorchRuntimeState(RedstoneRuntimeState& runtime, const glm::ivec3& position, const uint64_t redstoneTick) {
    const auto it = runtime.torches.find(position);
    if (it == runtime.torches.end()) {
        return;
    }

    pruneTorchTurnOffHistory(it->second, redstoneTick);
    if (!it->second.burnedOut && it->second.turnOffTicks.empty()) {
        runtime.torches.erase(it);
    }
}

size_t applyTorchStates(World& world, const std::vector<glm::ivec3>& torches, const WireNodeSet& wires,
                        const std::vector<glm::ivec3>& sourcePositions,
                        const std::vector<glm::ivec3>& repeaterPositions,
                        const std::vector<glm::ivec3>& observerPositions,
                        const std::vector<glm::ivec3>& comparatorPositions, const uint64_t redstoneTick) {
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
    PositionSet excludedSources;
    excludedSources.reserve(1);
    std::vector<RedstoneSource> inputSources;
    inputSources.reserve(sourcePositions.size() + repeaterPositions.size() + observerPositions.size() +
                         comparatorPositions.size());
    for (const glm::ivec3& position : orderedTorches) {
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isTorchState(currentState)) {
            continue;
        }

        excludedSources.clear();
        excludedSources.insert(position);
        inputSources.clear();
        appendActiveSources(world, sourcePositions, &excludedSources, inputSources);
        appendPoweredRepeaterSources(world, repeaterPositions, inputSources);
        appendPoweredObserverSources(world, observerPositions, inputSources);
        appendPoweredComparatorStateSources(world, comparatorPositions, inputSources);
        const WirePowerMap inputWirePowers = propagateWirePower(world, wires, inputSources);

        bool shouldBeLit = shouldTorchBeLit(world, inputWirePowers, position, currentState);
        auto& runtime = world.redstoneRuntimeState();
        auto runtimeIt = runtime.torches.find(position);
        if (runtimeIt != runtime.torches.end() && runtimeIt->second.burnedOut &&
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
                world.redstoneScheduledUpdateQueue().reschedule(torchRuntime.cooldownEndsAtTick, position,
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

uint8_t repeaterInputPower(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& repeaterPosition,
                           const BlockStateId repeaterState) {
    const glm::ivec3 inputPosition = repeaterPosition - repeaterOutputDirection(repeaterState);
    const uint16_t targetWireFacing = logicUnitWireFacing(repeaterState, "Repeater");
    uint8_t inputPower =
        directSignalPowerTowardWireFacing(world, wirePowers, inputPosition, repeaterPosition, targetWireFacing);
    if (isConductiveBlockAt(world, inputPosition)) {
        inputPower =
            std::max(inputPower, conductedSignalPowerToward(world, wirePowers, inputPosition, repeaterPosition));
    }
    return inputPower;
}

uint8_t signalPowerFromNeighbor(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& signalPosition,
                                const glm::ivec3& targetPosition) {
    uint8_t signalPower = directSignalPowerToward(world, wirePowers, signalPosition, targetPosition);
    if (isConductiveBlockAt(world, signalPosition)) {
        signalPower =
            std::max(signalPower, conductedSignalPowerToward(world, wirePowers, signalPosition, targetPosition));
    }
    return signalPower;
}

uint8_t signalPowerFromNeighborTowardWireFacing(const World& world, const WirePowerMap& wirePowers,
                                                const glm::ivec3& signalPosition, const glm::ivec3& targetPosition,
                                                const uint16_t targetWireFacing) {
    uint8_t signalPower =
        directSignalPowerTowardWireFacing(world, wirePowers, signalPosition, targetPosition, targetWireFacing);
    if (isConductiveBlockAt(world, signalPosition)) {
        signalPower =
            std::max(signalPower, conductedSignalPowerToward(world, wirePowers, signalPosition, targetPosition));
    }
    return signalPower;
}

bool repeaterLockInputActive(const World& world, const glm::ivec3& repeaterPosition, const BlockStateId repeaterState) {
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

uint8_t comparatorOutputPower(const World& world, const GameplayRegistry* registry, const WirePowerMap& inputWirePowers,
                              const glm::ivec3& comparatorPosition, const BlockStateId comparatorState) {
    const glm::ivec3 outputDirection = comparatorOutputDirection(comparatorState);
    const uint16_t targetWireFacing = logicUnitWireFacing(comparatorState, "Comparator");
    const glm::ivec3 rearInputPosition = comparatorPosition - outputDirection;
    const uint8_t containerPower = containerSignalPowerAt(world, registry, rearInputPosition);
    const uint8_t rearPower = containerPower > 0
                                  ? containerPower
                                  : signalPowerFromNeighborTowardWireFacing(world, inputWirePowers, rearInputPosition,
                                                                            comparatorPosition, targetWireFacing);

    uint8_t sidePower = 0;
    for (const glm::ivec3& sideDirection : comparatorSideDirections(comparatorState)) {
        sidePower = std::max(sidePower, signalPowerFromNeighborTowardWireFacing(world, inputWirePowers,
                                                                                comparatorPosition + sideDirection,
                                                                                comparatorPosition, targetWireFacing));
    }

    if (comparatorUsesSubtractMode(comparatorState)) {
        return rearPower > sidePower ? static_cast<uint8_t>(rearPower - sidePower) : 0;
    }
    return rearPower >= sidePower ? rearPower : 0;
}

void evaluateComparators(const World& world, const GameplayRegistry* registry,
                         const std::vector<glm::ivec3>& comparatorPositions, const WirePowerMap& inputWirePowers,
                         std::vector<ComparatorEvaluation>& evaluations) {
    evaluations.reserve(evaluations.size() + comparatorPositions.size());
    for (const glm::ivec3& position : comparatorPositions) {
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isComparatorState(stateId)) {
            continue;
        }
        evaluations.push_back({position, comparatorOutputPower(world, registry, inputWirePowers, position, stateId)});
    }
}

size_t applyComparatorStates(World& world, const std::vector<ComparatorEvaluation>& evaluations) {
    size_t changed = 0;
    for (const ComparatorEvaluation& evaluation : evaluations) {
        const BlockStateId currentState =
            world.getBlockState(evaluation.position.x, evaluation.position.y, evaluation.position.z);
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

void scheduleRepeaterEvaluationUpdates(World& world, const uint64_t redstoneTick,
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

        world.redstoneScheduledUpdateQueue().schedule(redstoneTick + repeaterDelayTicks(stateId), position,
                                                      RedstoneScheduledAction::EvaluateRepeater);
    }
}

void scheduleObserverPulseUpdates(World& world, const uint64_t redstoneTick,
                                  const std::vector<glm::ivec3>& changedPositions) {
    for (const glm::ivec3& changedPosition : changedPositions) {
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 observerPosition = changedPosition - direction;
            const BlockStateId observerState =
                world.getBlockState(observerPosition.x, observerPosition.y, observerPosition.z);
            if (!isObserverState(observerState)) {
                continue;
            }
            if (observerPosition + observerFacingDirection(observerState) != changedPosition) {
                continue;
            }

            world.redstoneScheduledUpdateQueue().schedule(redstoneTick + kObserverPulseDelayTicks, observerPosition,
                                                          RedstoneScheduledAction::StartObserverPulse);
        }
    }
}

void scheduleButtonReleaseUpdates(World& world, const uint64_t redstoneTick,
                                  const std::vector<glm::ivec3>& sourcePositions) {
    for (const glm::ivec3& position : sourcePositions) {
        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isButtonState(stateId)) {
            continue;
        }
        if (!hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered")) {
            continue;
        }

        world.redstoneScheduledUpdateQueue().schedule(redstoneTick + buttonPulseTicks(stateId), position,
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
    world.redstoneScheduledUpdateQueue().schedule(redstoneTick + kObserverPulseDurationTicks, position,
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
    const BlockStateId updatedState =
        withLit(currentState, shouldTorchBeLit(world, wirePowers, position, currentState));
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

size_t applyWirePowers(World& world, const std::vector<WireNode>& wires, const WirePowerMap& wirePowers) {
    size_t changed = 0;
    PositionSet changedContainers;
    for (const WireNode& wire : wires) {
        const glm::ivec3& position = wire.position;
        const BlockStateId currentState = world.getBlockState(position.x, position.y, position.z);
        const uint8_t power = computedWirePowerForNode(wirePowers, wire);
        if (isWireState(currentState)) {
            if (!isMatchingWireNodeState(currentState, wire)) {
                continue;
            }

            const BlockStateId updatedState = withRedstonePower(currentState, power);
            if (updatedState != currentState) {
                world.setBlockState(position.x, position.y, position.z, updatedState);
                ++changed;
            }
            continue;
        }

        if (!isWireContainerState(currentState)) {
            continue;
        }
        WireContainerParts* parts = world.wireContainerParts().findMutable(position);
        if (parts == nullptr) {
            continue;
        }
        WirePart* part = parts->findMutable(wire.channelId, wire.facing);
        if (part == nullptr) {
            continue;
        }
        if (part->power != power) {
            part->power = power;
            changedContainers.insert(position);
            ++changed;
        }
    }
    for (const glm::ivec3& position : changedContainers) {
        world.notifyWireContainerPartsChanged(position);
    }
    return changed;
}

size_t applyPistonStates(World& world, GameplayRegistry* registry, const std::vector<glm::ivec3>& pistons,
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

size_t applyRedstoneControlledStates(World& world, const std::vector<glm::ivec3>& positions,
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
                const bool powered = BlockStateRegistry::getPropertyIndex(updatedState, PropIndices::POWERED) ==
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

void emitEdgeTriggeredDeviceEvent(GameplayRegistry& registry, const BlockStateId stateId, const glm::ivec3& position,
                                  const uint64_t redstoneTick) {
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (def.redstoneBehavior != "note_block" && def.redstoneBehavior != "dispenser" &&
        def.redstoneBehavior != "dropper") {
        failRedstoneSystem("Unsupported redstone edge-triggered behavior: " + def.redstoneBehavior);
    }
    ensureRedstoneDeviceActivationEventBus(registry).push({position, blockId, stateId, redstoneTick});
}

size_t applyEdgeTriggeredDeviceStates(World& world, GameplayRegistry* registry,
                                      const std::vector<glm::ivec3>& positions, const WirePowerMap& wirePowers,
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

size_t processWorldWithContext(World& world, const GameplayRegistry* readRegistry, GameplayRegistry* mutableRegistry,
                               const uint64_t redstoneTick, const size_t budget) {
    if (budget == 0) {
        return 0;
    }

    world.setLastProcessedRedstoneTick(redstoneTick);

    RedstoneProcessBuffers& buffers = t_redstoneBuffers;
    size_t changed = 0;
    std::vector<RedstoneScheduledUpdate>& scheduledUpdates = buffers.scheduledUpdates;
    scheduledUpdates.clear();
    scheduledUpdates.reserve(std::min(budget, world.redstoneScheduledUpdateQueue().size()));
    world.redstoneScheduledUpdateQueue().drainDue(redstoneTick, scheduledUpdates, budget);
    changed += applyScheduledUpdates(world, scheduledUpdates);

    std::vector<glm::ivec3>& changedPositions = buffers.changedPositions;
    changedPositions.clear();
    changedPositions.reserve(budget);
    world.redstoneChangedBlockQueue().drain(changedPositions, budget);
    scheduleObserverPulseUpdates(world, redstoneTick, changedPositions);

    if (world.redstoneUpdateQueue().size() == 0) {
        return changed;
    }

    std::vector<glm::ivec3>& dirtyPositions = buffers.dirtyPositions;
    dirtyPositions.clear();
    dirtyPositions.reserve(budget);
    world.redstoneUpdateQueue().drain(dirtyPositions, budget);

    RedstoneWorkSet& workSet = buffers.workSet;
    collectRedstoneWorkSet(world, dirtyPositions, workSet);

    changed += applyTorchStates(world, workSet.torches, workSet.wireSet, workSet.sourcePositions, workSet.repeaters,
                                workSet.observers, workSet.comparators, redstoneTick);
    scheduleButtonReleaseUpdates(world, redstoneTick, workSet.sourcePositions);

    std::vector<RedstoneSource>& outputSources = buffers.outputSources;
    outputSources.clear();
    appendActiveSources(world, workSet.sourcePositions, nullptr, outputSources);
    appendPoweredRepeaterSources(world, workSet.repeaters, outputSources);
    appendPoweredObserverSources(world, workSet.observers, outputSources);
    std::vector<ComparatorEvaluation>& comparatorEvaluations = buffers.comparatorEvaluations;
    comparatorEvaluations.clear();
    if (!workSet.comparators.empty()) {
        const WirePowerMap inputWirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
        evaluateComparators(world, readRegistry, workSet.comparators, inputWirePowers, comparatorEvaluations);
        appendComparatorSources(world, comparatorEvaluations, outputSources);
    }
    const WirePowerMap wirePowers = propagateWirePower(world, workSet.wireSet, outputSources);
    changed += applyWirePowers(world, workSet.wires, wirePowers);
    changed += applyComparatorStates(world, comparatorEvaluations);
    changed += applyPistonStates(world, mutableRegistry, workSet.pistons, wirePowers);
    changed += applyRepeaterLockStates(world, workSet.repeaters);
    scheduleRepeaterEvaluationUpdates(world, redstoneTick, workSet.repeaters, wirePowers);
    changed += applyRedstoneControlledStates(world, workSet.redstoneControlledBlocks, wirePowers);
    changed +=
        applyEdgeTriggeredDeviceStates(world, mutableRegistry, workSet.edgeTriggeredDevices, wirePowers, redstoneTick);
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
    if (!RedstoneTick::runsOnGameTick(ctx.tickIndex)) {
        return;
    }

    processWorldWithContext(*ctx.services.world, &ctx.registry, &ctx.registry,
                            RedstoneTick::fromGameTick(ctx.tickIndex), 4096);
}

size_t RedstoneSystem::processWorld(World& world, const uint64_t redstoneTick, const size_t budget) {
    return processWorldWithContext(world, nullptr, nullptr, redstoneTick, budget);
}

size_t RedstoneSystem::processWorld(World& world, const uint64_t redstoneTick, const GameplayRegistry& registry,
                                    const size_t budget) {
    return processWorldWithContext(world, &registry, nullptr, redstoneTick, budget);
}

size_t RedstoneSystem::processWorld(World& world, const uint64_t redstoneTick, GameplayRegistry& registry,
                                    const size_t budget) {
    return processWorldWithContext(world, &registry, &registry, redstoneTick, budget);
}

} // namespace ecs
