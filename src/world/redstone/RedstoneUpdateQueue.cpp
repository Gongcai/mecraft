#include "RedstoneUpdateQueue.h"

void RedstoneUpdateQueue::schedule(const uint64_t executionTick,
                                   const glm::ivec3& position,
                                   const RedstoneScheduledAction action) {
    const ScheduledKey key{position, action};
    if (m_scheduledKeys.find(key) != m_scheduledKeys.end()) {
        return;
    }

    const uint32_t sequence = m_sequenceCounter++;
    m_scheduledKeys.emplace(key, sequence);
    m_queue.push(RedstoneScheduledUpdate{
        executionTick,
        position,
        action,
        sequence
    });
}

void RedstoneUpdateQueue::reschedule(const uint64_t executionTick,
                                     const glm::ivec3& position,
                                     const RedstoneScheduledAction action) {
    const ScheduledKey key{position, action};
    const uint32_t sequence = m_sequenceCounter++;
    m_scheduledKeys[key] = sequence;
    m_queue.push(RedstoneScheduledUpdate{
        executionTick,
        position,
        action,
        sequence
    });
}

size_t RedstoneUpdateQueue::drainDue(const uint64_t currentTick,
                                     std::vector<RedstoneScheduledUpdate>& out,
                                     const size_t budget) {
    size_t count = 0;
    while (!m_queue.empty() && count < budget) {
        const RedstoneScheduledUpdate next = m_queue.top();
        if (next.executionTick > currentTick) {
            break;
        }

        m_queue.pop();
        const ScheduledKey key{next.position, next.action};
        const auto keyIt = m_scheduledKeys.find(key);
        if (keyIt == m_scheduledKeys.end() || keyIt->second != next.sequence) {
            continue;
        }
        m_scheduledKeys.erase(keyIt);
        out.push_back(next);
        ++count;
    }
    return count;
}

size_t RedstoneUpdateQueue::size() const {
    return m_queue.size();
}

void RedstoneUpdateQueue::clear() {
    while (!m_queue.empty()) {
        m_queue.pop();
    }
    m_scheduledKeys.clear();
}
