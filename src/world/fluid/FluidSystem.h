#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "../block/BlockStateRegistry.h"
#include "../tick/ScheduledBlockTickQueue.h"
#include "FluidRegistry.h"

class World;

struct FlowDirections {
    uint8_t allowedMask = 0;
    bool foundHole = false;
    bool hasAnyPassable = false;

    [[nodiscard]] bool allows(const int directionIndex) const { return (allowedMask & (1u << directionIndex)) != 0u; }
};

class FluidSystem {
public:
    explicit FluidSystem(World& world) : m_world(world) {}

    void reset();

    void onBlockChanged(glm::ivec3 pos, bool scheduleSlopeSearchNeighborhood = false);
    void scheduleBlockTick(glm::ivec3 pos, uint64_t dueTick);
    void scheduleNeighborsForFluidUpdate(glm::ivec3 pos, uint64_t dueTick);
    void processScheduledBlockTicks(uint64_t currentTick, uint32_t budget = 4096);

    [[nodiscard]] size_t pendingTickCount() const { return m_scheduledTickQueue.pendingCount(); }

private:
    struct FlowDirectionCacheEntry {
        uint64_t tick = 0;
        uint8_t allowedMask = 0;
        bool foundHole = false;
        bool hasAnyPassable = false;
    };

    void updateFluidCell(const glm::ivec3& pos);
    void scheduleSlopeSearchNeighborhoodForFluidUpdate(glm::ivec3 pos, uint64_t dueTick);
    [[nodiscard]] BlockStateId computeTargetFluidState(const glm::ivec3& pos, BlockStateId currentState);
    [[nodiscard]] FlowDirections flowDirectionsFor(const glm::ivec3& pos, const FluidDesc& desc);
    void purgeStaleFlowDirectionCache(uint64_t currentTick);
    [[nodiscard]] uint64_t resolveNeighborhoodTickDelay(const glm::ivec3& pos) const;

    World& m_world;
    ScheduledBlockTickQueue m_scheduledTickQueue;
    std::unordered_map<BlockPositionKey, FlowDirectionCacheEntry, BlockPositionKeyHash> m_flowDirectionCache;
    uint64_t m_lastProcessedGameTick = 0;
};
