#ifndef MECRAFT_RHI_HANDLE_ALLOCATOR_H
#define MECRAFT_RHI_HANDLE_ALLOCATOR_H

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

template <typename Handle> class RhiHandleAllocator {
public:
    explicit RhiHandleAllocator(const uint32_t firstIndex = 1u) : m_firstIndex(firstIndex) {
        assert(m_firstIndex != 0u);
    }

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
        assert(slot <= std::numeric_limits<uint32_t>::max() - m_firstIndex);
        return Handle{m_firstIndex + slot, m_generations[slot]};
    }

    [[nodiscard]] bool isAlive(Handle handle) const { return slotForHandle(handle).has_value(); }

    [[nodiscard]] std::optional<uint32_t> slotForHandle(const Handle handle) const {
        if (handle.index < m_firstIndex) {
            return std::nullopt;
        }
        const uint32_t slot = handle.index - m_firstIndex;
        if (slot >= m_generations.size() || !m_alive[slot] || m_generations[slot] != handle.generation) {
            return std::nullopt;
        }
        return slot;
    }

    [[nodiscard]] bool release(Handle handle) {
        const std::optional<uint32_t> slot = slotForHandle(handle);
        if (!slot.has_value()) {
            return false;
        }

        m_alive[*slot] = false;
        m_freeSlots.push_back(*slot);
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
    uint32_t m_firstIndex = 1u;
    std::vector<uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<uint32_t> m_freeSlots;
};

#endif // MECRAFT_RHI_HANDLE_ALLOCATOR_H
