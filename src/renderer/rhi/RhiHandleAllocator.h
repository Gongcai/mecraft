#ifndef MECRAFT_RHI_HANDLE_ALLOCATOR_H
#define MECRAFT_RHI_HANDLE_ALLOCATOR_H

#include <cstdint>
#include <vector>

template <typename Handle>
class RhiHandleAllocator {
public:
    [[nodiscard]] Handle allocate() {
        uint32_t slot = 0;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            ++m_generations[slot];
            if (m_generations[slot] == 0) {
                m_generations[slot] = 1;
            }
        } else {
            slot = static_cast<uint32_t>(m_generations.size());
            m_generations.push_back(1);
            m_alive.push_back(false);
        }

        m_alive[slot] = true;
        return Handle{slot + 1, m_generations[slot]};
    }

    [[nodiscard]] bool isAlive(Handle handle) const {
        if (handle.index == 0) {
            return false;
        }
        const uint32_t slot = handle.index - 1;
        return slot < m_generations.size() &&
               m_alive[slot] &&
               m_generations[slot] == handle.generation;
    }

    [[nodiscard]] bool release(Handle handle) {
        if (!isAlive(handle)) {
            return false;
        }

        const uint32_t slot = handle.index - 1;
        m_alive[slot] = false;
        m_freeSlots.push_back(slot);
        return true;
    }

    [[nodiscard]] uint32_t liveCount() const {
        uint32_t count = 0;
        for (const bool alive : m_alive) {
            if (alive) {
                ++count;
            }
        }
        return count;
    }

    void clear() {
        m_generations.clear();
        m_alive.clear();
        m_freeSlots.clear();
    }

private:
    std::vector<uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<uint32_t> m_freeSlots;
};

#endif // MECRAFT_RHI_HANDLE_ALLOCATOR_H
