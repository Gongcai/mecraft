#ifndef MECRAFT_STREAMLINE_RUNTIME_H
#define MECRAFT_STREAMLINE_RUNTIME_H

#include <vulkan/vulkan.h>

#include <array>
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

enum class StreamlineDlssMode {
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

struct StreamlineDlssOptimalSettings {
    uint32_t renderWidth = 0u;
    uint32_t renderHeight = 0u;
    uint32_t renderWidthMin = 0u;
    uint32_t renderHeightMin = 0u;
    uint32_t renderWidthMax = 0u;
    uint32_t renderHeightMax = 0u;
};

struct StreamlineDlssOptions {
    StreamlineDlssMode mode = StreamlineDlssMode::Quality;
    uint32_t outputWidth = 0u;
    uint32_t outputHeight = 0u;
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
};

struct StreamlineDlssResource {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0u;
    VkImageAspectFlags aspectMask = 0u;
    uint32_t mipLevels = 0u;
    uint32_t arrayLayers = 0u;
    uint32_t baseMip = 0u;
    uint32_t mipCount = 0u;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 0u;
};

struct StreamlineDlssFrameConstants {
    std::array<float, 16u> cameraViewToClip{};
    std::array<float, 16u> clipToCameraView{};
    std::array<float, 16u> clipToPrevClip{};
    std::array<float, 16u> prevClipToClip{};
    std::array<float, 2u> jitterOffset{};
    std::array<float, 2u> motionVectorScale{};
    std::array<float, 3u> cameraPosition{};
    std::array<float, 3u> cameraUp{};
    std::array<float, 3u> cameraRight{};
    std::array<float, 3u> cameraForward{};
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float verticalFovRadians = 0.0f;
    float cameraAspectRatio = 1.0f;
    bool depthInverted = false;
    bool reset = true;
};

struct StreamlineDlssDispatchInfo {
    uint64_t frameIndex = 0u;
    uint32_t viewport = 0u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    StreamlineDlssFrameConstants constants;
    StreamlineDlssResource inputColor;
    StreamlineDlssResource outputColor;
    StreamlineDlssResource depth;
    StreamlineDlssResource motionVectors;
    StreamlineDlssResource exposure;
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

    /// Queries the DLSS render extent selected by the loaded NVIDIA model.
    /// @param options Output dimensions and project quality mode.
    /// @param settings Receives the optimal and dynamic-resolution bounds.
    /// @return True when the DLSS plugin returned valid settings.
    bool queryDlssOptimalSettings(
        const StreamlineDlssOptions& options,
        StreamlineDlssOptimalSettings& settings);

    /// Configures DLSS Super Resolution for one Streamline viewport.
    /// @param viewport Stable viewport identifier shared by all DLSS calls.
    /// @param options Output dimensions, quality mode, and HDR exposure values.
    /// @return True when the DLSS plugin accepted the options.
    bool configureDlss(uint32_t viewport, const StreamlineDlssOptions& options);

    /// Tags Vulkan resources, provides common constants, and evaluates DLSS.
    /// @param info Complete frame-scoped Vulkan and temporal reconstruction data.
    /// @return True when every Streamline call completed successfully.
    bool evaluateDlss(const StreamlineDlssDispatchInfo& info);

    /// Releases DLSS resources owned by one Streamline viewport.
    /// @param viewport Stable viewport identifier used during configuration.
    /// @return True when the plugin released the viewport resources.
    bool releaseDlssResources(uint32_t viewport);

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
