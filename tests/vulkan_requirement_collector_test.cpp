#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>

namespace {

VkExtensionProperties extension(const char* name) {
    VkExtensionProperties result{};
    const size_t length = std::min(std::strlen(name),
                                   static_cast<size_t>(VK_MAX_EXTENSION_NAME_SIZE - 1u));
    std::memcpy(result.extensionName, name, length);
    return result;
}

} // namespace

int main() {
    VulkanRequirementCollector collector;
    collector.requireInstanceExtension("VK_KHR_surface");
    collector.requireInstanceExtension("VK_KHR_surface");
    collector.requireDeviceExtension("VK_KHR_swapchain");
    collector.requireDeviceExtension("VK_KHR_swapchain");
    collector.requireQueue(RhiQueueType::Graphics, 1u);
    collector.requireQueue(RhiQueueType::Graphics, 2u);
    collector.requireQueue(RhiQueueType::Compute, 1u);

    const auto instanceNames = collector.instanceExtensionNames();
    const auto deviceNames = collector.deviceExtensionNames();
    assert(instanceNames.size() == 1u);
    assert(deviceNames.size() == 1u);
    assert(std::strcmp(instanceNames[0], "VK_KHR_surface") == 0);
    assert(std::strcmp(deviceNames[0], "VK_KHR_swapchain") == 0);
    assert(collector.requiredQueueCount(RhiQueueType::Graphics) == 2u);
    assert(collector.requiredQueueCount(RhiQueueType::Compute) == 1u);
    assert(collector.requiredQueueCount(RhiQueueType::Transfer) == 0u);

    std::string missing;
    assert(collector.validateInstanceExtensions({extension("VK_KHR_surface")}, missing));
    assert(collector.validateDeviceExtensions({extension("VK_KHR_swapchain")}, missing));
    assert(!collector.validateDeviceExtensions({extension("VK_EXT_debug_utils")}, missing));
    assert(missing == "VK_KHR_swapchain");
    return 0;
}
