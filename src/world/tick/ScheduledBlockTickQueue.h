#ifndef MECRAFT_SCHEDULED_BLOCK_TICK_QUEUE_H
#define MECRAFT_SCHEDULED_BLOCK_TICK_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

/// One scheduled block update, ordered by the game tick it becomes due.
struct ScheduledBlockTick {
    uint64_t dueTick = 0;
    glm::ivec3 pos{};
};

/// Hashable block position key for per-position bookkeeping tables.
struct BlockPositionKey {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const BlockPositionKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct BlockPositionKeyHash {
    size_t operator()(const BlockPositionKey& pos) const noexcept;
};

/// World-agnostic scheduled block tick queue.
///
/// Combines a priority queue with a per-position dedup table so each block
/// position carries at most one pending tick, always at the earliest due tick
/// requested. Pop order is deterministic: dueTick ascending, then higher Y
/// first (so downward propagation is stable), then x/z ascending.
///
/// The queue is reentrant: the callback passed to process() may call
/// schedule(), and entries newly due this tick participate in the same run
/// following the comparator order.
class ScheduledBlockTickQueue {
public:
    /// Schedule one block position for a future tick.
    /// @param pos  World-space block position to update.
    /// @param dueTick  Game tick at which the update becomes due.
    ///
    /// An existing pending tick at an earlier or equal due tick wins and the
    /// new request is dropped; an earlier due tick replaces a later one.
    void schedule(const glm::ivec3& pos, uint64_t dueTick);

    /// Pop and execute due entries while budget lasts.
    /// @param currentTick  Game tick being processed; entries due at or
    ///                     before this tick are eligible.
    /// @param budget  Maximum number of entries to execute this call.
    /// @param fn  Callback invoked per executed entry, signature
    ///            void(const ScheduledBlockTick&). May re-enter schedule().
    /// @return Number of entries executed.
    ///
    /// Stale heap duplicates whose dedup entry no longer matches are skipped
    /// without consuming budget.
    template <typename Fn>
    uint32_t process(uint64_t currentTick, uint32_t budget, Fn&& fn) {
        uint32_t processed = 0;
        while (!m_queue.empty() && m_queue.top().dueTick <= currentTick && processed < budget) {
            const ScheduledBlockTick next = m_queue.top();
            m_queue.pop();

            const BlockPositionKey key{next.pos.x, next.pos.y, next.pos.z};
            const auto it = m_due.find(key);
            if (it == m_due.end() || it->second != next.dueTick) {
                continue;
            }
            m_due.erase(it);

            fn(next);
            ++processed;
        }
        return processed;
    }

    /// Unique pending positions (dedup table size, not heap size).
    [[nodiscard]] size_t pendingCount() const { return m_due.size(); }

    /// Remove all pending entries.
    void clear();

private:
    struct ScheduledBlockTickCompare {
        bool operator()(const ScheduledBlockTick& lhs, const ScheduledBlockTick& rhs) const {
            if (lhs.dueTick != rhs.dueTick) {
                return lhs.dueTick > rhs.dueTick;
            }
            // Deterministic in-tick order: process higher cells first so downward
            // flow propagates consistently, then sweep x/z ascending.
            if (lhs.pos.y != rhs.pos.y) {
                return lhs.pos.y < rhs.pos.y;
            }
            if (lhs.pos.x != rhs.pos.x) {
                return lhs.pos.x > rhs.pos.x;
            }
            return lhs.pos.z > rhs.pos.z;
        }
    };

    std::priority_queue<ScheduledBlockTick, std::vector<ScheduledBlockTick>, ScheduledBlockTickCompare> m_queue;
    std::unordered_map<BlockPositionKey, uint64_t, BlockPositionKeyHash> m_due;
};

#endif // MECRAFT_SCHEDULED_BLOCK_TICK_QUEUE_H
