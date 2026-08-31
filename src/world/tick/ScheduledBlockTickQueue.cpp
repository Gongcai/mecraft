#include "ScheduledBlockTickQueue.h"

#include <functional>

size_t BlockPositionKeyHash::operator()(const BlockPositionKey& pos) const noexcept {
    size_t hash = static_cast<size_t>(std::hash<int>{}(pos.x));
    hash ^= static_cast<size_t>(std::hash<int>{}(pos.y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u));
    hash ^= static_cast<size_t>(std::hash<int>{}(pos.z) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u));
    return hash;
}

void ScheduledBlockTickQueue::schedule(const glm::ivec3& pos, const uint64_t dueTick) {
    const BlockPositionKey key{pos.x, pos.y, pos.z};
    const auto it = m_due.find(key);
    if (it != m_due.end() && it->second <= dueTick) {
        return;
    }

    m_due[key] = dueTick;
    m_queue.push(ScheduledBlockTick{dueTick, pos});
}

void ScheduledBlockTickQueue::clear() {
    while (!m_queue.empty()) {
        m_queue.pop();
    }
    m_due.clear();
}
