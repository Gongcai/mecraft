#include "FluidSystem.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include "FluidState.h"
#include "World.h"

namespace {

constexpr std::array<glm::ivec3, 7> kFluidUpdateOffsets = {{
    {0, 0, 0},
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1}
}};

constexpr std::array<glm::ivec3, 4> kHorizontalFluidOffsets = {{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 0, 1},
    {0, 0, -1}
}};

struct FlowDirections {
    uint8_t allowedMask = 0;
    bool foundHole = false;
    bool hasAnyPassable = false;

    [[nodiscard]] bool allows(const int directionIndex) const {
        return (allowedMask & (1u << directionIndex)) != 0u;
    }
};

struct SearchNode {
    glm::ivec3 pos{};
    uint8_t distance = 0;
};

bool isPositionLoaded(const World& world, const glm::ivec3& pos) {
    return world.isChunkLoadedForBlock(pos.x, pos.y, pos.z);
}

bool canFluidReplaceAt(const World& world, const glm::ivec3& pos, const FluidDesc& desc) {
    if (!isPositionLoaded(world, pos)) {
        return false;
    }
    return FluidState::canReplace(desc, world.getBlockState(pos.x, pos.y, pos.z));
}

bool hasSupportBelow(const World& world, const glm::ivec3& pos, const FluidDesc& desc) {
    const glm::ivec3 belowPos = pos + glm::ivec3(0, -1, 0);
    if (!isPositionLoaded(world, belowPos)) {
        return true;
    }
    return !FluidState::canReplace(desc, world.getBlockState(belowPos.x, belowPos.y, belowPos.z));
}

FluidKind resolveTargetFluidKind(const World& world, const glm::ivec3& pos, const BlockID currentId) {
    const DecodedFluid current = FluidState::decode(currentId);
    if (current.kind != FluidKind::None) {
        return current.kind;
    }

    const glm::ivec3 abovePos = pos + glm::ivec3(0, 1, 0);
    if (isPositionLoaded(world, abovePos)) {
        const FluidKind aboveKind = FluidState::decode(world.getBlockState(abovePos.x, abovePos.y, abovePos.z)).kind;
        if (aboveKind != FluidKind::None) {
            return aboveKind;
        }
    }

    for (const glm::ivec3& offset : kHorizontalFluidOffsets) {
        const glm::ivec3 neighborPos = pos + offset;
        if (!isPositionLoaded(world, neighborPos)) {
            continue;
        }
        const FluidKind neighborKind = FluidState::decode(
            world.getBlockState(neighborPos.x, neighborPos.y, neighborPos.z)).kind;
        if (neighborKind != FluidKind::None) {
            return neighborKind;
        }
    }

    return FluidKind::None;
}

FlowDirections computeFlowDirections(const World& world, const glm::ivec3& sourcePos, const FluidDesc& desc) {
    FlowDirections result;
    const int maxDistance = static_cast<int>(desc.slopeSearchDistance);
    if (maxDistance <= 0) {
        return result;
    }

    std::array<int, 4> holeDistances{};
    holeDistances.fill(std::numeric_limits<int>::max());

    for (int dirIndex = 0; dirIndex < static_cast<int>(kHorizontalFluidOffsets.size()); ++dirIndex) {
        const glm::ivec3 firstStep = sourcePos + kHorizontalFluidOffsets[static_cast<size_t>(dirIndex)];
        if (!canFluidReplaceAt(world, firstStep, desc)) {
            continue;
        }

        result.hasAnyPassable = true;
        result.allowedMask |= static_cast<uint8_t>(1u << dirIndex);

        if (!hasSupportBelow(world, firstStep, desc)) {
            holeDistances[dirIndex] = 1;
            result.foundHole = true;
            continue;
        }

        const int gridSize = maxDistance * 2 + 1;
        std::vector<uint8_t> visited(static_cast<size_t>(gridSize * gridSize), 0);
        auto toVisitedIndex = [&](const glm::ivec3& pos) -> size_t {
            const int dx = pos.x - sourcePos.x + maxDistance;
            const int dz = pos.z - sourcePos.z + maxDistance;
            return static_cast<size_t>(dx + dz * gridSize);
        };
        auto inSearchBounds = [&](const glm::ivec3& pos) -> bool {
            return std::abs(pos.x - sourcePos.x) <= maxDistance &&
                   std::abs(pos.z - sourcePos.z) <= maxDistance;
        };

        std::queue<SearchNode> queue;
        queue.push(SearchNode{firstStep, 1});
        visited[toVisitedIndex(firstStep)] = 1;

        while (!queue.empty()) {
            const SearchNode node = queue.front();
            queue.pop();

            if (node.distance >= desc.slopeSearchDistance) {
                continue;
            }

            for (const glm::ivec3& offset : kHorizontalFluidOffsets) {
                const glm::ivec3 nextPos = node.pos + offset;
                if (!inSearchBounds(nextPos) || !canFluidReplaceAt(world, nextPos, desc)) {
                    continue;
                }

                const size_t visitIndex = toVisitedIndex(nextPos);
                if (visited[visitIndex] != 0) {
                    continue;
                }
                visited[visitIndex] = 1;

                const uint8_t nextDistance = static_cast<uint8_t>(node.distance + 1);
                if (!hasSupportBelow(world, nextPos, desc)) {
                    holeDistances[dirIndex] = nextDistance;
                    result.foundHole = true;
                    while (!queue.empty()) {
                        queue.pop();
                    }
                    break;
                }

                queue.push(SearchNode{nextPos, nextDistance});
            }
        }
    }

    if (!result.foundHole) {
        return result;
    }

    int bestDistance = std::numeric_limits<int>::max();
    for (const int distance : holeDistances) {
        bestDistance = std::min(bestDistance, distance);
    }

    result.allowedMask = 0;
    for (int dirIndex = 0; dirIndex < static_cast<int>(holeDistances.size()); ++dirIndex) {
        if (holeDistances[dirIndex] == bestDistance) {
            result.allowedMask |= static_cast<uint8_t>(1u << dirIndex);
        }
    }
    return result;
}

int countHorizontalSourceNeighbors(const World& world,
                                   const glm::ivec3& pos,
                                   const FluidKind kind) {
    int sourceCount = 0;
    for (const glm::ivec3& offset : kHorizontalFluidOffsets) {
        const glm::ivec3 neighborPos = pos + offset;
        if (!isPositionLoaded(world, neighborPos)) {
            continue;
        }

        const DecodedFluid neighbor = FluidState::decode(
            world.getBlockState(neighborPos.x, neighborPos.y, neighborPos.z));
        if (neighbor.kind == kind && neighbor.isSource) {
            ++sourceCount;
        }
    }
    return sourceCount;
}

}

void FluidSystem::reset() {
    m_scheduledBlockTickQueue = {};
    m_scheduledBlockTickDue.clear();
    m_lastProcessedGameTick = 0;
}

void FluidSystem::onBlockChanged(const glm::ivec3 pos) {
    scheduleNeighborsForFluidUpdate(pos, m_lastProcessedGameTick + resolveNeighborhoodTickDelay(pos));
}

void FluidSystem::scheduleBlockTick(const glm::ivec3 pos, const uint64_t dueTick) {
    if (!m_world.isChunkLoadedForBlock(pos.x, pos.y, pos.z)) {
        return;
    }

    const ScheduledBlockTickPos key{pos.x, pos.y, pos.z};
    const auto it = m_scheduledBlockTickDue.find(key);
    if (it != m_scheduledBlockTickDue.end() && it->second <= dueTick) {
        return;
    }

    m_scheduledBlockTickDue[key] = dueTick;
    m_scheduledBlockTickQueue.push(ScheduledBlockTick{dueTick, pos});
}

void FluidSystem::scheduleNeighborsForFluidUpdate(const glm::ivec3 pos, const uint64_t dueTick) {
    for (const glm::ivec3& offset : kFluidUpdateOffsets) {
        scheduleBlockTick(pos + offset, dueTick);
    }
}

void FluidSystem::processScheduledBlockTicks(const uint64_t currentTick, const uint32_t budget) {
    m_lastProcessedGameTick = currentTick;

    uint32_t processed = 0;
    while (!m_scheduledBlockTickQueue.empty() &&
           m_scheduledBlockTickQueue.top().dueTick <= currentTick &&
           processed < budget) {
        const ScheduledBlockTick scheduled = m_scheduledBlockTickQueue.top();
        m_scheduledBlockTickQueue.pop();

        const ScheduledBlockTickPos key{scheduled.pos.x, scheduled.pos.y, scheduled.pos.z};
        const auto it = m_scheduledBlockTickDue.find(key);
        if (it == m_scheduledBlockTickDue.end() || it->second != scheduled.dueTick) {
            continue;
        }
        m_scheduledBlockTickDue.erase(it);

        updateFluidCell(scheduled.pos);
        ++processed;
    }
}

size_t FluidSystem::ScheduledBlockTickPosHash::operator()(const ScheduledBlockTickPos& pos) const noexcept {
    size_t hash = static_cast<size_t>(std::hash<int>{}(pos.x));
    hash ^= static_cast<size_t>(std::hash<int>{}(pos.y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u));
    hash ^= static_cast<size_t>(std::hash<int>{}(pos.z) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u));
    return hash;
}

void FluidSystem::updateFluidCell(const glm::ivec3& pos) {
    if (!m_world.isChunkLoadedForBlock(pos.x, pos.y, pos.z)) {
        return;
    }

    const BlockID currentId = m_world.getBlockState(pos.x, pos.y, pos.z);
    const FluidKind targetKind = resolveTargetFluidKind(m_world, pos, currentId);
    if (targetKind == FluidKind::None) {
        if (currentId != BlockIds::AIR && FluidState::decode(currentId).kind == FluidKind::None) {
            return;
        }
    } else {
        const FluidDesc& desc = FluidRegistry::get(targetKind);
        if (!FluidState::canReplace(desc, currentId) && !FluidState::isFluidOf(currentId, targetKind)) {
            return;
        }
    }

    const StateID targetState = computeTargetFluidState(pos, currentId);
    if (targetState == currentId) {
        return;
    }

    m_world.setBlockState(pos.x, pos.y, pos.z, targetState);
}

StateID FluidSystem::computeTargetFluidState(const glm::ivec3& pos, const BlockID currentId) const {
    const FluidKind kind = resolveTargetFluidKind(m_world, pos, currentId);
    if (kind == FluidKind::None) {
        return FluidState::decode(currentId).kind == FluidKind::None ? currentId : BlockIds::AIR;
    }

    const FluidDesc& desc = FluidRegistry::get(kind);
    const DecodedFluid current = FluidState::decode(currentId);
    if (current.kind != FluidKind::None && current.kind != kind) {
        return currentId;
    }
    if (current.isSource) {
        return FluidState::encode(DecodedFluid{kind, 0, false, true});
    }
    if (current.kind == FluidKind::None && !FluidState::canReplace(desc, currentId)) {
        return currentId;
    }

    const glm::ivec3 belowPos = pos + glm::ivec3(0, -1, 0);
    const bool supportBelow = hasSupportBelow(m_world, pos, desc);
    const glm::ivec3 abovePos = pos + glm::ivec3(0, 1, 0);
    const BlockID aboveState = isPositionLoaded(m_world, abovePos)
        ? m_world.getBlockState(abovePos.x, abovePos.y, abovePos.z)
        : BlockIds::AIR;
    const DecodedFluid above = FluidState::decode(aboveState);

    if (above.kind == kind) {
        if (!supportBelow) {
            return FluidState::encode(DecodedFluid{kind, 0, true, false});
        }
        return FluidState::encode(DecodedFluid{kind, 1, false, false});
    }

    if (desc.canCreateInfiniteSource && !current.falling &&
        (!desc.requiresSupportForInfiniteSource || supportBelow) &&
        countHorizontalSourceNeighbors(m_world, pos, kind) >= desc.infiniteSourceNeighborCount) {
        return FluidState::encode(DecodedFluid{kind, 0, false, true});
    }

    uint8_t minNeighborLevel = static_cast<uint8_t>(desc.maxLevel + 1);
    bool foundHorizontalSource = false;
    for (int dirIndex = 0; dirIndex < static_cast<int>(kHorizontalFluidOffsets.size()); ++dirIndex) {
        const glm::ivec3 sourceOffset = kHorizontalFluidOffsets[static_cast<size_t>(dirIndex)];
        const glm::ivec3 neighborPos = pos - sourceOffset;
        if (!isPositionLoaded(m_world, neighborPos)) {
            continue;
        }

        const BlockID neighborState = m_world.getBlockState(neighborPos.x, neighborPos.y, neighborPos.z);
        const DecodedFluid neighbor = FluidState::decode(neighborState);
        if (neighbor.kind != kind || neighbor.falling || neighbor.level >= desc.maxLevel) {
            continue;
        }

        const FlowDirections flowDirections = computeFlowDirections(m_world, neighborPos, desc);
        if (flowDirections.foundHole && !flowDirections.allows(dirIndex)) {
            continue;
        }
        if (!flowDirections.foundHole && !flowDirections.hasAnyPassable) {
            continue;
        }

        minNeighborLevel = std::min(minNeighborLevel, neighbor.level);
        foundHorizontalSource = true;
    }

    if (!foundHorizontalSource || minNeighborLevel >= desc.maxLevel) {
        return BlockIds::AIR;
    }

    return FluidState::encode(DecodedFluid{
        kind,
        static_cast<uint8_t>(minNeighborLevel + 1),
        !supportBelow,
        false
    });
}

uint64_t FluidSystem::resolveNeighborhoodTickDelay(const glm::ivec3& pos) const {
    uint64_t bestDelay = std::numeric_limits<uint64_t>::max();
    for (const glm::ivec3& offset : kFluidUpdateOffsets) {
        const glm::ivec3 samplePos = pos + offset;
        if (!m_world.isChunkLoadedForBlock(samplePos.x, samplePos.y, samplePos.z)) {
            continue;
        }

        const DecodedFluid fluid = FluidState::decode(
            m_world.getBlockState(samplePos.x, samplePos.y, samplePos.z));
        if (fluid.kind == FluidKind::None) {
            continue;
        }

        bestDelay = std::min(bestDelay, FluidRegistry::get(fluid.kind).tickDelay);
    }

    if (bestDelay == std::numeric_limits<uint64_t>::max()) {
        return FluidRegistry::defaultTickDelay();
    }
    return bestDelay;
}
