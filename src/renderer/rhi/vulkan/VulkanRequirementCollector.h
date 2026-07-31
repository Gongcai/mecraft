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

    /// Adds a Vulkan 1.2 feature name required in the device feature chain.
    /// @param feature Field name from VkPhysicalDeviceVulkan12Features.
    void requireVulkan12Feature(const char* feature);

    /// Adds a Vulkan 1.3 feature name required in the device feature chain.
    /// @param feature Field name from VkPhysicalDeviceVulkan13Features.
    void requireVulkan13Feature(const char* feature);

    /// Records the minimum queue count required for a logical queue role.
    /// @param queue Queue role requested by the RHI or an SDK integration.
    /// @param count Minimum number of queues for the role.
    void requireQueue(RhiQueueType queue, uint32_t count = 1u);

    /// Adds SDK-owned queues after the queues reserved by the RHI.
    /// @param queue Logical queue role used by the SDK.
    /// @param count Number of additional queues required by the SDK.
    void requireAdditionalQueues(RhiQueueType queue, uint32_t count);

    /// Adds native Vulkan optical-flow queues required by frame generation.
    /// @param count Number of queues required from an exclusive optical-flow family.
    void requireOpticalFlowQueues(uint32_t count);

    /// Returns the collected instance extension names as pointers usable by Vulkan.
    [[nodiscard]] std::vector<const char*> instanceExtensionNames() const;

    /// Returns the collected device extension names as pointers usable by Vulkan.
    [[nodiscard]] std::vector<const char*> deviceExtensionNames() const;

    /// Returns Vulkan 1.2 feature field names requested by integrations.
    [[nodiscard]] std::vector<const char*> vulkan12FeatureNames() const;

    /// Returns Vulkan 1.3 feature field names requested by integrations.
    [[nodiscard]] std::vector<const char*> vulkan13FeatureNames() const;

    /// Checks collected instance extensions against enumerated driver properties.
    /// @param available Enumerated instance extension properties.
    /// @param missing Receives the first missing extension name.
    [[nodiscard]] bool validateInstanceExtensions(const std::vector<VkExtensionProperties>& available,
                                                  std::string& missing) const;

    /// Checks collected device extensions against enumerated driver properties.
    /// @param available Enumerated device extension properties.
    /// @param missing Receives the first missing extension name.
    [[nodiscard]] bool validateDeviceExtensions(const std::vector<VkExtensionProperties>& available,
                                                std::string& missing) const;

    /// Returns the minimum queue count requested for one queue role.
    [[nodiscard]] uint32_t requiredQueueCount(RhiQueueType queue) const;

    /// Returns SDK queues appended after the RHI-owned queue range.
    [[nodiscard]] uint32_t additionalQueueCount(RhiQueueType queue) const;

    /// Returns the required native optical-flow queue count.
    [[nodiscard]] uint32_t opticalFlowQueueCount() const;

private:
    [[nodiscard]] static bool containsExtension(const std::vector<VkExtensionProperties>& available,
                                                const std::string& required);

    std::vector<std::string> m_instanceExtensions;
    std::vector<std::string> m_deviceExtensions;
    std::vector<std::string> m_vulkan12Features;
    std::vector<std::string> m_vulkan13Features;
    uint32_t m_graphicsQueueCount = 0u;
    uint32_t m_computeQueueCount = 0u;
    uint32_t m_transferQueueCount = 0u;
    uint32_t m_presentQueueCount = 0u;
    uint32_t m_additionalGraphicsQueueCount = 0u;
    uint32_t m_additionalComputeQueueCount = 0u;
    uint32_t m_additionalTransferQueueCount = 0u;
    uint32_t m_additionalPresentQueueCount = 0u;
    uint32_t m_opticalFlowQueueCount = 0u;
};

#endif // MECRAFT_VULKAN_REQUIREMENT_COLLECTOR_H
