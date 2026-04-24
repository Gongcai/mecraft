#include "FluidSystem.h"

#include <array>
#include <algorithm>
#include <functional>

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

constexpr uint64_t kWaterFlowTickDelay = 5;

}

void FluidSystem::reset() {
    m_scheduledBlockTickQueue = {};
    m_scheduledBlockTickDue.clear();
    m_lastProcessedGameTick = 0;
}

void FluidSystem::onBlockChanged(const glm::ivec3 pos) {
    scheduleNeighborsForFluidUpdate(pos, m_lastProcessedGameTick + kWaterFlowTickDelay);
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
    if (!FluidState::isWater(currentId) && !FluidState::canWaterReplace(currentId)) {
        return;
    }

    const StateID targetState = computeTargetFluidState(pos, currentId);
    if (targetState == currentId) {
        return;
    }

    m_world.setBlockState(pos.x, pos.y, pos.z, targetState);
}

StateID FluidSystem::computeTargetFluidState(const glm::ivec3& pos, const BlockID currentId) const {
    if (FluidState::isSource(currentId)) {
        return FluidState::makeWater(0, false);
    }

    if (!FluidState::isWater(currentId) && !FluidState::canWaterReplace(currentId)) {
        return currentId;
    }

    const BlockID below = m_world.getBlockState(pos.x, pos.y - 1, pos.z);
    const bool hasSupportBelow = !FluidState::canWaterReplace(below);
    const BlockID above = m_world.getBlockState(pos.x, pos.y + 1, pos.z);
    if (FluidState::isWater(above)) {
        if (!hasSupportBelow) {
            return FluidState::makeWater(0, true);
        }
        return FluidState::makeWater(1, false);
    }

    uint8_t minNeighborLevel = 8;
    bool foundHorizontalSource = false;
    for (const glm::ivec3& offset : kHorizontalFluidOffsets) {
        const BlockID neighbor = m_world.getBlockState(pos.x + offset.x, pos.y + offset.y, pos.z + offset.z);
        if (!FluidState::isWater(neighbor) || FluidState::isFalling(neighbor)) {
            continue;
        }

        minNeighborLevel = std::min(minNeighborLevel, FluidState::level(neighbor));
        foundHorizontalSource = true;
    }

    if (!foundHorizontalSource || minNeighborLevel >= 7) {
        return BlockIds::AIR;
    }

    return FluidState::makeWater(static_cast<uint8_t>(minNeighborLevel + 1), !hasSupportBelow);
}
