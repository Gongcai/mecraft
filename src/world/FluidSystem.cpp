#include "FluidSystem.h"

#include <algorithm>
#include <array>
#include <cstdlib>
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
    const FluidCellView cell = world.getCombinedCell(pos.x, pos.y, pos.z);
    // Can replace if air, same fluid, or waterlogged block that can accept this fluid
    if (cell.isEmpty()) {
        return true;
    }
    if (FluidState::decode(cell.fluidState).kind == desc.kind) {
        return true;
    }
    if (cell.hasBlock() && cell.fluidState == BlockIds::AIR &&
        FluidState::canCoexist(desc, cell.blockState)) {
        return true;
    }
    return FluidState::canReplace(desc, cell.blockState);
}

bool hasSupportBelow(const World& world, const glm::ivec3& pos, const FluidDesc& desc) {
    const glm::ivec3 belowPos = pos + glm::ivec3(0, -1, 0);
    if (!isPositionLoaded(world, belowPos)) {
        return true;
    }
    const FluidCellView cell = world.getCombinedCell(belowPos.x, belowPos.y, belowPos.z);
    // A position has support if there's a solid block (not passable by fluid)
    if (cell.isEmpty()) {
        return false;
    }
    if (cell.hasBlock() && !FluidState::canReplace(desc, cell.blockState) &&
        !FluidState::canCoexist(desc, cell.blockState)) {
        return true;
    }
    return false;
}

bool isUnfilledDownhillPath(const World& world, const glm::ivec3& pos, const FluidDesc& desc) {
    const glm::ivec3 belowPos = pos + glm::ivec3(0, -1, 0);
    if (!isPositionLoaded(world, belowPos)) {
        return false;
    }

    const FluidCellView below = world.getCombinedCell(belowPos.x, belowPos.y, belowPos.z);
    const DecodedFluid belowFluid = FluidState::decode(below.fluidState);
    if (belowFluid.kind == desc.kind) {
        return false;
    }
    if (belowFluid.kind != FluidKind::None) {
        return false;
    }
    if (below.isEmpty()) {
        return true;
    }
    if (below.hasBlock()) {
        return FluidState::canReplace(desc, below.blockState) ||
               FluidState::canCoexist(desc, below.blockState);
    }
    return false;
}

FluidKind resolveTargetFluidKind(const World& world, const glm::ivec3& pos, const BlockID currentId) {
    // Check fluid state first (covers both pure fluid and waterlogged blocks)
    const StateID currentFluidState = world.getFluidState(pos.x, pos.y, pos.z);
    const DecodedFluid currentFluid = FluidState::decode(currentFluidState);
    if (currentFluid.kind != FluidKind::None) {
        return currentFluid.kind;
    }

    const glm::ivec3 abovePos = pos + glm::ivec3(0, 1, 0);
    if (isPositionLoaded(world, abovePos)) {
        const FluidKind aboveKind = FluidState::decode(world.getFluidState(abovePos.x, abovePos.y, abovePos.z)).kind;
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
            world.getFluidState(neighborPos.x, neighborPos.y, neighborPos.z)).kind;
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

        if (isUnfilledDownhillPath(world, firstStep, desc)) {
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
                if (isUnfilledDownhillPath(world, nextPos, desc)) {
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
            world.getFluidState(neighborPos.x, neighborPos.y, neighborPos.z));
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

void FluidSystem::onBlockChanged(const glm::ivec3 pos, const bool scheduleSlopeSearchNeighborhood) {
    const uint64_t dueTick = m_lastProcessedGameTick + resolveNeighborhoodTickDelay(pos);
    scheduleNeighborsForFluidUpdate(pos, dueTick);
    if (scheduleSlopeSearchNeighborhood) {
        scheduleSlopeSearchNeighborhoodForFluidUpdate(pos, dueTick);
    }
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

void FluidSystem::scheduleSlopeSearchNeighborhoodForFluidUpdate(const glm::ivec3 pos, const uint64_t dueTick) {
    const FluidDesc& waterDesc = FluidRegistry::get(FluidKind::Water);
    const int radius = static_cast<int>(waterDesc.slopeSearchDistance);
    if (radius <= 1) {
        return;
    }

    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (std::abs(dx) + std::abs(dz) > radius) {
                continue;
            }

            const glm::ivec3 horizontalOffset(dx, 0, dz);
            scheduleBlockTick(pos + horizontalOffset, dueTick);
            scheduleBlockTick(pos + horizontalOffset + glm::ivec3(0, 1, 0), dueTick);
        }
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

    const FluidCellView cell = m_world.getCombinedCell(pos.x, pos.y, pos.z);
    const StateID currentFluidState = cell.fluidState;
    const StateID currentBlockState = cell.blockState;
    const BlockID effectiveCurrentId = (currentFluidState != BlockIds::AIR) ? currentFluidState : currentBlockState;

    const FluidKind targetKind = resolveTargetFluidKind(m_world, pos, effectiveCurrentId);
    if (targetKind == FluidKind::None) {
        if (currentFluidState != BlockIds::AIR) {
            // Fluid should retract — clear the fluid layer
            m_world.setFluidState(pos.x, pos.y, pos.z, BlockIds::AIR);
        }
        return;
    }

    const FluidDesc& desc = FluidRegistry::get(targetKind);
    // Check if this cell can accept fluid
    if (cell.hasBlock() && currentFluidState == BlockIds::AIR) {
        // Non-fluid block — can only accept fluid if it allows coexistence
        if (!FluidState::canCoexist(desc, currentBlockState)) {
            return;
        }
    }

    const StateID targetState = computeTargetFluidState(pos, effectiveCurrentId);
    if (targetState == currentFluidState || targetState == effectiveCurrentId) {
        return;
    }

    m_world.setFluidState(pos.x, pos.y, pos.z, targetState);
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
    if (current.kind == FluidKind::None) {
        // Check if the block at this position can accept fluid
        const FluidCellView cell = m_world.getCombinedCell(pos.x, pos.y, pos.z);
        if (cell.hasBlock() && !FluidState::canCoexist(desc, cell.blockState)) {
            return currentId;
        }
    }

    const bool supportBelow = hasSupportBelow(m_world, pos, desc);
    const glm::ivec3 abovePos = pos + glm::ivec3(0, 1, 0);
    const StateID aboveState = isPositionLoaded(m_world, abovePos)
        ? m_world.getFluidState(abovePos.x, abovePos.y, abovePos.z)
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

        const StateID neighborFluidState = m_world.getFluidState(neighborPos.x, neighborPos.y, neighborPos.z);
        const DecodedFluid neighbor = FluidState::decode(neighborFluidState);
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
            m_world.getFluidState(samplePos.x, samplePos.y, samplePos.z));
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
