#include "BlockNeighborUpdateQueue.h"

void BlockNeighborUpdateQueue::enqueue(const glm::ivec3& pos) {
    if (m_queued.insert(pos).second) {
        m_queue.push(pos);
    }
}

size_t BlockNeighborUpdateQueue::drain(std::vector<glm::ivec3>& out, size_t budget) {
    size_t count = 0;
    while (!m_queue.empty() && count < budget) {
        glm::ivec3 pos = m_queue.front();
        m_queue.pop();
        m_queued.erase(pos);
        out.push_back(pos);
        ++count;
    }
    return count;
}

size_t BlockNeighborUpdateQueue::size() const {
    return m_queue.size();
}

void BlockNeighborUpdateQueue::clear() {
    while (!m_queue.empty()) m_queue.pop();
    m_queued.clear();
}
