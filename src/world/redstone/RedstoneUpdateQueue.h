#ifndef MECRAFT_REDSTONE_UPDATE_QUEUE_H
#define MECRAFT_REDSTONE_UPDATE_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

enum class RedstoneScheduledAction : uint8_t {
    ReleaseButton,
    EvaluateRepeater
};

/// A delayed redstone update scheduled for a specific redstone tick.
struct RedstoneScheduledUpdate {
    uint64_t executionTick = 0;
    glm::ivec3 position{};
    RedstoneScheduledAction action = RedstoneScheduledAction::ReleaseButton;
    uint32_t sequence = 0;

    [[nodiscard]] bool operator>(const RedstoneScheduledUpdate& other) const {
        if (executionTick != other.executionTick) {
            return executionTick > other.executionTick;
        }
        return sequence > other.sequence;
    }
};

/// Priority queue for deterministic delayed redstone device transitions.
class RedstoneUpdateQueue {
public:
    /// Schedule one delayed redstone action.
    /// @param executionTick  Redstone tick at which the action becomes due.
    /// @param position  World-space block position affected by the action.
    /// @param action  Device action to execute when due.
    void schedule(uint64_t executionTick,
                  const glm::ivec3& position,
                  RedstoneScheduledAction action);

    /// Drain due actions in execution order.
    /// @param currentTick  Current redstone tick.
    /// @param out  Receives due updates.
    /// @param budget  Maximum number of due updates to drain.
    /// @return Number of due updates drained.
    size_t drainDue(uint64_t currentTick,
                    std::vector<RedstoneScheduledUpdate>& out,
                    size_t budget);

    /// Total delayed updates currently pending.
    [[nodiscard]] size_t size() const;

    /// Remove all delayed updates.
    void clear();

private:
    struct ScheduledKey {
        glm::ivec3 position{};
        RedstoneScheduledAction action = RedstoneScheduledAction::ReleaseButton;

        [[nodiscard]] bool operator==(const ScheduledKey& other) const noexcept {
            return position == other.position && action == other.action;
        }
    };

    struct ScheduledKeyHash {
        [[nodiscard]] size_t operator()(const ScheduledKey& key) const noexcept {
            size_t seed = 14695981039346656037ULL;
            const auto mix = [&seed](const size_t value) {
                seed ^= value;
                seed *= 1099511628211ULL;
            };
            mix(static_cast<size_t>(key.position.x));
            mix(static_cast<size_t>(key.position.y));
            mix(static_cast<size_t>(key.position.z));
            mix(static_cast<size_t>(key.action));
            return seed;
        }
    };

    std::priority_queue<
        RedstoneScheduledUpdate,
        std::vector<RedstoneScheduledUpdate>,
        std::greater<>> m_queue;
    std::unordered_set<ScheduledKey, ScheduledKeyHash> m_scheduledKeys;
    uint32_t m_sequenceCounter = 0;
};

#endif // MECRAFT_REDSTONE_UPDATE_QUEUE_H
