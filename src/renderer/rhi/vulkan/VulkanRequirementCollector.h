#ifndef MECRAFT_VULKAN_REQUIREMENT_COLLECTOR_H
#define MECRAFT_VULKAN_REQUIREMENT_COLLECTOR_H

#include "renderer/rhi/RhiTypes.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

/// Collects Vulkan instance, device, and queue requirements before object creation.
///
/// SDK integrations add their requirements to the same collector used by the RHI,
/// which keeps extension validation and queue planning in one place.
class VulkanRequirementCollector final {
public:
    /// Adds an instance extension requirement when it is not already present.
    /// @param extension Vulkan extension name with static or collector-owned lifetime.
    void requireInstanceExtension(const char* extension);

    /// Adds a device extension requirement when it is not already present.
    /// @param extension Vulkan extension name with static or collector-owned lifetime.
    void requireDeviceExtension(const char* extension);

    /// Records the minimum queue count required for a logical queue role.
    /// @param queue Queue role requested by the RHI or an SDK integration.
    /// @param count Minimum number of queues for the role.
    void requireQueue(RhiQueueType queue, uint32_t count = 1u);

    /// Returns the collected instance extension names as pointers usable by Vulkan.
    [[nodiscard]] std::vector<const char*> instanceExtensionNames() const;

    /// Returns the collected device extension names as pointers usable by Vulkan.
    [[nodiscard]] std::vector<const char*> deviceExtensionNames() const;

    /// Checks collected instance extensions against enumerated driver properties.
    /// @param available Enumerated instance extension properties.
    /// @param missing Receives the first missing extension name.
    [[nodiscard]] bool validateInstanceExtensions(
        const std::vector<VkExtensionProperties>& available,
        std::string& missing) const;

    /// Checks collected device extensions against enumerated driver properties.
    /// @param available Enumerated device extension properties.
    /// @param missing Receives the first missing extension name.
    [[nodiscard]] bool validateDeviceExtensions(
        const std::vector<VkExtensionProperties>& available,
        std::string& missing) const;

    /// Returns the minimum queue count requested for one queue role.
    [[nodiscard]] uint32_t requiredQueueCount(RhiQueueType queue) const;

private:
    [[nodiscard]] static bool containsExtension(
        const std::vector<VkExtensionProperties>& available,
        const std::string& required);

    std::vector<std::string> m_instanceExtensions;
    std::vector<std::string> m_deviceExtensions;
    uint32_t m_graphicsQueueCount = 0u;
    uint32_t m_computeQueueCount = 0u;
    uint32_t m_transferQueueCount = 0u;
    uint32_t m_presentQueueCount = 0u;
};

#endif // MECRAFT_VULKAN_REQUIREMENT_COLLECTOR_H
