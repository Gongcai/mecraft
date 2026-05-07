#ifndef MECRAFT_BLOCK_NEIGHBOR_UPDATE_QUEUE_H
#define MECRAFT_BLOCK_NEIGHBOR_UPDATE_QUEUE_H

#include <glm/glm.hpp>
#include <queue>
#include <unordered_set>
#include <vector>

namespace ecs { class GameplayRegistry; }

/// A queue of block positions that need a neighbor-update check.
/// When a block changes, its 6 neighbors are enqueued; the tick-rate
/// BlockSupportSystem drains entries and validates support rules.
class BlockNeighborUpdateQueue {
public:
    /// Enqueue a position for neighbor-update processing.
    void enqueue(const glm::ivec3& pos);

    /// Drain up to `budget` entries, returning them in the output vector.
    /// Returns the number of entries drained.
    size_t drain(std::vector<glm::ivec3>& out, size_t budget);

    /// Total entries currently pending.
    size_t size() const;

    /// Clear all pending entries.
    void clear();

private:
    struct IVec3Hash {
        size_t operator()(const glm::ivec3& v) const noexcept {
            // FNV-1a inspired combine
            size_t h = 14695981039346656037ULL;
            h ^= static_cast<size_t>(v.x); h *= 1099511628211ULL;
            h ^= static_cast<size_t>(v.y); h *= 1099511628211ULL;
            h ^= static_cast<size_t>(v.z); h *= 1099511628211ULL;
            return h;
        }
    };

    std::queue<glm::ivec3> m_queue;
    std::unordered_set<glm::ivec3, IVec3Hash> m_queued;  // dedup
};

#endif // MECRAFT_BLOCK_NEIGHBOR_UPDATE_QUEUE_H
