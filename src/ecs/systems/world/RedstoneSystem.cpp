#include "RedstoneSystem.h"

#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"

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
};

struct RedstoneWorkSet {
    std::vector<glm::ivec3> wires;
    std::vector<glm::ivec3> lamps;
    std::vector<RedstoneSource> sources;
    PositionSet wireSet;
    PositionSet lampSet;
    PositionSet sourceSet;
};

struct PowerNode {
    glm::ivec3 position;
    uint8_t power = 0;

    bool operator<(const PowerNode& other) const {
        return power < other.power;
    }
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

void addLampIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isLampState(stateId)) {
        return;
    }
    if (workSet.lampSet.insert(position).second) {
        workSet.lamps.push_back(position);
    }
}

void addPoweredSourceIfPresent(const World& world, const glm::ivec3& position, RedstoneWorkSet& workSet) {
    const StateID stateId = world.getBlockState(position.x, position.y, position.z);
    if (!isPotentialSourceState(stateId)) {
        return;
    }

    const uint8_t power = sourceOutputPower(stateId);
    if (power == 0 || !workSet.sourceSet.insert(position).second) {
        return;
    }
    workSet.sources.push_back({position, power});
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
            if (isPotentialSourceState(neighborState)) {
                addPoweredSourceIfPresent(world, neighbor, workSet);
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

    if (!isPotentialSourceState(stateId)) {
        return;
    }

    addPoweredSourceIfPresent(world, position, workSet);
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = position + direction;
        const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        if (isWireState(neighborState)) {
            collectWireComponent(world, neighbor, workSet);
        } else if (isLampState(neighborState) && workSet.lampSet.insert(neighbor).second) {
            workSet.lamps.push_back(neighbor);
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
        for (const glm::ivec3& direction : kDirections) {
            const glm::ivec3 neighbor = source.position + direction;
            if (wires.find(neighbor) != wires.end()) {
                setBestWirePower(wirePowers, frontier, neighbor, source.power);
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

uint8_t computedWirePowerAt(const WirePowerMap& wirePowers, const glm::ivec3& position) {
    const auto it = wirePowers.find(position);
    return it == wirePowers.end() ? 0 : it->second;
}

uint8_t receivedPowerAt(const World& world, const WirePowerMap& wirePowers, const glm::ivec3& position) {
    uint8_t receivedPower = 0;
    for (const glm::ivec3& direction : kDirections) {
        const glm::ivec3 neighbor = position + direction;
        const StateID neighborState = world.getBlockState(neighbor.x, neighbor.y, neighbor.z);
        uint8_t neighborPower = 0;
        if (isWireState(neighborState)) {
            const auto computedIt = wirePowers.find(neighbor);
            neighborPower = computedIt != wirePowers.end()
                ? computedIt->second
                : wirePowerFromState(neighborState);
        } else {
            neighborPower = sourceOutputPower(neighborState);
        }
        receivedPower = std::max(receivedPower, neighborPower);
    }
    return receivedPower;
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
    static_cast<void>(redstoneTick);
    if (budget == 0 || world.redstoneUpdateQueue().size() == 0) {
        return 0;
    }

    std::vector<glm::ivec3> dirtyPositions;
    dirtyPositions.reserve(budget);
    world.redstoneUpdateQueue().drain(dirtyPositions, budget);

    const RedstoneWorkSet workSet = collectRedstoneWorkSet(world, dirtyPositions);
    const WirePowerMap wirePowers = propagateWirePower(workSet.wireSet, workSet.sources);

    size_t changed = 0;
    changed += applyWirePowers(world, workSet.wires, wirePowers);
    changed += applyLampStates(world, workSet.lamps, wirePowers);
    return changed;
}

} // namespace ecs
