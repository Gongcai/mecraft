#ifndef MECRAFT_VK_RHI_INTEROP_H
#define MECRAFT_VK_RHI_INTEROP_H

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiResources.h"

#include <vulkan/vulkan.h>

#include <optional>

class VkRhiDevice;

/// Native Vulkan device objects exposed only to Vulkan-specific SDK bridges.
struct VkRhiDeviceInteropInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t transferQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    uint32_t presentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
};

/// Native Vulkan image metadata resolved from validated RHI handles.
struct VkRhiTextureInteropInfo {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0u;
    VkImageType imageType = VK_IMAGE_TYPE_MAX_ENUM;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    uint32_t mipLevels = 0u;
    uint32_t arrayLayers = 0u;
    uint32_t baseMip = 0u;
    uint32_t mipCount = 0u;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = 0u;
    VkImageAspectFlags aspectMask = 0u;
};

/// Controlled native-object access for Vulkan-only renderer integrations.
class VkRhiInterop final {
public:
    [[nodiscard]] static std::optional<VkRhiDeviceInteropInfo> deviceInfo(
        const VkRhiDevice& device);
    [[nodiscard]] static std::optional<VkRhiTextureInteropInfo> textureInfo(
        const VkRhiDevice& device,
        RhiTextureHandle texture,
        RhiTextureViewHandle view);
    [[nodiscard]] static std::optional<VkCommandBuffer> commandBuffer(
        const VkRhiDevice& device,
        const RhiCommandList& commandList);
    /// Queues one SDK-owned timeline semaphore wait before the next graphics submission.
    /// @param device Vulkan RHI device receiving the external dependency.
    /// @param semaphore Opaque Vulkan timeline semaphore returned by the SDK.
    /// @param value Timeline value that must complete before frame resources are reused.
    /// @return True when the dependency was recorded for the next graphics submission.
    [[nodiscard]] static bool queueExternalTimelineWait(
        VkRhiDevice& device,
        void* semaphore,
        uint64_t value);
    /// Recreates the main swapchain across a Streamline DLSS-G load-state change.
    /// @param device Vulkan RHI device that owns the main swapchain.
    /// @param frameGenerationLoaded Desired DLSS-G plugin state for the new swapchain.
    /// @return True when the old swapchain was destroyed and the new one was created.
    [[nodiscard]] static bool recreateFrameGenerationSwapchain(
        VkRhiDevice& device,
        bool frameGenerationLoaded);
    [[nodiscard]] static VkPipelineStageFlags2 resourceStages(RhiResourceState state);
    [[nodiscard]] static VkAccessFlags2 resourceAccess(RhiResourceState state);
    [[nodiscard]] static VkImageLayout resourceLayout(RhiResourceState state);
};

#endif // MECRAFT_VK_RHI_INTEROP_H
