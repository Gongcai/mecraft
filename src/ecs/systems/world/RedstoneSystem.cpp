#include "RedstoneSystem.h"

#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/RedstoneUpdateQueue.h"

#include <algorithm>
#include <array>
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
    std::vector<glm::ivec3> lamps;
    std::vector<glm::ivec3> torches;
    std::vector<glm::ivec3> repeaters;
    std::vector<glm::ivec3> sourcePositions;
    PositionSet wireSet;
    PositionSet lampSet;
    PositionSet torchSet;
    PositionSet repeaterSet;
    PositionSet sourceSet;
};

struct PowerNode {
    glm::ivec3 position;
    uint8_t power = 0;

    bool operator<(const PowerNode& other) const {
        return power < other.power;
    }
};

struct TorchStateUpdate {
    glm::ivec3 position;
    StateID state = BlockIds::AIR;
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

uint8_t wirePowerFromState(const StateID stateId) {
    const uint16_t value = getRequiredProperty(stateId, PropIndices::POWER, "power");
    const auto values = powerPropertyValues();
    for (uint8_t power = 0; power < values.size(); ++power) {
        if (value == values[power]) {
            return power;
        }
    }
    throw std::runtime_error("Redstone wire state contains an unknown power value");
}

StateID withWirePower(const StateID stateId, const uint8_t power) {
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

    throw std::runtime_error("Unsupported redstone power source behavior: " + def.redstoneBehavior);
}

bool isPotentialSourceState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstonePowerSource;
}

bool isLampState(const StateID stateId) {
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.respondsToRedstone && def.redstoneBehavior == "lamp";
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
    throw std::runtime_error("Repeater state contains a non-horizontal facing value");
}

glm::ivec3 repeaterOutputDirection(const StateID stateId) {
    return horizontalDirectionFromFacing(getRequiredProperty(stateId, PropIndices::FACING, "facing"));
}

bool isPoweredPropertyTrue(const StateID stateId) {
    return hasBooleanPropertyValue(stateId, PropIndices::POWERED, PropIndices::POWERED_TRUE, "powered");
}

void addLampIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isLampState(stateId)) {
        return;
    }
    if (workSet.lampSet.insert(position).second) {
        workSet.lamps.push_back(position);
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

void addSourceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isPotentialSourceState(stateId)) {
        return;
    }
    if (workSet.sourceSet.insert(position).second) {
        workSet.sourcePositions.push_back(position);
    }
}

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

        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = position + direction;
            const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
            if (isWireState(neighborState)) {
                if (workSet.wireSet.insert(neighbor).second) {
                    workSet.wires.push_back(neighbor);
                    frontier.push(neighbor);
                }
                continue;
            }

            if (isLampState(neighborState) && workSet.lampSet.insert(neighbor).second) {
                workSet.lamps.push_back(neighbor);
            }
            if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            }
            if (isRepeaterState(neighborState)) {
                addRepeaterIfPresent(world, neighbor, workSet);
            }
            if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
    }
}

void collectPosition(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (isWireState(stateId)) {
        collectWireComponent(world, position, workSet);
        return;
    }

    if (isLampState(stateId) && workSet.lampSet.insert(position).second) {
        workSet.lamps.push_back(position);
    }

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
            } else if (isLampState(neighborState) && workSet.lampSet.insert(neighbor).second) {
                workSet.lamps.push_back(neighbor);
            } else if (isTorchState(neighborState)) {
                addTorchIfPresent(world, neighbor, workSet);
            } else if (isPotentialSourceState(neighborState)) {
                addSourceIfPresent(world, neighbor, workSet);
            }
        }
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
        } else if (isLampState(neighborState) && workSet.lampSet.insert(neighbor).second) {
            workSet.lamps.push_back(neighbor);
        } else if (isTorchState(neighborState)) {
            addTorchIfPresent(world, neighbor, workSet);
        } else if (isRepeaterState(neighborState)) {
            addRepeaterIfPresent(world, neighbor, workSet);
        }
    }
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

WirePowerMap propagateWirePower(const PositionSet& wires, const std::vector<RedstoneSource>& sources) {
    WirePowerMap wirePowers;
    std::priority_queue<PowerNode> frontier;

    for (const RedstoneSource& source : sources) {
        if (source.directional) {
            const glm::ivec3 neighbor = source.position + source.outputDirection;
            if (wires.find(neighbor) != wires.end()) {
                setBestWirePower(wirePowers, frontier, neighbor, source.power);
            }
        } else {
            for (const glm::ivec3& direction : kDirections) {
                const glm::ivec3 neighbor = source.position + direction;
                if (wires.find(neighbor) != wires.end()) {
                    setBestWirePower(wirePowers, frontier, neighbor, source.power);
                }
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
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = node.position + direction;
            if (wires.find(neighbor) != wires.end()) {
                setBestWirePower(wirePowers, frontier, neighbor, nextPower);
            }
        }
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
    return sourceOutputPower(sourceState);
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
        uint8_t neighborPower = 0;
        if (isWireState(neighborState)) {
            const auto computedIt = wirePowers.find(neighbor);
            neighborPower = computedIt != wirePowers.end()
                ? computedIt->second
                : wirePowerFromState(neighborState);
        } else {
            neighborPower = sourceOutputPowerToward(world, neighbor, position);
        }
        receivedPower = std::max(receivedPower, neighborPower);
    }
    return receivedPower;
}

bool isConductiveBlockAt(const World& world, const glm::ivec3& position) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (stateId == BlockIds::AIR) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    return def.isRedstoneConductor;
}

bool shouldTorchBeLit(const World& world,
                      const WirePowerMap& inputWirePowers,
                      const glm::ivec3& torchPosition,
                      const StateID torchState) {
    const glm::ivec3 attachedBlock = attachedBlockForTorch(torchState, torchPosition);
    if (!isConductiveBlockAt(world, attachedBlock)) {
        return true;
    }
    return receivedPowerAt(world, inputWirePowers, attachedBlock, &torchPosition) == 0;
}

size_t applyTorchStates(World& world,
                        const std::vector<glm::ivec3>& torches,
                        const PositionSet& wires,
                        const std::vector<glm::ivec3>& sourcePositions) {
    std::vector<TorchStateUpdate> updates;
    updates.reserve(torches.size());
    for (const glm::ivec3& position : torches) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isTorchState(currentState)) {
            continue;
        }

        PositionSet excludedSources;
        excludedSources.insert(position);
        const std::vector<RedstoneSource> inputSources =
            collectActiveSources(world, sourcePositions, &excludedSources);
        const WirePowerMap inputWirePowers = propagateWirePower(wires, inputSources);

        const StateID updatedState = withLit(
            currentState,
            shouldTorchBeLit(world, inputWirePowers, position, currentState));
        if (updatedState != currentState) {
            updates.push_back({position, updatedState});
        }
    }

    size_t changed = 0;
    for (const TorchStateUpdate& update : updates) {
        const glm::ivec3& position = update.position;
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (currentState == update.state || !isTorchState(currentState)) {
            continue;
        }

        world.setBlockState(position.x, position.y, position.z, update.state);
        ++changed;
    }
    return changed;
}

uint8_t repeaterInputPower(const World& world,
                           const WirePowerMap& wirePowers,
                           const glm::ivec3& repeaterPosition,
                           const StateID repeaterState) {
    const glm::ivec3 inputPosition = repeaterPosition - repeaterOutputDirection(repeaterState);
    const StateID inputState = world.getBlockState(inputPosition.x, inputPosition.y, inputPosition.z);
    if (isWireState(inputState)) {
        const auto computedIt = wirePowers.find(inputPosition);
        return computedIt != wirePowers.end()
            ? computedIt->second
            : wirePowerFromState(inputState);
    }
    return sourceOutputPowerToward(world, inputPosition, repeaterPosition);
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

        const StateID updatedState = withWirePower(currentState, computedWirePowerAt(wirePowers, position));
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

size_t applyLampStates(World& world, const std::vector<glm::ivec3>& lamps, const WirePowerMap& wirePowers) {
    size_t changed = 0;
    for (const glm::ivec3& position : lamps) {
        const StateID currentState = world.getBlockState(position.x, position.y, position.z);
        if (!isLampState(currentState)) {
            continue;
        }

        const bool shouldBeLit = receivedPowerAt(world, wirePowers, position) > 0;
        const StateID updatedState = withLit(currentState, shouldBeLit);
        if (updatedState != currentState) {
            world.setBlockState(position.x, position.y, position.z, updatedState);
            ++changed;
        }
    }
    return changed;
}

} // namespace

void RedstoneSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if ((ctx.tickIndex % 2u) != 0u) {
        return;
    }

    processWorld(*ctx.services.world, ctx.tickIndex / 2u);
}

size_t RedstoneSystem::processWorld(World& world, const uint64_t redstoneTick, const size_t budget) {
    if (budget == 0) {
        return 0;
    }

    size_t changed = 0;
    std::vector<RedstoneScheduledUpdate> scheduledUpdates;
    scheduledUpdates.reserve(std::min(budget, world.redstoneScheduledUpdateQueue().size()));
    world.redstoneScheduledUpdateQueue().drainDue(redstoneTick, scheduledUpdates, budget);
    changed += applyScheduledUpdates(world, scheduledUpdates);

    if (world.redstoneUpdateQueue().size() == 0) {
        return changed;
    }

    std::vector<glm::ivec3> dirtyPositions;
    dirtyPositions.reserve(budget);
    world.redstoneUpdateQueue().drain(dirtyPositions, budget);

    const RedstoneWorkSet workSet = collectRedstoneWorkSet(world, dirtyPositions);

    changed += applyTorchStates(world, workSet.torches, workSet.wireSet, workSet.sourcePositions);
    scheduleButtonReleaseUpdates(world, redstoneTick, workSet.sourcePositions);

    std::vector<RedstoneSource> outputSources =
        collectActiveSources(world, workSet.sourcePositions, nullptr);
    appendSources(outputSources, collectPoweredRepeaterSources(world, workSet.repeaters));
    const WirePowerMap wirePowers = propagateWirePower(workSet.wireSet, outputSources);
    changed += applyWirePowers(world, workSet.wires, wirePowers);
    scheduleRepeaterEvaluationUpdates(world, redstoneTick, workSet.repeaters, wirePowers);
    changed += applyLampStates(world, workSet.lamps, wirePowers);
    return changed;
}

} // namespace ecs
