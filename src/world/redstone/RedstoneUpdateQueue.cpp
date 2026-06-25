#include "RedstoneUpdateQueue.h"

void RedstoneUpdateQueue::schedule(const uint64_t executionTick,
                                   const glm::ivec3& position,
                                   const RedstoneScheduledAction action) {
    const ScheduledKey key{position, action};
    if (!m_scheduledKeys.insert(key).second) {
        return;
    }

    m_queue.push(RedstoneScheduledUpdate{
        executionTick,
        position,
        action,
        m_sequenceCounter++
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
        m_scheduledKeys.erase(ScheduledKey{next.position, next.action});
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
