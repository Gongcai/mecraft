#include "renderer/rhi/vulkan/VkRhiInterop.h"

#include "renderer/rhi/vulkan/VkRhiConversions.h"

using namespace renderer::rhi::vulkan;

VkPipelineStageFlags2 VkRhiInterop::resourceStages(const RhiResourceState state) {
    return toVkResourceState(state).stages;
}

VkAccessFlags2 VkRhiInterop::resourceAccess(const RhiResourceState state) {
    return toVkResourceState(state).access;
}

VkImageLayout VkRhiInterop::resourceLayout(const RhiResourceState state) {
    return toVkResourceState(state).layout;
}
