#include "renderer/core/BindlessDescriptorSlotAllocator.h"

#include <cstdlib>

namespace renderer::core {

const char* bindlessDescriptorSlotErrorStableId(const BindlessDescriptorSlotError error) {
    switch (error) {
    case BindlessDescriptorSlotError::None: return "None";
    case BindlessDescriptorSlotError::CapacityExceeded: return "BindlessDescriptorCapacityExceeded";
    case BindlessDescriptorSlotError::PublicationRejected: return "BindlessDescriptorPublicationRejected";
    case BindlessDescriptorSlotError::InvalidHandle: return "InvalidHandle";
    case BindlessDescriptorSlotError::StaleGeneration: return "StaleGeneration";
    case BindlessDescriptorSlotError::SlotNotLive: return "SlotNotLive";
    }
    std::abort();
}

} // namespace renderer::core
