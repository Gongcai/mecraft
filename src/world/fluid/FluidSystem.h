#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../block/BlockStateRegistry.h"
#include "FluidRegistry.h"

class World;

struct ScheduledBlockTick {
    uint64_t dueTick = 0;
    glm::ivec3 pos{};
};

class FluidSystem {
public:
    explicit FluidSystem(World& world) : m_world(world) {}

    void reset();

    void onBlockChanged(glm::ivec3 pos, bool scheduleSlopeSearchNeighborhood = false);
    void scheduleBlockTick(glm::ivec3 pos, uint64_t dueTick);
    void scheduleNeighborsForFluidUpdate(glm::ivec3 pos, uint64_t dueTick);
    void processScheduledBlockTicks(uint64_t currentTick, uint32_t budget = 4096);

    [[nodiscard]] size_t pendingTickCount() const { return m_scheduledBlockTickDue.size(); }

private:
    struct ScheduledBlockTickPos {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const ScheduledBlockTickPos& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct ScheduledBlockTickPosHash {
        size_t operator()(const ScheduledBlockTickPos& pos) const noexcept;
    };

    struct ScheduledBlockTickCompare {
        bool operator()(const ScheduledBlockTick& lhs, const ScheduledBlockTick& rhs) const {
            return lhs.dueTick > rhs.dueTick;
        }
    };

    void updateFluidCell(const glm::ivec3& pos);
    void scheduleSlopeSearchNeighborhoodForFluidUpdate(glm::ivec3 pos, uint64_t dueTick);
    [[nodiscard]] BlockStateId computeTargetFluidState(const glm::ivec3& pos, BlockStateId currentState) const;
    [[nodiscard]] uint64_t resolveNeighborhoodTickDelay(const glm::ivec3& pos) const;

    World& m_world;
    std::priority_queue<ScheduledBlockTick, std::vector<ScheduledBlockTick>, ScheduledBlockTickCompare>
        m_scheduledBlockTickQueue;
    std::unordered_map<ScheduledBlockTickPos, uint64_t, ScheduledBlockTickPosHash> m_scheduledBlockTickDue;
    uint64_t m_lastProcessedGameTick = 0;
};
