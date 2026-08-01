#ifndef MECRAFT_BINDLESS_DESCRIPTOR_SLOT_ALLOCATOR_H
#define MECRAFT_BINDLESS_DESCRIPTOR_SLOT_ALLOCATOR_H

#include "renderer/contracts/BindlessDescriptorContract.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace renderer::core {

/// Identifies deterministic bindless descriptor-slot lifecycle failures.
enum class BindlessDescriptorSlotError : uint8_t {
    None,
    CapacityExceeded,
    PublicationRejected,
    InvalidHandle,
    StaleGeneration,
    SlotNotLive
};

/// Returns the stable identifier used by diagnostics and automated validation.
/// @param error Descriptor-slot lifecycle failure to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* bindlessDescriptorSlotErrorStableId(BindlessDescriptorSlotError error);

/// Exposes bounded bindless table occupancy without traversing allocator storage.
struct BindlessDescriptorSlotStats final {
    uint32_t capacity = 0u;
    uint32_t liveCount = 0u;
    uint32_t retiredCount = 0u;
    uint32_t availableCount = 0u;
    uint32_t exhaustedCount = 0u;
    uint32_t peakLiveCount = 0u;
};

/// Reports how many retired slots became reusable after one completed submission sequence.
struct BindlessDescriptorSlotReclaimResult final {
    uint32_t reclaimedCount = 0u;
    uint32_t exhaustedCount = 0u;
};

/// Returns either one allocated handle generation or a structured capacity failure.
/// @tparam Tag Bindless resource-array domain associated with the allocated slot.
template <typename Tag> struct BindlessDescriptorSlotAllocationResult final {
    renderer::contracts::BindlessDescriptorHandle<Tag> handle;
    BindlessDescriptorSlotError error = BindlessDescriptorSlotError::None;

    /// Reports whether allocation produced one live descriptor handle.
    /// @return True only when no allocation error was recorded and the handle is valid.
    [[nodiscard]] constexpr bool succeeded() const {
        return error == BindlessDescriptorSlotError::None && handle.isValid();
    }
};

/// Allocates zero-based descriptor slots and delays generation reuse until every referencing GPU submission completes.
/// The owner serializes calls on its device thread; this class intentionally performs no internal locking.
/// @tparam Tag Bindless resource-array domain that determines the returned strong handle type.
template <typename Tag> class BindlessDescriptorSlotAllocator final {
public:
    using Handle = renderer::contracts::BindlessDescriptorHandle<Tag>;
    using AllocationResult = BindlessDescriptorSlotAllocationResult<Tag>;

    /// Creates an allocator for one immutable descriptor-array capacity.
    /// @param capacity Maximum number of simultaneously live, retired, and exhausted slots.
    explicit BindlessDescriptorSlotAllocator(const uint32_t capacity) : m_slots(capacity) {}

    /// Allocates one live slot in constant amortized time.
    /// Fresh slots are assigned in ascending order; completed retired slots are reused with a new generation.
    /// @return Live handle or CapacityExceeded when no slot can be published.
    [[nodiscard]] AllocationResult allocate() {
        return allocateAndPublish([](const Handle) { return true; });
    }

    /// Allocates one slot only after the owner atomically publishes its descriptor contents.
    /// The selected fresh or recycled index remains available when publication is rejected.
    /// @tparam Publisher Callable receiving the candidate handle and returning true after publication.
    /// @param publisher Atomic descriptor publication operation executed before allocator state changes.
    /// @return Live handle, CapacityExceeded, or PublicationRejected without consuming a slot.
    template <typename Publisher> [[nodiscard]] AllocationResult allocateAndPublish(Publisher&& publisher) {
        uint32_t index = 0u;
        bool recycled = false;
        if (!m_recycledIndices.empty()) {
            index = m_recycledIndices.back();
            recycled = true;
        } else if (m_nextFreshIndex < m_slots.size()) {
            index = m_nextFreshIndex;
        } else {
            return {{}, BindlessDescriptorSlotError::CapacityExceeded};
        }

        Slot& slot = m_slots[index];
        assert(slot.state == SlotState::Available);
        assert(slot.generation != 0u);
        const Handle handle{index, slot.generation};
        if (!publisher(handle)) {
            return {{}, BindlessDescriptorSlotError::PublicationRejected};
        }
        if (recycled) {
            m_recycledIndices.pop_back();
        } else {
            ++m_nextFreshIndex;
        }
        slot.state = SlotState::Live;
        ++m_liveCount;
        m_peakLiveCount = std::max(m_peakLiveCount, m_liveCount);
        return {handle, BindlessDescriptorSlotError::None};
    }

    /// Removes one live generation from publication and queues its index for submission-aware reuse.
    /// @param handle Exact live generation being retired.
    /// @param lastUseSequence Newest GPU submission that may read this descriptor slot.
    /// @return None on success or a stable validation error without mutating allocator state.
    [[nodiscard]] BindlessDescriptorSlotError retire(const Handle handle, const uint64_t lastUseSequence) {
        const BindlessDescriptorSlotError validation = validateLive(handle);
        if (validation != BindlessDescriptorSlotError::None) {
            return validation;
        }
        Slot& slot = m_slots[handle.index];

        slot.state = SlotState::Retired;
        --m_liveCount;
        ++m_retiredCount;
        m_retiredSlots.push_back({lastUseSequence, handle.index, handle.generation});
        std::push_heap(m_retiredSlots.begin(), m_retiredSlots.end(), RetiredSlotLater{});
        return BindlessDescriptorSlotError::None;
    }

    /// Validates one exact live handle without mutating slot state.
    /// @param handle Descriptor generation to inspect.
    /// @return None for a live generation or its precise stable lifecycle error.
    [[nodiscard]] BindlessDescriptorSlotError validateLive(const Handle handle) const {
        if (!handle.isValid() || handle.index >= m_slots.size()) {
            return BindlessDescriptorSlotError::InvalidHandle;
        }
        const Slot& slot = m_slots[handle.index];
        if (slot.generation != handle.generation) {
            return BindlessDescriptorSlotError::StaleGeneration;
        }
        return slot.state == SlotState::Live ? BindlessDescriptorSlotError::None
                                             : BindlessDescriptorSlotError::SlotNotLive;
    }

    /// Reclaims every retired slot whose final referencing submission has completed.
    /// Reclaimed slots increment generation before entering the reusable free list. A generation that reaches the
    /// complete uint32 range is permanently exhausted instead of wrapping to a stale published value.
    /// @param completedSequence Greatest globally completed GPU submission sequence.
    /// @return Counts of reusable and permanently exhausted slots processed by this call.
    [[nodiscard]] BindlessDescriptorSlotReclaimResult reclaim(const uint64_t completedSequence) {
        BindlessDescriptorSlotReclaimResult result;
        while (!m_retiredSlots.empty() && m_retiredSlots.front().sequence <= completedSequence) {
            std::pop_heap(m_retiredSlots.begin(), m_retiredSlots.end(), RetiredSlotLater{});
            const RetiredSlot retired = m_retiredSlots.back();
            m_retiredSlots.pop_back();

            Slot& slot = m_slots[retired.index];
            assert(slot.state == SlotState::Retired);
            assert(slot.generation == retired.generation);
            --m_retiredCount;
            if (slot.generation == std::numeric_limits<uint32_t>::max()) {
                slot.state = SlotState::Exhausted;
                ++m_exhaustedCount;
                ++result.exhaustedCount;
                continue;
            }
            ++slot.generation;
            slot.state = SlotState::Available;
            m_recycledIndices.push_back(retired.index);
            ++result.reclaimedCount;
        }
        return result;
    }

    /// Tests whether a handle still identifies the currently published generation.
    /// @param handle Descriptor handle to validate.
    /// @return True only for an in-range live slot with an equal generation.
    [[nodiscard]] bool isLive(const Handle handle) const {
        return validateLive(handle) == BindlessDescriptorSlotError::None;
    }

    /// Returns current table occupancy and the peak number of simultaneously live slots.
    /// @return Constant-time snapshot suitable for Dashboard and capacity validation.
    [[nodiscard]] BindlessDescriptorSlotStats stats() const {
        const uint32_t capacity = static_cast<uint32_t>(m_slots.size());
        return {capacity,         m_liveCount,
                m_retiredCount,   capacity - m_liveCount - m_retiredCount - m_exhaustedCount,
                m_exhaustedCount, m_peakLiveCount};
    }

private:
    enum class SlotState : uint8_t { Available, Live, Retired, Exhausted };

    struct Slot final {
        uint32_t generation = 1u;
        SlotState state = SlotState::Available;
    };

    struct RetiredSlot final {
        uint64_t sequence = 0u;
        uint32_t index = 0u;
        uint32_t generation = 0u;
    };

    struct RetiredSlotLater final {
        [[nodiscard]] bool operator()(const RetiredSlot& lhs, const RetiredSlot& rhs) const {
            return lhs.sequence != rhs.sequence ? lhs.sequence > rhs.sequence : lhs.index > rhs.index;
        }
    };

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_recycledIndices;
    std::vector<RetiredSlot> m_retiredSlots;
    uint32_t m_nextFreshIndex = 0u;
    uint32_t m_liveCount = 0u;
    uint32_t m_retiredCount = 0u;
    uint32_t m_exhaustedCount = 0u;
    uint32_t m_peakLiveCount = 0u;
};

} // namespace renderer::core

#endif // MECRAFT_BINDLESS_DESCRIPTOR_SLOT_ALLOCATOR_H
