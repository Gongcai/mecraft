#ifndef MECRAFT_STREAMLINE_RUNTIME_H
#define MECRAFT_STREAMLINE_RUNTIME_H

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class VulkanRequirementCollector;

/// Aggregated Vulkan requirements returned by all enabled Streamline features.
struct StreamlineVulkanRequirements {
    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::vector<std::string> features12;
    std::vector<std::string> features13;
    uint32_t additionalGraphicsQueues = 0u;
    uint32_t additionalComputeQueues = 0u;
    uint32_t opticalFlowQueues = 0u;
    bool hardwareSchedulingRequired = false;
    bool vsyncOffRequired = false;
};

/// Native Vulkan objects and Streamline-owned queue ranges passed after device creation.
struct StreamlineVulkanDeviceInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t graphicsQueueIndex = 0u;
    uint32_t computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t computeQueueIndex = 0u;
    uint32_t opticalFlowQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t opticalFlowQueueIndex = 0u;
    bool useNativeOpticalFlow = false;
};

/// Owns the process-wide Streamline runtime used by the Windows Vulkan backend.
class StreamlineRuntime final {
public:
    [[nodiscard]] static StreamlineRuntime& instance();

    /// Verifies and loads the signed Streamline runtime, then queries feature requirements.
    /// @param runtimeDirectory Absolute directory containing the production Streamline DLLs.
    /// @return True when every requested feature initialized and exposed Vulkan requirements.
    bool initialize(const std::filesystem::path& runtimeDirectory = {});

    /// Adds queried Streamline extensions, features, and queue counts to Vulkan planning.
    /// @param collector Collector used by the Vulkan backend before instance creation.
    [[nodiscard]] bool appendVulkanRequirements(VulkanRequirementCollector& collector) const;

    /// Provides the created Vulkan instance, device, and reserved queue ranges to Streamline.
    /// @param info Native Vulkan objects and queue indices reserved for Streamline.
    /// @return True when Streamline accepted the Vulkan device configuration.
    bool setVulkanDevice(const StreamlineVulkanDeviceInfo& info);

    /// Shuts Streamline down while the Vulkan device and instance are still alive.
    /// @return True when shutdown completed successfully or the runtime was not initialized.
    bool shutdown();

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] bool vulkanDeviceSet() const;
    [[nodiscard]] const StreamlineVulkanRequirements& vulkanRequirements() const;
    [[nodiscard]] const std::string& lastError() const;

private:
    struct Implementation;

    StreamlineRuntime();
    ~StreamlineRuntime();
    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    std::unique_ptr<Implementation> m_impl;
};

#endif // MECRAFT_STREAMLINE_RUNTIME_H
