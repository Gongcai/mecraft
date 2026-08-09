#include "renderer/rhi/vulkan/VkRhiDevice.h"

#include "Diagnostics.h"
#include "renderer/rhi/RhiHandleAllocator.h"
#include "renderer/rhi/RhiShaderCompiler.h"
#include "renderer/rhi/vulkan/VkRhiCommandList.h"
#include "renderer/rhi/vulkan/VkRhiConversions.h"
#include "renderer/rhi/vulkan/VkRhiInterop.h"
#include "renderer/rhi/vulkan/VulkanRequirementCollector.h"
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "renderer/upscaling/StreamlineRuntime.h"
#include <sl_helpers_vk.h>
#endif

#include <GLFW/glfw3.h>
#if defined(MECRAFT_ENABLE_STREAMLINE) && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <Windows.h>
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace renderer::rhi::vulkan;

struct QueueFamilies {
    uint32_t graphics = UINT32_MAX;
    uint32_t compute = UINT32_MAX;
    uint32_t transfer = UINT32_MAX;
    uint32_t present = UINT32_MAX;
    uint32_t opticalFlow = UINT32_MAX;

    [[nodiscard]] bool complete(const bool requireOpticalFlow) const {
        return graphics != UINT32_MAX && compute != UINT32_MAX && transfer != UINT32_MAX && present != UINT32_MAX &&
               (!requireOpticalFlow || opticalFlow != UINT32_MAX);
    }
};

namespace {

std::atomic<uint64_t> g_nextVkRhiDeviceId{1u};

template <typename Handle> [[nodiscard]] uint64_t handleKey(const Handle handle) {
    return (static_cast<uint64_t>(handle.generation) << 32u) | handle.index;
}

void logVkError(const char* operation, const VkResult result) {
    std::cerr << "VkRhiDevice: " << operation << " failed with VkResult " << static_cast<int32_t>(result) << '\n';
}

void logRhiError(const char* message) {
    std::cerr << "VkRhiDevice: " << message << '\n';
}

[[nodiscard]] bool containsDeviceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

[[nodiscard]] bool vkSucceeded(const VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    logVkError(operation, result);
    return false;
}

[[nodiscard]] renderer::rhi::vulkan::VkResourceStateMapping
toVkCommandResourceState(const RhiResourceState state, const RhiCommandListType commandListType) {
    auto mapping = renderer::rhi::vulkan::toVkResourceState(state);
    if (commandListType != RhiCommandListType::Compute)
        return mapping;
    switch (state) {
    case RhiResourceState::DepthRead:
        mapping.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mapping.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    case RhiResourceState::ShaderRead:
    case RhiResourceState::ShaderWrite:
    case RhiResourceState::UniformBuffer:
    case RhiResourceState::StorageBuffer: mapping.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; break;
    case RhiResourceState::AccelerationStructureRead:
        mapping.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                         VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
        break;
    default: break;
    }
    return mapping;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        if (userData != nullptr) {
            static_cast<std::atomic<uint64_t>*>(userData)->fetch_add(1u, std::memory_order_relaxed);
        }
        std::cerr << "Vulkan validation: " << (data != nullptr && data->pMessage != nullptr ? data->pMessage : "")
                  << '\n';
    }
    return VK_FALSE;
}

[[nodiscard]] VkImageType toVkImageType(const RhiTextureDimension dimension) {
    return dimension == RhiTextureDimension::Texture3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
}

[[nodiscard]] VkImageViewType toVkImageViewType(const RhiTextureViewType type) {
    switch (type) {
    case RhiTextureViewType::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
    case RhiTextureViewType::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case RhiTextureViewType::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
    case RhiTextureViewType::Cube: return VK_IMAGE_VIEW_TYPE_CUBE;
    case RhiTextureViewType::CubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

[[nodiscard]] VkImageUsageFlags toVkImageUsage(const RhiTextureUsageFlags usage) {
    VkImageUsageFlags result = 0u;
    if ((usage & rhiFlag(RhiTextureUsage::Sampled)) != 0u)
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::Storage)) != 0u)
        result |= VK_IMAGE_USAGE_STORAGE_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::ColorAttachment)) != 0u) {
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((usage & rhiFlag(RhiTextureUsage::DepthStencilAttachment)) != 0u) {
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if ((usage & rhiFlag(RhiTextureUsage::TransferSrc)) != 0u)
        result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::TransferDst)) != 0u)
        result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
}

[[nodiscard]] VkBufferUsageFlags toVkBufferUsage(const RhiBufferUsageFlags usage) {
    VkBufferUsageFlags result = 0u;
    if ((usage & rhiFlag(RhiBufferUsage::Vertex)) != 0u)
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Index)) != 0u)
        result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Uniform)) != 0u)
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Storage)) != 0u)
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Indirect)) != 0u)
        result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::TransferSrc)) != 0u)
        result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::TransferDst)) != 0u)
        result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::DeviceAddress)) != 0u)
        result |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::AccelerationStructureStorage)) != 0u)
        result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if ((usage & rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput)) != 0u)
        result |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if ((usage & rhiFlag(RhiBufferUsage::MicromapStorage)) != 0u)
        result |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    if ((usage & rhiFlag(RhiBufferUsage::MicromapBuildInput)) != 0u)
        result |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
    return result;
}

[[nodiscard]] VkCullModeFlags toVkCullMode(const RhiCullMode mode) {
    switch (mode) {
    case RhiCullMode::None: return VK_CULL_MODE_NONE;
    case RhiCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RhiCullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

[[nodiscard]] VkFrontFace toVkFrontFace(const RhiFrontFace face) {
    return face == RhiFrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
}

[[nodiscard]] VkFilter toVkFilter(const RhiFilter filter) {
    return filter == RhiFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerMipmapMode toVkMipmapMode(const RhiMipmapMode mode) {
    return mode == RhiMipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

[[nodiscard]] VkSamplerAddressMode toVkAddressMode(const RhiAddressMode mode) {
    switch (mode) {
    case RhiAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RhiAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RhiAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case RhiAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

[[nodiscard]] VkBorderColor toVkBorderColor(const RhiBorderColor color) {
    switch (color) {
    case RhiBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case RhiBorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case RhiBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

[[nodiscard]] VkSampleCountFlagBits toVkSampleCount(const uint32_t count) {
    switch (count) {
    case 1u: return VK_SAMPLE_COUNT_1_BIT;
    case 2u: return VK_SAMPLE_COUNT_2_BIT;
    case 4u: return VK_SAMPLE_COUNT_4_BIT;
    case 8u: return VK_SAMPLE_COUNT_8_BIT;
    default: return static_cast<VkSampleCountFlagBits>(0);
    }
}

[[nodiscard]] uint32_t formatByteSize(const RhiTextureFormat format) {
    switch (format) {
    case RhiTextureFormat::R8Unorm:
    case RhiTextureFormat::R8Uint: return 1u;
    case RhiTextureFormat::Rg8Unorm:
    case RhiTextureFormat::R16Float:
    case RhiTextureFormat::R16Uint:
    case RhiTextureFormat::Depth16: return 2u;
    case RhiTextureFormat::Rgba8Unorm:
    case RhiTextureFormat::Rgba8Srgb:
    case RhiTextureFormat::Bgra8Unorm:
    case RhiTextureFormat::Bgra8Srgb:
    case RhiTextureFormat::Rgb10A2Unorm:
    case RhiTextureFormat::Rg16Float:
    case RhiTextureFormat::R32Float:
    case RhiTextureFormat::R32Uint:
    case RhiTextureFormat::Depth24:
    case RhiTextureFormat::Depth24Stencil8:
    case RhiTextureFormat::Depth32Float: return 4u;
    case RhiTextureFormat::Rg32Uint:
    case RhiTextureFormat::Rgba16Float: return 8u;
    case RhiTextureFormat::Rgba32Float: return 16u;
    case RhiTextureFormat::Undefined: break;
    }
    return 0u;
}

[[nodiscard]] bool supportsFormatFeatures(const VkPhysicalDevice physicalDevice, const VkFormat format,
                                          const VkFormatFeatureFlags2 requiredFeatures) {
    VkFormatProperties3 properties3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 properties2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &properties3};
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &properties2);
    return (properties3.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}

[[nodiscard]] QueueFamilies queryQueueFamilies(const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
                                               const bool requireOpticalFlow) {
    uint32_t count = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());

    QueueFamilies families;
    for (uint32_t i = 0u; i < count; ++i) {
        const auto flags = properties[i].queueFlags;
        if (families.graphics == UINT32_MAX && (flags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            families.graphics = i;
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) != 0u && (flags & VK_QUEUE_GRAPHICS_BIT) == 0u) {
            families.compute = i;
        }
        if (families.transfer == UINT32_MAX && (flags & VK_QUEUE_TRANSFER_BIT) != 0u &&
            (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_OPTICAL_FLOW_BIT_NV)) == 0u) {
            families.transfer = i;
        }
#if defined(VK_NV_optical_flow)
        if (requireOpticalFlow && families.opticalFlow == UINT32_MAX && (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) != 0u &&
            (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0u) {
            families.opticalFlow = i;
        }
#endif
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        if (families.present == UINT32_MAX && present == VK_TRUE) {
            families.present = i;
        }
    }
    if (families.graphics != UINT32_MAX) {
        if (families.compute == UINT32_MAX)
            families.compute = families.graphics;
        if (families.transfer == UINT32_MAX)
            families.transfer = families.graphics;
    }
    return families;
}

[[nodiscard]] std::vector<VkQueueFamilyProperties> queryQueueFamilyProperties(const VkPhysicalDevice physicalDevice) {
    uint32_t count = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());
    return properties;
}

[[nodiscard]] uint32_t requiredQueuesForFamily(const QueueFamilies& families,
                                               const VulkanRequirementCollector& requirements, const uint32_t family) {
    uint32_t count = 0u;
    if (family == families.graphics || family == families.compute || family == families.transfer ||
        family == families.present) {
        count = 1u;
    }
    if (family == families.compute) {
        count += requirements.additionalQueueCount(RhiQueueType::Compute);
    }
    if (family == families.graphics) {
        count += requirements.additionalQueueCount(RhiQueueType::Graphics);
    }
    if (family == families.opticalFlow) {
        count += requirements.opticalFlowQueueCount();
    }
    return count;
}

[[nodiscard]] bool queueRequirementsSupported(const VkPhysicalDevice physicalDevice, const QueueFamilies& families,
                                              const VulkanRequirementCollector& requirements) {
    const std::vector<VkQueueFamilyProperties> properties = queryQueueFamilyProperties(physicalDevice);
    const std::array<uint32_t, 5u> requestedFamilies{families.graphics, families.compute, families.transfer,
                                                     families.present, families.opticalFlow};
    for (const uint32_t family : requestedFamilies) {
        if (family == UINT32_MAX) {
            continue;
        }
        if (family >= properties.size() ||
            requiredQueuesForFamily(families, requirements, family) > properties[family].queueCount) {
            return false;
        }
    }
    return true;
}

#if defined(MECRAFT_ENABLE_STREAMLINE)
[[nodiscard]] bool supportsStreamlineVulkan12Features(const VkPhysicalDeviceVulkan12Features& supported,
                                                      const std::vector<const char*>& requiredNames) {
    for (const char* name : requiredNames) {
        if (std::strcmp(name, "timelineSemaphore") == 0) {
            if (supported.timelineSemaphore != VK_TRUE)
                return false;
        } else if (std::strcmp(name, "descriptorIndexing") == 0) {
            if (supported.descriptorIndexing != VK_TRUE)
                return false;
        } else if (std::strcmp(name, "bufferDeviceAddress") == 0) {
            if (supported.bufferDeviceAddress != VK_TRUE)
                return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool supportsStreamlineVulkan13Features(const VkPhysicalDeviceVulkan13Features& supported,
                                                      const std::vector<const char*>& requiredNames) {
    for (const char* name : requiredNames) {
        if (std::strcmp(name, "synchronization2") == 0) {
            if (supported.synchronization2 != VK_TRUE)
                return false;
        } else if (std::strcmp(name, "privateData") == 0) {
            if (supported.privateData != VK_TRUE)
                return false;
        } else {
            return false;
        }
    }
    return true;
}
#endif

} // namespace

#include "renderer/rhi/vulkan/VkRhiInternal.h"

std::optional<VkRhiDeviceInteropInfo> VkRhiInterop::deviceInfo(const VkRhiDevice& device) {
    if (!device.m_initialized || device.m_data == nullptr) {
        return std::nullopt;
    }
    const VkRhiDeviceData& data = *device.m_data;
    return VkRhiDeviceInteropInfo{data.instance,
                                  data.physicalDevice,
                                  data.device,
                                  data.graphicsQueue,
                                  data.computeQueue,
                                  data.transferQueue,
                                  data.presentQueue,
                                  data.queueFamilies.graphics,
                                  data.queueFamilies.compute,
                                  data.queueFamilies.transfer,
                                  data.queueFamilies.present};
}

std::optional<VkRhiTextureInteropInfo>
VkRhiInterop::textureInfo(const VkRhiDevice& device, const RhiTextureHandle texture, const RhiTextureViewHandle view) {
    if (!device.m_initialized || device.m_data == nullptr || !texture.isValid() || !view.isValid()) {
        return std::nullopt;
    }
    const std::shared_lock<std::shared_mutex> registryLock(device.m_data->resourceRegistryMutex);
    const auto* textureRecord = findRecord(device.m_data->textures, texture);
    const auto* viewRecord = findRecord(device.m_data->textureViews, view);
    if (textureRecord == nullptr || viewRecord == nullptr || viewRecord->desc.texture.index != texture.index ||
        viewRecord->desc.texture.generation != texture.generation) {
        return std::nullopt;
    }
    const RhiTextureFormat viewFormat =
        viewRecord->desc.format == RhiTextureFormat::Undefined ? textureRecord->desc.format : viewRecord->desc.format;
    const uint32_t mipCount = viewRecord->desc.mipCount == kRhiRemainingMipLevels
                                  ? textureRecord->desc.mipLevels - viewRecord->desc.baseMip
                                  : viewRecord->desc.mipCount;
    const uint32_t textureLayers =
        textureRecord->desc.dimension == RhiTextureDimension::Texture3D ? 1u : textureRecord->desc.depthOrLayers;
    const bool texture3D = textureRecord->desc.dimension == RhiTextureDimension::Texture3D;
    const uint32_t layerCount = texture3D ? 1u
                                          : (viewRecord->desc.layerCount == kRhiRemainingArrayLayers
                                                 ? textureLayers - viewRecord->desc.baseLayer
                                                 : viewRecord->desc.layerCount);
    const uint32_t viewDepth = textureRecord->desc.dimension == RhiTextureDimension::Texture3D
                                   ? std::max(1u, textureRecord->desc.depthOrLayers >> viewRecord->desc.baseMip)
                                   : 1u;
    return VkRhiTextureInteropInfo{textureRecord->image,
                                   viewRecord->view,
                                   toVkFormat(viewFormat),
                                   {std::max(1u, textureRecord->desc.width >> viewRecord->desc.baseMip),
                                    std::max(1u, textureRecord->desc.height >> viewRecord->desc.baseMip), viewDepth},
                                   toVkImageUsage(textureRecord->desc.usage),
                                   toVkImageType(textureRecord->desc.dimension),
                                   toVkImageViewType(viewRecord->desc.viewType),
                                   textureRecord->desc.mipLevels,
                                   textureLayers,
                                   viewRecord->desc.baseMip,
                                   mipCount,
                                   texture3D ? 0u : viewRecord->desc.baseLayer,
                                   layerCount,
                                   defaultAspectForFormat(viewFormat)};
}

std::optional<VkCommandBuffer> VkRhiInterop::commandBuffer(const VkRhiDevice& device,
                                                           const RhiCommandList& commandList) {
    if (!device.m_initialized || device.m_data == nullptr) {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> registryLock(device.m_data->commandRegistryMutex);
    if (device.m_data->commandLists.find(const_cast<RhiCommandList*>(&commandList)) ==
        device.m_data->commandLists.end()) {
        return std::nullopt;
    }
    const auto& vkCommandList = static_cast<const VkRhiCommandList&>(commandList);
    if (vkCommandList.state() != RhiCommandListState::Recording) {
        return std::nullopt;
    }
    const auto native = static_cast<VkCommandBuffer>(vkCommandList.nativeCommandBuffer());
    return native != VK_NULL_HANDLE ? std::optional<VkCommandBuffer>(native) : std::nullopt;
}

bool VkRhiInterop::queueExternalTimelineWait(VkRhiDevice& device, void* semaphore, const uint64_t value) {
    if (!device.m_initialized || device.m_data == nullptr || std::this_thread::get_id() != device.m_deviceThread ||
        semaphore == nullptr || value == 0u || device.m_data->externalFrameCompletionSemaphore != VK_NULL_HANDLE) {
        return false;
    }
    device.m_data->externalFrameCompletionSemaphore = reinterpret_cast<VkSemaphore>(semaphore);
    device.m_data->externalFrameCompletionValue = value;
    return true;
}

namespace {

[[nodiscard]] VkResult waitDeviceIdle(VkRhiDeviceData& data) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    return StreamlineRuntime::instance().waitVulkanDeviceIdle(data.device);
#else
    return vkDeviceWaitIdle(data.device);
#endif
}

[[nodiscard]] VkResult createMainSurface(VkRhiDeviceData& data) {
#if defined(MECRAFT_ENABLE_STREAMLINE) && defined(_WIN32)
    return StreamlineRuntime::instance().createVulkanWin32Surface(data.instance, GetModuleHandleW(nullptr),
                                                                  glfwGetWin32Window(data.window), data.surface);
#else
    return glfwCreateWindowSurface(data.instance, data.window, nullptr, &data.surface);
#endif
}

void destroyMainSurface(VkRhiDeviceData& data) {
    if (data.surface == VK_NULL_HANDLE) {
        return;
    }
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime::instance().destroyVulkanSurface(data.instance, data.surface);
#else
    vkDestroySurfaceKHR(data.instance, data.surface, nullptr);
#endif
    data.surface = VK_NULL_HANDLE;
}

[[nodiscard]] VkResult createMainSwapchain(VkRhiDeviceData& data, const VkSwapchainCreateInfoKHR& info) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    return StreamlineRuntime::instance().createVulkanSwapchain(data.device, info, data.swapchain);
#else
    return vkCreateSwapchainKHR(data.device, &info, nullptr, &data.swapchain);
#endif
}

void destroyMainSwapchain(VkRhiDeviceData& data) {
    if (data.swapchain == VK_NULL_HANDLE) {
        return;
    }
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime::instance().destroyVulkanSwapchain(data.device, data.swapchain);
#else
    vkDestroySwapchainKHR(data.device, data.swapchain, nullptr);
#endif
    data.swapchain = VK_NULL_HANDLE;
}

[[nodiscard]] VkResult queryMainSwapchainImages(VkRhiDeviceData& data, uint32_t& imageCount, VkImage* images) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    return StreamlineRuntime::instance().getVulkanSwapchainImages(data.device, data.swapchain, imageCount, images);
#else
    return vkGetSwapchainImagesKHR(data.device, data.swapchain, &imageCount, images);
#endif
}

[[nodiscard]] VkResult acquireMainSwapchainImage(VkRhiDeviceData& data, const uint64_t timeout,
                                                 const VkSemaphore semaphore, const VkFence fence,
                                                 uint32_t& imageIndex) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    return StreamlineRuntime::instance().acquireVulkanImage(data.device, data.swapchain, timeout, semaphore, fence,
                                                            imageIndex);
#else
    return vkAcquireNextImageKHR(data.device, data.swapchain, timeout, semaphore, fence, &imageIndex);
#endif
}

template <typename DeferredQueue, typename DeferredItem> void enqueueDeferred(DeferredQueue& queue, DeferredItem item) {
    const auto insertion =
        std::upper_bound(queue.begin(), queue.end(), item.sequence,
                         [](const uint64_t sequence, const auto& queued) { return sequence < queued.sequence; });
    queue.insert(insertion, std::move(item));
}

struct NativeAccelerationStructureBuildInput {
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<uint32_t> primitiveCounts;
};

struct NativeMicromapBuildInput {
    std::vector<VkMicromapUsageEXT> usages;
    VkMicromapBuildInfoEXT info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
};

[[nodiscard]] bool rangeFits(const uint64_t totalSize, const uint64_t offset, const uint64_t size) {
    return size != 0u && offset <= totalSize && size <= totalSize - offset;
}

[[nodiscard]] bool rangesOverlap(const uint64_t lhsOffset, const uint64_t lhsSize, const uint64_t rhsOffset,
                                 const uint64_t rhsSize) {
    return lhsOffset <= rhsOffset ? lhsSize > rhsOffset - lhsOffset : rhsSize > lhsOffset - rhsOffset;
}

[[nodiscard]] bool checkedAdd(const uint64_t lhs, const uint64_t rhs, uint64_t& result) {
    if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checkedMultiply(const uint64_t lhs, const uint64_t rhs, uint64_t& result) {
    if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] bool stridedRangeFits(const uint64_t totalSize, const uint64_t offset, const uint64_t stride,
                                    const uint64_t elementSize, const uint64_t elementCount) {
    if (elementCount == 0u || stride < elementSize ||
        elementCount - 1u > (std::numeric_limits<uint64_t>::max() - elementSize) / stride) {
        return false;
    }
    return rangeFits(totalSize, offset, (elementCount - 1u) * stride + elementSize);
}

[[nodiscard]] bool bufferHasUsages(const VkRhiDeviceData::Buffer& buffer, const RhiBufferUsageFlags usages) {
    return (buffer.desc.usage & usages) == usages;
}

[[nodiscard]] VkDeviceAddress nativeBufferDeviceAddress(const VkRhiDeviceData& data,
                                                        const VkRhiDeviceData::Buffer& buffer) {
    VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addressInfo.buffer = buffer.buffer;
    return vkGetBufferDeviceAddress(data.device, &addressInfo);
}

[[nodiscard]] bool deviceAddressAtOffset(const VkDeviceAddress base, const uint64_t offset, VkDeviceAddress& address) {
    return base != 0u && checkedAdd(base, offset, address);
}

[[nodiscard]] bool fillNativeAccelerationStructureBuildInput(const VkRhiDeviceData& data,
                                                             const RhiCapabilities& capabilities,
                                                             const RhiAccelerationStructureBuildInput& input,
                                                             NativeAccelerationStructureBuildInput& native,
                                                             VkRhiCommandResourceReferences* references) {
    constexpr RhiAccelerationStructureBuildFlags kKnownBuildFlags =
        rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate) |
        rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
        rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace) |
        rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild);
    constexpr RhiAccelerationStructureGeometryFlags kKnownGeometryFlags =
        rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque) |
        rhiFlag(RhiAccelerationStructureGeometryFlag::NoDuplicateAnyHitInvocation);
    const bool conflictingPreferenceFlags =
        (input.flags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace)) != 0u &&
        (input.flags & rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastBuild)) != 0u;
    if (input.geometries == nullptr || input.ranges == nullptr || input.geometryCount == 0u ||
        input.geometryCount > capabilities.maxAccelerationStructureGeometryCount ||
        (input.flags & ~kKnownBuildFlags) != 0u || conflictingPreferenceFlags ||
        toVkAccelerationStructureType(input.type) == VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR ||
        (input.type == RhiAccelerationStructureType::TopLevel && input.geometryCount != 1u)) {
        return false;
    }

    native.geometries.resize(input.geometryCount);
    native.ranges.resize(input.geometryCount);
    native.primitiveCounts.resize(input.geometryCount);
    uint64_t totalPrimitiveCount = 0u;
    for (uint32_t index = 0u; index < input.geometryCount; ++index) {
        const RhiAccelerationStructureGeometryDesc& geometry = input.geometries[index];
        const RhiAccelerationStructureBuildRangeDesc& range = input.ranges[index];
        if ((geometry.flags & ~kKnownGeometryFlags) != 0u || range.primitiveCount == 0u ||
            range.primitiveCount > capabilities.maxAccelerationStructurePrimitiveCount ||
            totalPrimitiveCount > capabilities.maxAccelerationStructurePrimitiveCount - range.primitiveCount) {
            return false;
        }
        totalPrimitiveCount += range.primitiveCount;

        VkAccelerationStructureGeometryKHR& nativeGeometry = native.geometries[index];
        nativeGeometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        nativeGeometry.flags = toVkAccelerationStructureGeometryFlags(geometry.flags);
        native.ranges[index] = {range.primitiveCount, range.primitiveOffset, range.firstVertex, range.transformOffset};
        native.primitiveCounts[index] = range.primitiveCount;

        switch (geometry.type) {
        case RhiAccelerationStructureGeometryType::Triangles: {
            if (input.type != RhiAccelerationStructureType::BottomLevel ||
                geometry.triangles.vertexFormat != RhiVertexFormat::Float3 || geometry.triangles.vertexCount == 0u ||
                geometry.triangles.vertexStride < sizeof(float) * 3u ||
                geometry.triangles.vertexStride % sizeof(float) != 0u ||
                geometry.triangles.vertexStride > std::numeric_limits<uint32_t>::max()) {
                return false;
            }
            const auto* vertexBuffer = findRecord(data.buffers, geometry.triangles.vertexBuffer);
            constexpr RhiBufferUsageFlags kBuildInputUsages =
                rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
            if (vertexBuffer == nullptr || !bufferHasUsages(*vertexBuffer, kBuildInputUsages) ||
                !stridedRangeFits(vertexBuffer->desc.size, geometry.triangles.vertexOffset,
                                  geometry.triangles.vertexStride, sizeof(float) * 3u,
                                  geometry.triangles.vertexCount)) {
                return false;
            }
            VkDeviceAddress vertexDataAddress = 0u;
            if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *vertexBuffer), geometry.triangles.vertexOffset,
                                       vertexDataAddress) ||
                vertexDataAddress % alignof(float) != 0u) {
                return false;
            }

            VkAccelerationStructureGeometryTrianglesDataKHR triangles{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
            triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            triangles.vertexData.deviceAddress = vertexDataAddress;
            triangles.vertexStride = geometry.triangles.vertexStride;
            triangles.maxVertex = geometry.triangles.vertexCount - 1u;
            triangles.indexType = toVkAccelerationStructureIndexType(geometry.triangles.indexFormat);
            if (triangles.indexType == VK_INDEX_TYPE_MAX_ENUM) {
                return false;
            }

            if (geometry.triangles.indexFormat == RhiAccelerationStructureIndexFormat::None) {
                const uint64_t vertexPrimitiveCount = static_cast<uint64_t>(range.primitiveCount) * 3u;
                uint64_t firstVertexOffset = 0u;
                uint64_t primitiveDataOffset = 0u;
                uint64_t completeVertexOffset = 0u;
                if (geometry.triangles.indexBuffer.isValid() || geometry.triangles.indexOffset != 0u ||
                    vertexPrimitiveCount / 3u != range.primitiveCount || range.primitiveOffset % sizeof(float) != 0u ||
                    vertexPrimitiveCount > geometry.triangles.vertexCount ||
                    range.firstVertex > geometry.triangles.vertexCount - vertexPrimitiveCount ||
                    !checkedMultiply(range.firstVertex, geometry.triangles.vertexStride, firstVertexOffset) ||
                    !checkedAdd(geometry.triangles.vertexOffset, range.primitiveOffset, primitiveDataOffset) ||
                    !checkedAdd(primitiveDataOffset, firstVertexOffset, completeVertexOffset) ||
                    !stridedRangeFits(vertexBuffer->desc.size, completeVertexOffset, geometry.triangles.vertexStride,
                                      sizeof(float) * 3u, vertexPrimitiveCount)) {
                    return false;
                }
            } else {
                const auto* indexBuffer = findRecord(data.buffers, geometry.triangles.indexBuffer);
                const uint64_t indexSize = geometry.triangles.indexFormat == RhiAccelerationStructureIndexFormat::Uint16
                                               ? sizeof(uint16_t)
                                               : sizeof(uint32_t);
                const uint64_t indexCount = static_cast<uint64_t>(range.primitiveCount) * 3u;
                uint64_t completeIndexOffset = 0u;
                if (indexBuffer == nullptr || !bufferHasUsages(*indexBuffer, kBuildInputUsages) ||
                    indexCount / 3u != range.primitiveCount || range.primitiveOffset % indexSize != 0u ||
                    range.firstVertex >= geometry.triangles.vertexCount ||
                    !checkedAdd(geometry.triangles.indexOffset, range.primitiveOffset, completeIndexOffset) ||
                    !rangeFits(indexBuffer->desc.size, completeIndexOffset, indexCount * indexSize)) {
                    return false;
                }
                VkDeviceAddress indexDataAddress = 0u;
                if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *indexBuffer),
                                           geometry.triangles.indexOffset, indexDataAddress) ||
                    indexDataAddress % indexSize != 0u) {
                    return false;
                }
                triangles.indexData.deviceAddress = indexDataAddress;
                if (references != nullptr) {
                    references->reference(geometry.triangles.indexBuffer);
                }
            }

            if (geometry.triangles.transformBuffer.isValid()) {
                const auto* transformBuffer = findRecord(data.buffers, geometry.triangles.transformBuffer);
                uint64_t completeTransformOffset = 0u;
                if (range.transformOffset % (sizeof(float) * 4u) != 0u || transformBuffer == nullptr ||
                    !bufferHasUsages(*transformBuffer, kBuildInputUsages) ||
                    !checkedAdd(geometry.triangles.transformOffset, range.transformOffset, completeTransformOffset) ||
                    !rangeFits(transformBuffer->desc.size, completeTransformOffset, sizeof(float) * 12u)) {
                    return false;
                }
                VkDeviceAddress transformDataAddress = 0u;
                if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *transformBuffer),
                                           geometry.triangles.transformOffset, transformDataAddress) ||
                    transformDataAddress % (sizeof(float) * 4u) != 0u) {
                    return false;
                }
                triangles.transformData.deviceAddress = transformDataAddress;
                if (references != nullptr) {
                    references->reference(geometry.triangles.transformBuffer);
                }
            } else if (geometry.triangles.transformOffset != 0u || range.transformOffset != 0u) {
                return false;
            }

            nativeGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            nativeGeometry.geometry.triangles = triangles;
            if (references != nullptr) {
                references->reference(geometry.triangles.vertexBuffer);
            }
            break;
        }
        case RhiAccelerationStructureGeometryType::Aabbs: {
            if (input.type != RhiAccelerationStructureType::BottomLevel || range.firstVertex != 0u ||
                range.transformOffset != 0u || geometry.aabbs.stride < sizeof(float) * 6u ||
                geometry.aabbs.stride % 8u != 0u) {
                return false;
            }
            const auto* buffer = findRecord(data.buffers, geometry.aabbs.buffer);
            constexpr RhiBufferUsageFlags kBuildInputUsages =
                rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
            uint64_t completeOffset = 0u;
            if (buffer == nullptr || !bufferHasUsages(*buffer, kBuildInputUsages) ||
                !checkedAdd(geometry.aabbs.offset, range.primitiveOffset, completeOffset) ||
                !stridedRangeFits(buffer->desc.size, completeOffset, geometry.aabbs.stride, sizeof(float) * 6u,
                                  range.primitiveCount)) {
                return false;
            }
            VkDeviceAddress dataAddress = 0u;
            if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *buffer), geometry.aabbs.offset, dataAddress) ||
                dataAddress % 8u != 0u || range.primitiveOffset % 8u != 0u) {
                return false;
            }
            VkAccelerationStructureGeometryAabbsDataKHR aabbs{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR};
            aabbs.data.deviceAddress = dataAddress;
            aabbs.stride = geometry.aabbs.stride;
            nativeGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            nativeGeometry.geometry.aabbs = aabbs;
            if (references != nullptr) {
                references->reference(geometry.aabbs.buffer);
            }
            break;
        }
        case RhiAccelerationStructureGeometryType::Instances: {
            if (input.type != RhiAccelerationStructureType::TopLevel || range.firstVertex != 0u ||
                range.transformOffset != 0u ||
                range.primitiveCount > capabilities.maxAccelerationStructureInstanceCount) {
                return false;
            }
            const auto* buffer = findRecord(data.buffers, geometry.instances.buffer);
            constexpr RhiBufferUsageFlags kBuildInputUsages =
                rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
            const uint64_t instanceStride =
                geometry.instances.arrayOfPointers ? sizeof(uint64_t) : sizeof(RhiAccelerationStructureInstance);
            const uint64_t requiredAlignment = geometry.instances.arrayOfPointers ? sizeof(uint64_t) : 16u;
            uint64_t completeOffset = 0u;
            if (buffer == nullptr || !bufferHasUsages(*buffer, kBuildInputUsages) ||
                !checkedAdd(geometry.instances.offset, range.primitiveOffset, completeOffset) ||
                !rangeFits(buffer->desc.size, completeOffset,
                           static_cast<uint64_t>(range.primitiveCount) * instanceStride)) {
                return false;
            }
            VkDeviceAddress dataAddress = 0u;
            if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *buffer), geometry.instances.offset,
                                       dataAddress) ||
                dataAddress % requiredAlignment != 0u || range.primitiveOffset % 16u != 0u) {
                return false;
            }
            VkAccelerationStructureGeometryInstancesDataKHR instances{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
            instances.arrayOfPointers = geometry.instances.arrayOfPointers ? VK_TRUE : VK_FALSE;
            instances.data.deviceAddress = dataAddress;
            nativeGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            nativeGeometry.geometry.instances = instances;
            if (references != nullptr) {
                references->reference(geometry.instances.buffer);
            }
            break;
        }
        }
    }
    return true;
}

[[nodiscard]] VkOpacityMicromapFormatEXT toVkOpacityMicromapFormat(const RhiOpacityMicromapFormat format) {
    switch (format) {
    case RhiOpacityMicromapFormat::TwoState: return VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    case RhiOpacityMicromapFormat::FourState: return VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
    }
    return VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
}

[[nodiscard]] VkBuildMicromapFlagsEXT toVkMicromapBuildFlags(const RhiMicromapBuildFlags flags) {
    VkBuildMicromapFlagsEXT result = 0u;
    if ((flags & rhiFlag(RhiMicromapBuildFlag::PreferFastTrace)) != 0u)
        result |= VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    if ((flags & rhiFlag(RhiMicromapBuildFlag::PreferFastBuild)) != 0u)
        result |= VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;
    if ((flags & rhiFlag(RhiMicromapBuildFlag::AllowCompaction)) != 0u)
        result |= VK_BUILD_MICROMAP_ALLOW_COMPACTION_BIT_EXT;
    return result;
}

[[nodiscard]] bool fillNativeMicromapBuildInput(const VkRhiDeviceData& data, const RhiCapabilities& capabilities,
                                                const RhiMicromapBuildInput& input, NativeMicromapBuildInput& native,
                                                VkRhiCommandResourceReferences* references) {
    constexpr RhiMicromapBuildFlags kKnownFlags = rhiFlag(RhiMicromapBuildFlag::PreferFastTrace) |
                                                  rhiFlag(RhiMicromapBuildFlag::PreferFastBuild) |
                                                  rhiFlag(RhiMicromapBuildFlag::AllowCompaction);
    const bool conflictingPreferences = (input.flags & rhiFlag(RhiMicromapBuildFlag::PreferFastTrace)) != 0u &&
                                        (input.flags & rhiFlag(RhiMicromapBuildFlag::PreferFastBuild)) != 0u;
    if (!capabilities.opacityMicromap || input.usages == nullptr || input.usageCount == 0u ||
        (input.flags & ~kKnownFlags) != 0u || conflictingPreferences || input.opacityDataOffset % 256u != 0u ||
        input.triangleOffset % 256u != 0u || input.triangleStride < sizeof(VkMicromapTriangleEXT) ||
        input.triangleStride % sizeof(uint32_t) != 0u) {
        return false;
    }

    const auto* opacityData = findRecord(data.buffers, input.opacityDataBuffer);
    const auto* triangles = findRecord(data.buffers, input.triangleBuffer);
    constexpr RhiBufferUsageFlags kBuildInputUsages =
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::MicromapBuildInput);
    if (opacityData == nullptr || triangles == nullptr || !bufferHasUsages(*opacityData, kBuildInputUsages) ||
        !bufferHasUsages(*triangles, kBuildInputUsages) || input.opacityDataOffset >= opacityData->desc.size ||
        input.triangleOffset >= triangles->desc.size) {
        return false;
    }

    native.usages.resize(input.usageCount);
    uint64_t triangleCount = 0u;
    for (uint32_t index = 0u; index < input.usageCount; ++index) {
        const RhiOpacityMicromapUsageDesc& usage = input.usages[index];
        const bool formatValid =
            usage.format == RhiOpacityMicromapFormat::TwoState || usage.format == RhiOpacityMicromapFormat::FourState;
        const uint32_t maximumLevel = usage.format == RhiOpacityMicromapFormat::TwoState
                                          ? capabilities.maxOpacityMicromapTwoStateSubdivisionLevel
                                          : capabilities.maxOpacityMicromapFourStateSubdivisionLevel;
        if (!formatValid || usage.count == 0u || usage.subdivisionLevel > maximumLevel ||
            triangleCount > std::numeric_limits<uint32_t>::max() - usage.count) {
            return false;
        }
        triangleCount += usage.count;
        native.usages[index] = {usage.count, usage.subdivisionLevel,
                                static_cast<uint32_t>(toVkOpacityMicromapFormat(usage.format))};
    }
    uint64_t triangleBytes = 0u;
    if (!checkedMultiply(triangleCount - 1u, input.triangleStride, triangleBytes) ||
        !checkedAdd(triangleBytes, sizeof(VkMicromapTriangleEXT), triangleBytes) ||
        !rangeFits(triangles->desc.size, input.triangleOffset, triangleBytes)) {
        return false;
    }

    VkDeviceAddress opacityAddress = 0u;
    VkDeviceAddress triangleAddress = 0u;
    if (!deviceAddressAtOffset(nativeBufferDeviceAddress(data, *opacityData), input.opacityDataOffset,
                               opacityAddress) ||
        !deviceAddressAtOffset(nativeBufferDeviceAddress(data, *triangles), input.triangleOffset, triangleAddress) ||
        opacityAddress % 256u != 0u || triangleAddress % 256u != 0u) {
        return false;
    }
    native.info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    native.info.flags = toVkMicromapBuildFlags(input.flags);
    native.info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    native.info.usageCountsCount = input.usageCount;
    native.info.pUsageCounts = native.usages.data();
    native.info.data.deviceAddress = opacityAddress;
    native.info.triangleArray.deviceAddress = triangleAddress;
    native.info.triangleArrayStride = input.triangleStride;
    if (references != nullptr) {
        references->reference(input.opacityDataBuffer);
        references->reference(input.triangleBuffer);
    }
    return true;
}

[[nodiscard]] bool resolveResourceReferences(VkRhiDeviceData& data, VkRhiCommandResourceReferences& references) {
    for (size_t index = 0u; index < references.pipelines.size(); ++index) {
        const auto* pipeline = findRecord(data.pipelines, references.pipelines[index]);
        if (pipeline == nullptr)
            return false;
        references.reference(pipeline->layoutHandle);
    }
    for (size_t index = 0u; index < references.pipelineLayouts.size(); ++index) {
        const auto* layout = findRecord(data.pipelineLayouts, references.pipelineLayouts[index]);
        if (layout == nullptr)
            return false;
        for (const RhiBindGroupLayoutHandle bindGroupLayout : layout->desc.bindGroupLayouts) {
            references.reference(bindGroupLayout);
        }
    }
    for (size_t index = 0u; index < references.bindGroups.size(); ++index) {
        const auto* bindGroup = findRecord(data.bindGroups, references.bindGroups[index]);
        if (bindGroup == nullptr)
            return false;
        references.reference(bindGroup->layoutHandle);
        for (const RhiBindGroupEntry& entry : bindGroup->desc.entries) {
            references.reference(entry.resource.buffer.buffer);
            references.reference(entry.resource.textureView);
            references.reference(entry.resource.sampler);
            references.reference(entry.resource.combinedTextureSampler.textureView);
            references.reference(entry.resource.combinedTextureSampler.sampler);
            references.reference(entry.resource.accelerationStructure);
        }
    }
    for (size_t index = 0u; index < references.accelerationStructures.size(); ++index) {
        const auto* accelerationStructure =
            findRecord(data.accelerationStructures, references.accelerationStructures[index]);
        if (accelerationStructure == nullptr)
            return false;
        references.reference(accelerationStructure->desc.buffer);
    }
    for (size_t index = 0u; index < references.micromaps.size(); ++index) {
        const auto* micromap = findRecord(data.micromaps, references.micromaps[index]);
        if (micromap == nullptr)
            return false;
        references.reference(micromap->desc.buffer);
    }
    for (size_t index = 0u; index < references.textureViews.size(); ++index) {
        const auto* view = findRecord(data.textureViews, references.textureViews[index]);
        if (view == nullptr)
            return false;
        references.reference(view->desc.texture);
    }
    const auto allExist = [](const auto& handles, const auto& records) {
        return std::all_of(handles.begin(), handles.end(),
                           [&records](const auto handle) { return findRecord(records, handle) != nullptr; });
    };
    return allExist(references.buffers, data.buffers) && allExist(references.textures, data.textures) &&
           allExist(references.textureViews, data.textureViews) && allExist(references.samplers, data.samplers) &&
           allExist(references.bindGroupLayouts, data.bindGroupLayouts) &&
           allExist(references.pipelineLayouts, data.pipelineLayouts) &&
           allExist(references.pipelines, data.pipelines) && allExist(references.bindGroups, data.bindGroups) &&
           allExist(references.queryPools, data.queryPools) &&
           allExist(references.accelerationStructures, data.accelerationStructures) &&
           allExist(references.micromaps, data.micromaps);
}

[[nodiscard]] VkRect2D toVkClippedScissor(const RhiRect2D& rect, const uint32_t targetWidth,
                                          const uint32_t targetHeight) {
    const int64_t minX = std::clamp<int64_t>(rect.x, 0, targetWidth);
    const int64_t minY = std::clamp<int64_t>(rect.y, 0, targetHeight);
    const int64_t maxX = std::clamp<int64_t>(static_cast<int64_t>(rect.x) + rect.width, minX, targetWidth);
    const int64_t maxY = std::clamp<int64_t>(static_cast<int64_t>(rect.y) + rect.height, minY, targetHeight);
    return {{static_cast<int32_t>(minX), static_cast<int32_t>(targetHeight - static_cast<uint32_t>(maxY))},
            {static_cast<uint32_t>(maxX - minX), static_cast<uint32_t>(maxY - minY)}};
}

void markResourceReferencesUsed(VkRhiDeviceData& data, const VkRhiCommandResourceReferences& references,
                                const uint64_t sequence) {
    const auto markAll = [sequence](const auto& handles, auto& records) {
        for (const auto handle : handles) {
            auto* record = findRecord(records, handle);
            if (record != nullptr)
                record->lifetime.markUsed(sequence);
        }
    };
    markAll(references.buffers, data.buffers);
    markAll(references.textures, data.textures);
    markAll(references.textureViews, data.textureViews);
    markAll(references.samplers, data.samplers);
    markAll(references.bindGroupLayouts, data.bindGroupLayouts);
    markAll(references.pipelineLayouts, data.pipelineLayouts);
    markAll(references.pipelines, data.pipelines);
    markAll(references.bindGroups, data.bindGroups);
    markAll(references.queryPools, data.queryPools);
    markAll(references.accelerationStructures, data.accelerationStructures);
    markAll(references.micromaps, data.micromaps);
}

void destroyDeferredObject(VkRhiDeviceData& data, const VkRhiDeviceData::DeferredObject& item) {
    switch (item.type) {
    case VK_OBJECT_TYPE_IMAGE_VIEW:
        vkDestroyImageView(data.device, reinterpret_cast<VkImageView>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_SAMPLER:
        vkDestroySampler(data.device, reinterpret_cast<VkSampler>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_SHADER_MODULE:
        vkDestroyShaderModule(data.device, reinterpret_cast<VkShaderModule>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        vkDestroyDescriptorSetLayout(data.device, reinterpret_cast<VkDescriptorSetLayout>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
        vkDestroyPipelineLayout(data.device, reinterpret_cast<VkPipelineLayout>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_PIPELINE:
        vkDestroyPipeline(data.device, reinterpret_cast<VkPipeline>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_DESCRIPTOR_SET: {
        const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(item.object);
        vkFreeDescriptorSets(data.device, data.descriptorPool, 1u, &set);
        break;
    }
    case VK_OBJECT_TYPE_QUERY_POOL:
        vkDestroyQueryPool(data.device, reinterpret_cast<VkQueryPool>(item.object), nullptr);
        break;
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR:
        data.destroyAccelerationStructure(data.device, reinterpret_cast<VkAccelerationStructureKHR>(item.object),
                                          nullptr);
        break;
    case VK_OBJECT_TYPE_MICROMAP_EXT:
        data.destroyMicromap(data.device, reinterpret_cast<VkMicromapEXT>(item.object), nullptr);
        break;
    default: break;
    }
}

void destroySwapchainResources(VkRhiDeviceData& data) {
    for (const VkSemaphore semaphore : data.presentReadySemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(data.device, semaphore, nullptr);
        }
    }
    data.presentReadySemaphores.clear();
    for (const auto viewHandle : data.swapchainViews) {
        const auto it = data.textureViews.find(handleKey(viewHandle));
        if (it != data.textureViews.end()) {
            vkDestroyImageView(data.device, it->second.view, nullptr);
            data.textureViews.erase(it);
        }
        (void)data.textureViewHandles.release(viewHandle);
    }
    for (const auto textureHandle : data.swapchainTextures) {
        data.textures.erase(handleKey(textureHandle));
        (void)data.textureHandles.release(textureHandle);
    }
    for (const auto viewHandle : data.depthViews) {
        const auto it = data.textureViews.find(handleKey(viewHandle));
        if (it != data.textureViews.end()) {
            vkDestroyImageView(data.device, it->second.view, nullptr);
            data.textureViews.erase(it);
        }
        (void)data.textureViewHandles.release(viewHandle);
    }
    for (const auto textureHandle : data.depthTextures) {
        const auto it = data.textures.find(handleKey(textureHandle));
        if (it != data.textures.end()) {
            vmaDestroyImage(data.allocator, it->second.image, it->second.allocation);
            data.textures.erase(it);
        }
        (void)data.textureHandles.release(textureHandle);
    }
    data.swapchainViews.clear();
    data.swapchainTextures.clear();
    data.depthViews.clear();
    data.depthTextures.clear();
    data.swapchainImageInitialized.clear();
    destroyMainSwapchain(data);
    data.acquiredImage = UINT32_MAX;
    data.frameAcquired = false;
    data.frameSubmitted = false;
    data.frameImageAvailableWaited = false;
    data.frameLastGraphicsSequence = 0u;
}

[[nodiscard]] bool transitionSwapchainDepthImages(VkRhiDeviceData& data) {
    if (data.depthTextures.empty()) {
        return false;
    }
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = data.queueFamilies.graphics;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(data.device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = pool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(data.device, &allocationInfo, &commandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        return false;
    }
    std::vector<VkImageMemoryBarrier2> barriers;
    barriers.reserve(data.depthTextures.size());
    for (const RhiTextureHandle handle : data.depthTextures) {
        const auto* texture = findRecord(data.textures, handle);
        if (texture == nullptr) {
            vkDestroyCommandPool(data.device, pool, nullptr);
            return false;
        }
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u};
        barriers.push_back(barrier);
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        return false;
    }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = 1u;
    submitInfo.pCommandBufferInfos = &commandInfo;
    const bool submitted = vkQueueSubmit2(data.graphicsQueue, 1u, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS &&
                           vkQueueWaitIdle(data.graphicsQueue) == VK_SUCCESS;
    vkDestroyCommandPool(data.device, pool, nullptr);
    return submitted;
}

[[nodiscard]] bool createSwapchain(VkRhiDeviceData& data) {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(data.physicalDevice, data.surface, &capabilities),
                     "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return false;
    }

    uint32_t formatCount = 0u;
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceFormatsKHR(data.physicalDevice, data.surface, &formatCount, nullptr),
                     "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
        formatCount == 0u) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (!vkSucceeded(
            vkGetPhysicalDeviceSurfaceFormatsKHR(data.physicalDevice, data.surface, &formatCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        return false;
    }
    formats.resize(formatCount);
    const auto formatIt = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
        return format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (formatIt == formats.end()) {
        std::cerr << "VkRhiDevice: the surface does not expose the required SDR presentation format\n";
        return false;
    }

    uint32_t presentModeCount = 0u;
    if (!vkSucceeded(
            vkGetPhysicalDeviceSurfacePresentModesKHR(data.physicalDevice, data.surface, &presentModeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR") ||
        presentModeCount == 0u) {
        return false;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (!vkSucceeded(vkGetPhysicalDeviceSurfacePresentModesKHR(data.physicalDevice, data.surface, &presentModeCount,
                                                               presentModes.data()),
                     "vkGetPhysicalDeviceSurfacePresentModesKHR")) {
        return false;
    }
    presentModes.resize(presentModeCount);
    data.immediatePresentSupported =
        std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != presentModes.end();
    const VkPresentModeKHR requestedPresentMode =
        data.vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (std::find(presentModes.begin(), presentModes.end(), requestedPresentMode) == presentModes.end()) {
        std::cerr << "VkRhiDevice: the requested presentation mode is unavailable\n";
        return false;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(data.window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }
    VkExtent2D extent{};
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth), capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight), capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount != 0u) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const std::array<uint32_t, 2u> families{data.queueFamilies.graphics, data.queueFamilies.present};
    const VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((capabilities.supportedUsageFlags & requiredUsage) != requiredUsage ||
        (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0u) {
        std::cerr << "VkRhiDevice: the surface does not expose the required swapchain capabilities\n";
        return false;
    }
    VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainInfo.surface = data.surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = formatIt->format;
    swapchainInfo.imageColorSpace = formatIt->colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1u;
    swapchainInfo.imageUsage = requiredUsage;
    if (families[0] != families[1]) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = static_cast<uint32_t>(families.size());
        swapchainInfo.pQueueFamilyIndices = families.data();
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = requestedPresentMode;
    swapchainInfo.clipped = VK_TRUE;
    if (!vkSucceeded(createMainSwapchain(data, swapchainInfo), "vkCreateSwapchainKHR")) {
        return false;
    }
    data.swapchainFormat = formatIt->format;
    data.swapchainExtent = extent;
    data.presentMode = requestedPresentMode;

    uint32_t actualImageCount = 0u;
    if (!vkSucceeded(queryMainSwapchainImages(data, actualImageCount, nullptr), "vkGetSwapchainImagesKHR") ||
        actualImageCount == 0u) {
        destroySwapchainResources(data);
        return false;
    }
    std::vector<VkImage> images(actualImageCount);
    if (!vkSucceeded(queryMainSwapchainImages(data, actualImageCount, images.data()), "vkGetSwapchainImagesKHR") ||
        actualImageCount == 0u) {
        destroySwapchainResources(data);
        return false;
    }
    images.resize(actualImageCount);
    data.swapchainTextures.reserve(actualImageCount);
    data.swapchainViews.reserve(actualImageCount);
    data.depthTextures.reserve(actualImageCount);
    data.depthViews.reserve(actualImageCount);
    data.presentReadySemaphores.reserve(actualImageCount);

    for (uint32_t i = 0u; i < actualImageCount; ++i) {
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore presentReady = VK_NULL_HANDLE;
        if (!vkSucceeded(vkCreateSemaphore(data.device, &semaphoreInfo, nullptr, &presentReady),
                         "vkCreateSemaphore(present ready)")) {
            destroySwapchainResources(data);
            return false;
        }
        data.presentReadySemaphores.push_back(presentReady);
        const RhiTextureHandle textureHandle = data.textureHandles.allocate();
        RhiTextureDesc textureDesc{};
        textureDesc.debugName = "Vulkan swapchain image";
        textureDesc.memoryCategory = RhiMemoryCategory::Presentation;
        textureDesc.format = RhiTextureFormat::Bgra8Unorm;
        textureDesc.width = extent.width;
        textureDesc.height = extent.height;
        textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) | rhiFlag(RhiTextureUsage::TransferSrc) |
                            rhiFlag(RhiTextureUsage::TransferDst) | rhiFlag(RhiTextureUsage::Present);
        data.textures.emplace(handleKey(textureHandle), VkRhiDeviceData::Texture{images[i], VK_NULL_HANDLE, textureDesc,
                                                                                 textureDesc.debugName, true, true});
        data.swapchainTextures.push_back(textureHandle);
        data.swapchainImageInitialized.push_back(false);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = data.swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        VkImageView view = VK_NULL_HANDLE;
        if (!vkSucceeded(vkCreateImageView(data.device, &viewInfo, nullptr, &view), "vkCreateImageView(swapchain)")) {
            destroySwapchainResources(data);
            return false;
        }
        const RhiTextureViewHandle viewHandle = data.textureViewHandles.allocate();
        RhiTextureViewDesc rhiViewDesc{};
        rhiViewDesc.texture = textureHandle;
        rhiViewDesc.format = RhiTextureFormat::Bgra8Unorm;
        data.textureViews.emplace(handleKey(viewHandle), VkRhiDeviceData::TextureView{view, rhiViewDesc});
        data.swapchainViews.push_back(viewHandle);

        VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.format = VK_FORMAT_D32_SFLOAT;
        depthInfo.extent = {extent.width, extent.height, 1u};
        depthInfo.mipLevels = 1u;
        depthInfo.arrayLayers = 1u;
        depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        VmaAllocationInfo depthAllocationInfo{};
        if (!vkSucceeded(vmaCreateImage(data.allocator, &depthInfo, &allocationInfo, &depthImage, &depthAllocation,
                                        &depthAllocationInfo),
                         "vmaCreateImage(swapchain depth)")) {
            destroySwapchainResources(data);
            return false;
        }
        const RhiTextureHandle depthHandle = data.textureHandles.allocate();
        RhiTextureDesc depthDesc{};
        depthDesc.debugName = "Vulkan swapchain depth";
        depthDesc.memoryCategory = RhiMemoryCategory::Presentation;
        depthDesc.format = RhiTextureFormat::Depth32Float;
        depthDesc.width = extent.width;
        depthDesc.height = extent.height;
        depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment) | rhiFlag(RhiTextureUsage::Sampled);
        data.textures.emplace(handleKey(depthHandle), VkRhiDeviceData::Texture{depthImage,
                                                                               depthAllocation,
                                                                               depthDesc,
                                                                               depthDesc.debugName,
                                                                               false,
                                                                               true,
                                                                               {},
                                                                               depthAllocationInfo.size});
        data.depthTextures.push_back(depthHandle);

        VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        depthViewInfo.image = depthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
        depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u};
        VkImageView depthView = VK_NULL_HANDLE;
        if (!vkSucceeded(vkCreateImageView(data.device, &depthViewInfo, nullptr, &depthView),
                         "vkCreateImageView(swapchain depth)")) {
            destroySwapchainResources(data);
            return false;
        }
        const RhiTextureViewHandle depthViewHandle = data.textureViewHandles.allocate();
        RhiTextureViewDesc rhiDepthViewDesc{};
        rhiDepthViewDesc.texture = depthHandle;
        rhiDepthViewDesc.format = RhiTextureFormat::Depth32Float;
        data.textureViews.emplace(handleKey(depthViewHandle),
                                  VkRhiDeviceData::TextureView{depthView, rhiDepthViewDesc});
        data.depthViews.push_back(depthViewHandle);
    }
    if (!transitionSwapchainDepthImages(data)) {
        destroySwapchainResources(data);
        return false;
    }
    data.swapchainDirty = false;
    return true;
}

[[nodiscard]] bool recreateSwapchain(VkRhiDeviceData& data) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(data.window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (!vkSucceeded(waitDeviceIdle(data), "vkDeviceWaitIdle(swapchain)")) {
        return false;
    }
    destroySwapchainResources(data);
    return createSwapchain(data);
}

} // namespace

bool VkRhiInterop::recreateFrameGenerationSwapchain(VkRhiDevice& device, const bool frameGenerationLoaded) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (!device.m_initialized || device.m_data == nullptr || device.m_data->frameAcquired ||
        std::this_thread::get_id() != device.m_deviceThread || waitDeviceIdle(*device.m_data) != VK_SUCCESS) {
        return false;
    }
    device.reclaimCompletedWork();
    device.m_data->externalFrameCompletionSemaphore = VK_NULL_HANDLE;
    device.m_data->externalFrameCompletionValue = 0u;
    destroySwapchainResources(*device.m_data);
    if (!StreamlineRuntime::instance().setDlssFrameGenerationLoaded(frameGenerationLoaded)) {
        return false;
    }
    if (!createSwapchain(*device.m_data)) {
        return false;
    }
    device.refreshSwapchainCapabilities();
    return true;
#else
    static_cast<void>(device);
    static_cast<void>(frameGenerationLoaded);
    return false;
#endif
}

namespace {

[[nodiscard]] bool recreateSurfaceAndSwapchain(VkRhiDeviceData& data) {
    if (!vkSucceeded(waitDeviceIdle(data), "vkDeviceWaitIdle(surface)")) {
        return false;
    }
    destroySwapchainResources(data);
    destroyMainSurface(data);
    if (!vkSucceeded(createMainSurface(data), "vkCreateWin32SurfaceKHR")) {
        return false;
    }
    VkBool32 presentSupported = VK_FALSE;
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceSupportKHR(data.physicalDevice, data.queueFamilies.present, data.surface,
                                                          &presentSupported),
                     "vkGetPhysicalDeviceSurfaceSupportKHR") ||
        presentSupported != VK_TRUE) {
        return false;
    }
    if (!createSwapchain(data)) {
        data.surfaceLost = true;
        return false;
    }
    data.surfaceLost = false;
    return true;
}

[[nodiscard]] bool uploadTextureInitialData(VkRhiDeviceData& data, VkImage image, const RhiTextureDesc& desc,
                                            const RhiTextureInitialData& initialData) {
    if (initialData.finalState == RhiResourceState::Undefined || initialData.mipLevel >= desc.mipLevels ||
        initialData.layerCount == 0u)
        return false;
    const bool texture3D = desc.dimension == RhiTextureDimension::Texture3D;
    const uint32_t arrayLayers = texture3D ? 1u : desc.depthOrLayers;
    const uint32_t mipDepth = texture3D ? std::max(desc.depthOrLayers >> initialData.mipLevel, 1u) : 1u;
    if (texture3D) {
        if (initialData.arrayLayer >= mipDepth || initialData.layerCount > mipDepth - initialData.arrayLayer) {
            return false;
        }
    } else if (initialData.arrayLayer >= arrayLayers || initialData.layerCount > arrayLayers - initialData.arrayLayer) {
        return false;
    }
    const size_t mipWidth = std::max(desc.width >> initialData.mipLevel, 1u);
    const size_t mipHeight = std::max(desc.height >> initialData.mipLevel, 1u);
    const size_t formatBytes = formatByteSize(desc.format);
    if (mipHeight > std::numeric_limits<size_t>::max() / mipWidth) {
        return false;
    }
    const size_t sliceTexels = mipWidth * mipHeight;
    if (initialData.layerCount > std::numeric_limits<size_t>::max() / sliceTexels) {
        return false;
    }
    const size_t texelCount = sliceTexels * initialData.layerCount;
    if (formatBytes == 0u || formatBytes > std::numeric_limits<size_t>::max() / texelCount) {
        return false;
    }
    const size_t expectedSize = texelCount * formatBytes;
    if (initialData.sizeBytes != expectedSize) {
        return false;
    }
    VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stagingInfo.size = initialData.sizeBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(vmaCreateBuffer(data.allocator, &stagingInfo, &allocationInfo, &staging, &allocation,
                                     &nativeAllocationInfo),
                     "vmaCreateBuffer(texture upload)"))
        return false;
    std::memcpy(nativeAllocationInfo.pMappedData, initialData.pixels, initialData.sizeBytes);
    vmaFlushAllocation(data.allocator, allocation, 0u, initialData.sizeBytes);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = data.queueFamilies.graphics;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateCommandPool(data.device, &poolInfo, nullptr, &pool),
                     "vkCreateCommandPool(texture upload)")) {
        vmaDestroyBuffer(data.allocator, staging, allocation);
        return false;
    }
    VkCommandBufferAllocateInfo commandAllocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocation.commandPool = pool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = 1u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (!vkSucceeded(vkAllocateCommandBuffers(data.device, &commandAllocation, &commandBuffer),
                     "vkAllocateCommandBuffers(texture upload)")) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        vmaDestroyBuffer(data.allocator, staging, allocation);
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vkSucceeded(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(texture upload)")) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        vmaDestroyBuffer(data.allocator, staging, allocation);
        return false;
    }
    VkImageMemoryBarrier2 toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = defaultAspectForFormat(desc.format);
    toTransfer.subresourceRange.baseMipLevel = 0u;
    toTransfer.subresourceRange.levelCount = desc.mipLevels;
    toTransfer.subresourceRange.baseArrayLayer = 0u;
    toTransfer.subresourceRange.layerCount = arrayLayers;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &toTransfer;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = defaultAspectForFormat(desc.format);
    region.imageSubresource.mipLevel = initialData.mipLevel;
    region.imageSubresource.baseArrayLayer = texture3D ? 0u : initialData.arrayLayer;
    region.imageSubresource.layerCount = texture3D ? 1u : initialData.layerCount;
    region.imageOffset.z = texture3D ? static_cast<int32_t>(initialData.arrayLayer) : 0;
    region.imageExtent.width = std::max(desc.width >> initialData.mipLevel, 1u);
    region.imageExtent.height = std::max(desc.height >> initialData.mipLevel, 1u);
    region.imageExtent.depth = texture3D ? initialData.layerCount : 1u;
    vkCmdCopyBufferToImage(commandBuffer, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    const auto finalState = toVkResourceState(initialData.finalState);
    VkImageMemoryBarrier2 toFinal{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toFinal.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    toFinal.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toFinal.dstStageMask = finalState.stages;
    toFinal.dstAccessMask = finalState.access;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toFinal.newLayout = finalState.layout;
    toFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.image = image;
    toFinal.subresourceRange = toTransfer.subresourceRange;
    dependency.pImageMemoryBarriers = &toFinal;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    if (!vkSucceeded(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(texture upload)")) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        vmaDestroyBuffer(data.allocator, staging, allocation);
        return false;
    }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = 1u;
    submitInfo.pCommandBufferInfos = &commandInfo;
    const VkResult submitResult = vkQueueSubmit2(data.graphicsQueue, 1u, &submitInfo, VK_NULL_HANDLE);
    const VkResult waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(data.graphicsQueue) : submitResult;
    const bool submitted = vkSucceeded(submitResult, "vkQueueSubmit2(texture upload)") &&
                           vkSucceeded(waitResult, "vkQueueWaitIdle(texture upload)");
    vkDestroyCommandPool(data.device, pool, nullptr);
    vmaDestroyBuffer(data.allocator, staging, allocation);
    return submitted;
}

} // namespace

VkRhiDevice::VkRhiDevice() = default;

VkRhiDevice::~VkRhiDevice() {
    shutdown();
}

bool VkRhiDevice::prepareWindowCreation() {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (!StreamlineRuntime::instance().initialized()) {
        std::cerr << "VkRhiDevice: Streamline must be initialized before Vulkan discovery\n";
        return false;
    }
#endif
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return glfwVulkanSupported() == GLFW_TRUE;
}

bool VkRhiDevice::init(const RhiDeviceDesc& desc) {
    if (m_initialized || desc.nativeWindow == nullptr) {
        return false;
    }
    m_data = std::make_unique<VkRhiDeviceData>();
    m_data->window = static_cast<GLFWwindow*>(desc.nativeWindow);
    m_data->requestedWidth = static_cast<uint32_t>(std::max(desc.width, 1));
    m_data->requestedHeight = static_cast<uint32_t>(std::max(desc.height, 1));
    if (desc.vsyncEnabled.has_value()) {
        m_data->vsyncEnabled = *desc.vsyncEnabled;
    }

    uint32_t glfwExtensionCount = 0u;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensions == nullptr || glfwExtensionCount == 0u) {
        std::cerr << "VkRhiDevice: GLFW did not provide Vulkan instance extensions\n";
        shutdown();
        return false;
    }
    VulkanRequirementCollector requirements;
    for (uint32_t i = 0u; i < glfwExtensionCount; ++i) {
        requirements.requireInstanceExtension(glfwExtensions[i]);
    }
    if (desc.enableDebugOutput || desc.enableDebugMarkers) {
        requirements.requireInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    requirements.requireDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
#if defined(MECRAFT_ENABLE_FSR31)
    requirements.requireDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
    requirements.requireDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
#endif
    requirements.requireQueue(RhiQueueType::Graphics);
    requirements.requireQueue(RhiQueueType::Compute);
    requirements.requireQueue(RhiQueueType::Transfer);
    requirements.requireQueue(RhiQueueType::Present);
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (!streamline.appendVulkanRequirements(requirements)) {
        std::cerr << "VkRhiDevice: Streamline requirements are unavailable\n";
        shutdown();
        return false;
    }
#endif

    uint32_t instanceExtensionCount = 0u;
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> availableInstanceExtensions(instanceExtensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, availableInstanceExtensions.data());
    std::string missingExtension;
    if (!requirements.validateInstanceExtensions(availableInstanceExtensions, missingExtension)) {
        std::cerr << "VkRhiDevice: required Vulkan instance extension is unavailable: " << missingExtension << '\n';
        shutdown();
        return false;
    }
    const std::vector<const char*> instanceExtensions = requirements.instanceExtensionNames();
    std::vector<const char*> layers;
    if (desc.enableDebugOutput) {
        uint32_t layerCount = 0u;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        const bool validationAvailable =
            std::any_of(availableLayers.begin(), availableLayers.end(), [](const auto& layer) {
                return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
            });
        if (!validationAvailable) {
            std::cerr << "VkRhiDevice: validation output was requested but the validation layer is unavailable\n";
            shutdown();
            return false;
        }
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = desc.debugName != nullptr ? desc.debugName : "Mecraft";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    applicationInfo.pEngineName = "Mecraft";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (!vkSucceeded(vkCreateInstance(&instanceInfo, nullptr, &m_data->instance), "vkCreateInstance")) {
        shutdown();
        return false;
    }

    if (desc.enableDebugOutput || desc.enableDebugMarkers) {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_data->instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger == nullptr) {
            std::cerr << "VkRhiDevice: VK_EXT_debug_utils entry points are unavailable\n";
            shutdown();
            return false;
        }
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;
        debugInfo.pUserData = &m_data->validationErrorCount;
        if (!vkSucceeded(createMessenger(m_data->instance, &debugInfo, nullptr, &m_data->debugMessenger),
                         "vkCreateDebugUtilsMessengerEXT")) {
            shutdown();
            return false;
        }
    }
    if (!vkSucceeded(createMainSurface(*m_data), "vkCreateWin32SurfaceKHR")) {
        shutdown();
        return false;
    }

    uint32_t physicalDeviceCount = 0u;
    vkEnumeratePhysicalDevices(m_data->instance, &physicalDeviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(m_data->instance, &physicalDeviceCount, physicalDevices.data());
    int bestScore = -1;
    VkPhysicalDeviceVulkan12Features selected12{};
    VkPhysicalDeviceVulkan13Features selected13{};
    VkPhysicalDeviceFeatures selectedCoreFeatures{};
    bool selectedAccelerationStructureHostCommands = false;
    bool selectedAccelerationStructureDescriptorUpdateAfterBind = false;
    bool selectedOpacityMicromap = false;
    const bool requireOpticalFlow = requirements.opticalFlowQueueCount() > 0u;
    const std::vector<const char*> requiredFeatures12 = requirements.vulkan12FeatureNames();
    const std::vector<const char*> requiredFeatures13 = requirements.vulkan13FeatureNames();
    for (const VkPhysicalDevice candidate : physicalDevices) {
        uint32_t extensionCount = 0u;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
        if (!requirements.validateDeviceExtensions(extensions, missingExtension)) {
            continue;
        }
        const bool supportsOpacityMicromap =
            containsDeviceExtension(extensions, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
        const QueueFamilies families = queryQueueFamilies(candidate, m_data->surface, requireOpticalFlow);
        if (!families.complete(requireOpticalFlow) || !queueRequirementsSupported(candidate, families, requirements)) {
            continue;
        }
        VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
                                                     requireOpticalFlow ? &opticalFlow : nullptr};
        VkPhysicalDeviceOpacityMicromapFeaturesEXT opacityMicromap{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, &rayQuery};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            supportsOpacityMicromap ? static_cast<void*>(&opacityMicromap) : static_cast<void*>(&rayQuery)};
        VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rayTracingMaintenance{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &accelerationStructure};
        VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT, &rayTracingMaintenance};
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &depthClip};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13};
        VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12};
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features11};
        vkGetPhysicalDeviceFeatures2(candidate, &features2);
#if defined(MECRAFT_ENABLE_FSR31)
        if (features2.features.shaderInt16 != VK_TRUE) {
            continue;
        }
#endif
        if (features2.features.samplerAnisotropy != VK_TRUE || features2.features.independentBlend != VK_TRUE ||
            features2.features.imageCubeArray != VK_TRUE || features11.shaderDrawParameters != VK_TRUE ||
            features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE ||
            features13.shaderDemoteToHelperInvocation != VK_TRUE || features12.timelineSemaphore != VK_TRUE ||
            features12.bufferDeviceAddress != VK_TRUE || features12.hostQueryReset != VK_TRUE ||
            depthClip.depthClipControl != VK_TRUE || accelerationStructure.accelerationStructure != VK_TRUE ||
            accelerationStructure.descriptorBindingAccelerationStructureUpdateAfterBind != VK_TRUE ||
            rayTracingMaintenance.rayTracingMaintenance1 != VK_TRUE || rayQuery.rayQuery != VK_TRUE) {
            continue;
        }
#if defined(MECRAFT_ENABLE_STREAMLINE)
        if (!supportsStreamlineVulkan12Features(features12, requiredFeatures12) ||
            !supportsStreamlineVulkan13Features(features13, requiredFeatures13) ||
            (requireOpticalFlow && opticalFlow.opticalFlow != VK_TRUE)) {
            continue;
        }
#endif
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        if (score > bestScore) {
            bestScore = score;
            m_data->physicalDevice = candidate;
            m_data->queueFamilies = families;
            m_data->properties = properties;
            selected12 = features12;
            selected13 = features13;
            selectedCoreFeatures = features2.features;
            selectedAccelerationStructureHostCommands =
                accelerationStructure.accelerationStructureHostCommands == VK_TRUE;
            selectedAccelerationStructureDescriptorUpdateAfterBind = true;
            selectedOpacityMicromap = supportsOpacityMicromap && opacityMicromap.micromap == VK_TRUE;
        }
    }
    if (m_data->physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "VkRhiDevice: no device satisfies the Vulkan 1.3 RHI feature contract\n";
        shutdown();
        return false;
    }

    std::set<uint32_t> uniqueFamilies{m_data->queueFamilies.graphics, m_data->queueFamilies.compute,
                                      m_data->queueFamilies.transfer, m_data->queueFamilies.present};
    if (m_data->queueFamilies.opticalFlow != UINT32_MAX) {
        uniqueFamilies.insert(m_data->queueFamilies.opticalFlow);
    }
    uint32_t maxQueueCount = 1u;
    for (const uint32_t family : uniqueFamilies) {
        maxQueueCount = std::max(maxQueueCount, requiredQueuesForFamily(m_data->queueFamilies, requirements, family));
    }
    const std::vector<float> queuePriorities(maxQueueCount, 1.0f);
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (const uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = requiredQueuesForFamily(m_data->queueFamilies, requirements, family);
        queueInfo.pQueuePriorities = queuePriorities.data();
        queueInfos.push_back(queueInfo);
    }
    m_data->streamlineComputeQueueIndex = 1u;
    m_data->streamlineGraphicsQueueIndex = m_data->queueFamilies.graphics == m_data->queueFamilies.compute
                                               ? 1u + requirements.additionalQueueCount(RhiQueueType::Compute)
                                               : 1u;
    m_data->streamlineOpticalFlowQueueIndex = 0u;
    VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    opticalFlow.opticalFlow = requireOpticalFlow ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
                                                 requireOpticalFlow ? &opticalFlow : nullptr};
    rayQuery.rayQuery = VK_TRUE;
    VkPhysicalDeviceOpacityMicromapFeaturesEXT opacityMicromap{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT, &rayQuery};
    opacityMicromap.micromap = selectedOpacityMicromap ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        selectedOpacityMicromap ? static_cast<void*>(&opacityMicromap) : static_cast<void*>(&rayQuery)};
    accelerationStructure.accelerationStructure = VK_TRUE;
    accelerationStructure.accelerationStructureHostCommands =
        selectedAccelerationStructureHostCommands ? VK_TRUE : VK_FALSE;
    accelerationStructure.descriptorBindingAccelerationStructureUpdateAfterBind =
        selectedAccelerationStructureDescriptorUpdateAfterBind ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rayTracingMaintenance{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &accelerationStructure};
    rayTracingMaintenance.rayTracingMaintenance1 = VK_TRUE;
    VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT, &rayTracingMaintenance};
    depthClip.depthClipControl = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &depthClip};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;
    features13.privateData = std::any_of(requiredFeatures13.begin(), requiredFeatures13.end(),
                                         [](const char* name) { return std::strcmp(name, "privateData") == 0; })
                                 ? VK_TRUE
                                 : VK_FALSE;
    features13.subgroupSizeControl = selected13.subgroupSizeControl;
    features13.computeFullSubgroups = selected13.computeFullSubgroups;
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13};
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.hostQueryReset = VK_TRUE;
    features12.shaderFloat16 = selected12.shaderFloat16;
    features12.shaderInt8 = selected12.shaderInt8;
    features12.descriptorIndexing = selected12.descriptorIndexing;
    features12.shaderSampledImageArrayNonUniformIndexing = selected12.shaderSampledImageArrayNonUniformIndexing;
    features12.shaderStorageBufferArrayNonUniformIndexing = selected12.shaderStorageBufferArrayNonUniformIndexing;
    features12.shaderStorageImageArrayNonUniformIndexing = selected12.shaderStorageImageArrayNonUniformIndexing;
    features12.descriptorBindingUniformBufferUpdateAfterBind = selected12.descriptorBindingUniformBufferUpdateAfterBind;
    features12.descriptorBindingSampledImageUpdateAfterBind = selected12.descriptorBindingSampledImageUpdateAfterBind;
    features12.descriptorBindingStorageImageUpdateAfterBind = selected12.descriptorBindingStorageImageUpdateAfterBind;
    features12.descriptorBindingStorageBufferUpdateAfterBind = selected12.descriptorBindingStorageBufferUpdateAfterBind;
    features12.descriptorBindingUpdateUnusedWhilePending = selected12.descriptorBindingUpdateUnusedWhilePending;
    features12.descriptorBindingPartiallyBound = selected12.descriptorBindingPartiallyBound;
    features12.descriptorBindingVariableDescriptorCount = selected12.descriptorBindingVariableDescriptorCount;
    features12.runtimeDescriptorArray = selected12.runtimeDescriptorArray;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12};
    features11.shaderDrawParameters = VK_TRUE;
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features11};
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.independentBlend = VK_TRUE;
    features2.features.imageCubeArray = VK_TRUE;
    features2.features.multiDrawIndirect = selectedCoreFeatures.multiDrawIndirect;
    // Single-channel (r8) storage images used by async-compute SSAO are an
    // extended storage format; enable the feature whenever the device has it.
    features2.features.shaderStorageImageExtendedFormats = selectedCoreFeatures.shaderStorageImageExtendedFormats;
#if defined(MECRAFT_ENABLE_FSR31)
    features2.features.shaderInt16 = VK_TRUE;
#endif
    std::vector<const char*> deviceExtensions = requirements.deviceExtensionNames();
    if (selectedOpacityMicromap) {
        deviceExtensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
    }
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features2};
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (!vkSucceeded(vkCreateDevice(m_data->physicalDevice, &deviceInfo, nullptr, &m_data->device), "vkCreateDevice")) {
        shutdown();
        return false;
    }
    vkGetDeviceQueue(m_data->device, m_data->queueFamilies.graphics, 0u, &m_data->graphicsQueue);
    vkGetDeviceQueue(m_data->device, m_data->queueFamilies.compute, 0u, &m_data->computeQueue);
    vkGetDeviceQueue(m_data->device, m_data->queueFamilies.transfer, 0u, &m_data->transferQueue);
    vkGetDeviceQueue(m_data->device, m_data->queueFamilies.present, 0u, &m_data->presentQueue);
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineVulkanDeviceInfo streamlineDeviceInfo;
    streamlineDeviceInfo.instance = m_data->instance;
    streamlineDeviceInfo.physicalDevice = m_data->physicalDevice;
    streamlineDeviceInfo.device = m_data->device;
    streamlineDeviceInfo.graphicsQueueFamily = m_data->queueFamilies.graphics;
    streamlineDeviceInfo.graphicsQueueIndex = m_data->streamlineGraphicsQueueIndex;
    streamlineDeviceInfo.computeQueueFamily = m_data->queueFamilies.compute;
    streamlineDeviceInfo.computeQueueIndex = m_data->streamlineComputeQueueIndex;
    streamlineDeviceInfo.opticalFlowQueueFamily = m_data->queueFamilies.opticalFlow;
    streamlineDeviceInfo.opticalFlowQueueIndex = m_data->streamlineOpticalFlowQueueIndex;
    streamlineDeviceInfo.useNativeOpticalFlow = requireOpticalFlow;
    if (!streamline.setVulkanDevice(streamlineDeviceInfo)) {
        std::cerr << streamline.lastError() << '\n';
        shutdown();
        return false;
    }
#endif
    m_data->setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(m_data->device, "vkSetDebugUtilsObjectNameEXT"));
    m_data->beginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_data->device, "vkCmdBeginDebugUtilsLabelEXT"));
    m_data->endLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_data->device, "vkCmdEndDebugUtilsLabelEXT"));
    m_data->insertLabel = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_data->device, "vkCmdInsertDebugUtilsLabelEXT"));
    m_data->createAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkCreateAccelerationStructureKHR"));
    m_data->destroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkDestroyAccelerationStructureKHR"));
    m_data->getAccelerationStructureBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkGetAccelerationStructureBuildSizesKHR"));
    m_data->getAccelerationStructureDeviceAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkGetAccelerationStructureDeviceAddressKHR"));
    m_data->cmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkCmdBuildAccelerationStructuresKHR"));
    m_data->cmdCopyAccelerationStructure = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(m_data->device, "vkCmdCopyAccelerationStructureKHR"));
    m_data->cmdWriteAccelerationStructuresProperties =
        reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(
            vkGetDeviceProcAddr(m_data->device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    if (selectedOpacityMicromap) {
        m_data->createMicromap =
            reinterpret_cast<PFN_vkCreateMicromapEXT>(vkGetDeviceProcAddr(m_data->device, "vkCreateMicromapEXT"));
        m_data->destroyMicromap =
            reinterpret_cast<PFN_vkDestroyMicromapEXT>(vkGetDeviceProcAddr(m_data->device, "vkDestroyMicromapEXT"));
        m_data->getMicromapBuildSizes = reinterpret_cast<PFN_vkGetMicromapBuildSizesEXT>(
            vkGetDeviceProcAddr(m_data->device, "vkGetMicromapBuildSizesEXT"));
        m_data->cmdBuildMicromaps =
            reinterpret_cast<PFN_vkCmdBuildMicromapsEXT>(vkGetDeviceProcAddr(m_data->device, "vkCmdBuildMicromapsEXT"));
        if (m_data->createMicromap == nullptr || m_data->destroyMicromap == nullptr ||
            m_data->getMicromapBuildSizes == nullptr || m_data->cmdBuildMicromaps == nullptr) {
            logRhiError("required opacity-micromap entry points are unavailable");
            shutdown();
            return false;
        }
    }
    if (m_data->createAccelerationStructure == nullptr || m_data->destroyAccelerationStructure == nullptr ||
        m_data->getAccelerationStructureBuildSizes == nullptr ||
        m_data->getAccelerationStructureDeviceAddress == nullptr || m_data->cmdBuildAccelerationStructures == nullptr ||
        m_data->cmdCopyAccelerationStructure == nullptr ||
        m_data->cmdWriteAccelerationStructuresProperties == nullptr) {
        logRhiError("required acceleration-structure entry points are unavailable");
        shutdown();
        return false;
    }

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = m_data->physicalDevice;
    allocatorInfo.device = m_data->device;
    allocatorInfo.instance = m_data->instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (!vkSucceeded(vmaCreateAllocator(&allocatorInfo, &m_data->allocator), "vmaCreateAllocator")) {
        shutdown();
        return false;
    }
    const std::array<VkDescriptorPoolSize, 7u> poolSizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096u},
                                                          {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4096u}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags =
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 4096u;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (!vkSucceeded(vkCreateDescriptorPool(m_data->device, &poolInfo, nullptr, &m_data->descriptorPool),
                     "vkCreateDescriptorPool")) {
        shutdown();
        return false;
    }
    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (!vkSucceeded(vkCreatePipelineCache(m_data->device, &cacheInfo, nullptr, &m_data->pipelineCache),
                     "vkCreatePipelineCache")) {
        shutdown();
        return false;
    }
    VkSemaphoreTypeCreateInfo timelineType{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineType};
    for (VkSemaphore& timeline : m_data->queueTimelines) {
        if (!vkSucceeded(vkCreateSemaphore(m_data->device, &semaphoreInfo, nullptr, &timeline),
                         "vkCreateSemaphore(queue timeline)")) {
            shutdown();
            return false;
        }
    }
    for (auto& frame : m_data->frames) {
        VkSemaphoreCreateInfo binaryInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = 0u;
        if (!vkSucceeded(vkCreateSemaphore(m_data->device, &binaryInfo, nullptr, &frame.imageAvailable),
                         "vkCreateSemaphore(image available)") ||
            !vkSucceeded(vkCreateFence(m_data->device, &fenceInfo, nullptr, &frame.fence), "vkCreateFence(frame)")) {
            shutdown();
            return false;
        }
    }
    if (!createSwapchain(*m_data)) {
        shutdown();
        return false;
    }

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceOpacityMicromapPropertiesEXT opacityMicromapProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT};
    if (selectedOpacityMicromap) {
        opacityMicromapProperties.pNext = nullptr;
        accelerationStructureProperties.pNext = &opacityMicromapProperties;
    }
    VkPhysicalDeviceVulkan12Properties properties12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
                                                    &accelerationStructureProperties};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties12};
    vkGetPhysicalDeviceProperties2(m_data->physicalDevice, &properties2);
    m_capabilities.vulkanApiVersion = m_data->properties.apiVersion;
    m_capabilities.dynamicRendering = true;
    m_capabilities.synchronization2 = true;
    m_capabilities.shaderDemoteToHelperInvocation = true;
    m_capabilities.timelineSemaphore = true;
    // Placed textures ride Vulkan 1.3 core (vkGetDeviceImageMemoryRequirements)
    // plus VMA's aliasing allocation path; no extension gate needed.
    m_capabilities.textureAliasing = true;
    m_capabilities.bufferDeviceAddress = true;
    m_capabilities.accelerationStructure = true;
    m_capabilities.rayQuery = true;
    m_capabilities.opacityMicromap = selectedOpacityMicromap;
    m_capabilities.maxOpacityMicromapTwoStateSubdivisionLevel =
        selectedOpacityMicromap ? opacityMicromapProperties.maxOpacity2StateSubdivisionLevel : 0u;
    m_capabilities.maxOpacityMicromapFourStateSubdivisionLevel =
        selectedOpacityMicromap ? opacityMicromapProperties.maxOpacity4StateSubdivisionLevel : 0u;
    m_capabilities.accelerationStructureHostCommands = selectedAccelerationStructureHostCommands;
    m_capabilities.maxAccelerationStructureGeometryCount = accelerationStructureProperties.maxGeometryCount;
    m_capabilities.maxAccelerationStructureInstanceCount = accelerationStructureProperties.maxInstanceCount;
    m_capabilities.maxAccelerationStructurePrimitiveCount = accelerationStructureProperties.maxPrimitiveCount;
    m_capabilities.minAccelerationStructureScratchOffsetAlignment =
        accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
    m_capabilities.multiDrawIndirect = selectedCoreFeatures.multiDrawIndirect == VK_TRUE;
    m_capabilities.timestampQuery = m_data->properties.limits.timestampComputeAndGraphics == VK_TRUE;
    m_capabilities.textureView = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.storageImage = true;
    m_capabilities.descriptorIndexing = selected12.descriptorIndexing == VK_TRUE;
    m_capabilities.descriptorBindingPartiallyBound = selected12.descriptorBindingPartiallyBound == VK_TRUE;
    m_capabilities.descriptorBindingVariableDescriptorCount =
        selected12.descriptorBindingVariableDescriptorCount == VK_TRUE;
    m_capabilities.descriptorBindingUpdateUnusedWhilePending =
        selected12.descriptorBindingUpdateUnusedWhilePending == VK_TRUE;
    m_capabilities.descriptorBindingUniformBufferUpdateAfterBind =
        selected12.descriptorBindingUniformBufferUpdateAfterBind == VK_TRUE;
    m_capabilities.descriptorBindingSampledImageUpdateAfterBind =
        selected12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
    m_capabilities.descriptorBindingStorageImageUpdateAfterBind =
        selected12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE;
    m_capabilities.descriptorBindingStorageBufferUpdateAfterBind =
        selected12.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE;
    m_capabilities.descriptorBindingAccelerationStructureUpdateAfterBind =
        selectedAccelerationStructureDescriptorUpdateAfterBind;
    m_capabilities.runtimeDescriptorArray = selected12.runtimeDescriptorArray == VK_TRUE;
    m_capabilities.shaderSampledImageArrayNonUniformIndexing =
        selected12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    m_capabilities.shaderStorageBufferArrayNonUniformIndexing =
        selected12.shaderStorageBufferArrayNonUniformIndexing == VK_TRUE;
    m_capabilities.maxColorAttachments = m_data->properties.limits.maxColorAttachments;
    m_capabilities.maxSampledTexturesPerStage = m_data->properties.limits.maxPerStageDescriptorSampledImages;
    m_capabilities.textureBufferCopyRowPitchAlignment =
        static_cast<uint32_t>(m_data->properties.limits.optimalBufferCopyRowPitchAlignment);
    m_capabilities.maxSamplerAnisotropy = m_data->properties.limits.maxSamplerAnisotropy;
    m_capabilities.maxDescriptorSetUpdateAfterBindSampledImages =
        properties12.maxDescriptorSetUpdateAfterBindSampledImages;
    m_capabilities.maxDescriptorSetUpdateAfterBindSamplers = properties12.maxDescriptorSetUpdateAfterBindSamplers;
    m_capabilities.maxDescriptorSetUpdateAfterBindStorageImages =
        properties12.maxDescriptorSetUpdateAfterBindStorageImages;
    m_capabilities.maxDescriptorSetUpdateAfterBindUniformBuffers =
        properties12.maxDescriptorSetUpdateAfterBindUniformBuffers;
    m_capabilities.maxDescriptorSetUpdateAfterBindStorageBuffers =
        properties12.maxDescriptorSetUpdateAfterBindStorageBuffers;
    m_capabilities.maxDescriptorSetUpdateAfterBindAccelerationStructures =
        accelerationStructureProperties.maxDescriptorSetUpdateAfterBindAccelerationStructures;
    m_capabilities.maxPerStageDescriptorUpdateAfterBindSampledImages =
        properties12.maxPerStageDescriptorUpdateAfterBindSampledImages;
    m_capabilities.maxPerStageDescriptorUpdateAfterBindSamplers =
        properties12.maxPerStageDescriptorUpdateAfterBindSamplers;
    m_capabilities.maxPerStageDescriptorUpdateAfterBindStorageBuffers =
        properties12.maxPerStageDescriptorUpdateAfterBindStorageBuffers;
    m_capabilities.maxPerStageDescriptorUpdateAfterBindAccelerationStructures =
        accelerationStructureProperties.maxPerStageDescriptorUpdateAfterBindAccelerationStructures;
    m_capabilities.maxPerStageUpdateAfterBindResources = properties12.maxPerStageUpdateAfterBindResources;
    m_capabilities.graphicsQueueFamilyIndex = m_data->queueFamilies.graphics;
    m_capabilities.computeQueueFamilyIndex = m_data->queueFamilies.compute;
    m_capabilities.transferQueueFamilyIndex = m_data->queueFamilies.transfer;
    m_capabilities.presentQueueFamilyIndex = m_data->queueFamilies.present;
    uint32_t queueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(m_data->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_data->physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
    const auto supportsTimestamps = [&](const uint32_t family) {
        return family < queueFamilyProperties.size() && queueFamilyProperties[family].timestampValidBits != 0u;
    };
    m_capabilities.graphicsTimestampQuery = supportsTimestamps(m_data->queueFamilies.graphics);
    m_capabilities.computeTimestampQuery = supportsTimestamps(m_data->queueFamilies.compute);
    m_capabilities.transferTimestampQuery = supportsTimestamps(m_data->queueFamilies.transfer);
    m_capabilities.dedicatedComputeQueue = m_data->queueFamilies.compute != m_data->queueFamilies.graphics;
    m_capabilities.storageImageExtendedFormats = selectedCoreFeatures.shaderStorageImageExtendedFormats == VK_TRUE;
    m_capabilities.dedicatedTransferQueue = m_data->queueFamilies.transfer != m_data->queueFamilies.graphics &&
                                            m_data->queueFamilies.transfer != m_data->queueFamilies.compute;
    refreshSwapchainCapabilities();

    m_deviceThread = std::this_thread::get_id();
    m_deviceId = g_nextVkRhiDeviceId.fetch_add(1u, std::memory_order_relaxed);
    m_initialized = true;
    MECRAFT_LOG_STREAM(std::cout << "Vulkan: " << m_data->properties.deviceName << '\n');
    return true;
}

void VkRhiDevice::shutdown() {
    if (m_data == nullptr) {
        m_initialized = false;
        return;
    }
    if (m_data->device != VK_NULL_HANDLE) {
        const std::lock_guard<std::mutex> registryLock(m_data->commandRegistryMutex);
        if (waitDeviceIdle(*m_data) == VK_SUCCESS && m_initialized) {
            reclaimCompletedWorkUnlocked();
        }
        for (VkRhiCommandListPool* pool : m_data->commandListPools) {
            for (const auto& list : pool->m_commandLists) {
                m_data->commandLists.erase(list.get());
                for (const auto& transient : list->m_data->transientBuffers) {
                    vmaDestroyBuffer(m_data->allocator, transient.first, transient.second);
                }
                list->m_data->transientBuffers.clear();
                list->m_device = nullptr;
                list->m_data->commandBuffer = VK_NULL_HANDLE;
            }
            for (void*& commandPool : pool->m_commandPools) {
                if (commandPool != nullptr) {
                    vkDestroyCommandPool(m_data->device, static_cast<VkCommandPool>(commandPool), nullptr);
                    commandPool = nullptr;
                }
            }
            pool->m_device = nullptr;
        }
        m_data->commandListPools.clear();
        destroySwapchainResources(*m_data);
        for (auto& [_, record] : m_data->queryPools) {
            vkDestroyQueryPool(m_data->device, record.pool, nullptr);
        }
        for (auto& [_, record] : m_data->pipelines) {
            vkDestroyPipeline(m_data->device, record.pipeline, nullptr);
        }
        for (auto& [_, record] : m_data->pipelineLayouts) {
            vkDestroyPipelineLayout(m_data->device, record.layout, nullptr);
        }
        for (auto& [_, record] : m_data->bindGroups) {
            if (record.set != VK_NULL_HANDLE && m_data->descriptorPool != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &record.set);
            }
        }
        for (auto& [_, record] : m_data->bindGroupLayouts) {
            vkDestroyDescriptorSetLayout(m_data->device, record.layout, nullptr);
        }
        for (auto& [_, record] : m_data->shaders) {
            vkDestroyShaderModule(m_data->device, record.module, nullptr);
        }
        for (auto& [_, record] : m_data->samplers) {
            vkDestroySampler(m_data->device, record.sampler, nullptr);
        }
        for (auto& [_, record] : m_data->textureViews) {
            vkDestroyImageView(m_data->device, record.view, nullptr);
        }
        for (auto& [_, record] : m_data->accelerationStructures) {
            m_data->destroyAccelerationStructure(m_data->device, record.accelerationStructure, nullptr);
        }
        for (auto& [_, record] : m_data->micromaps) {
            m_data->destroyMicromap(m_data->device, record.micromap, nullptr);
        }
        for (const auto& record : m_data->deferredObjects) {
            destroyDeferredObject(*m_data, record);
        }
        m_data->deferredObjects.clear();
        for (auto& [_, record] : m_data->textures) {
            if (!record.swapchainOwned) {
                vmaDestroyImage(m_data->allocator, record.image, record.allocation);
            }
        }
        for (auto& [_, record] : m_data->buffers) {
            vmaDestroyBuffer(m_data->allocator, record.buffer, record.allocation);
        }
        for (const auto& record : m_data->deferredBuffers) {
            vmaDestroyBuffer(m_data->allocator, record.buffer, record.allocation);
        }
        for (const auto& record : m_data->deferredImages) {
            vmaDestroyImage(m_data->allocator, record.image, record.allocation);
        }
        // Shared texture memory blocks are freed after every image (placed
        // images alias them and must be destroyed first).
        for (auto& [_, record] : m_data->textureMemories) {
            vmaFreeMemory(m_data->allocator, record.allocation);
        }
        for (const auto& record : m_data->deferredMemories) {
            vmaFreeMemory(m_data->allocator, record.allocation);
        }
        for (auto& frame : m_data->frames) {
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_data->device, frame.imageAvailable, nullptr);
            }
            if (frame.fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_data->device, frame.fence, nullptr);
            }
        }
        for (const VkSemaphore timeline : m_data->queueTimelines) {
            if (timeline != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_data->device, timeline, nullptr);
            }
        }
        if (m_data->pipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(m_data->device, m_data->pipelineCache, nullptr);
        }
        if (m_data->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_data->device, m_data->descriptorPool, nullptr);
        }
        if (m_data->allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(m_data->allocator);
        }
    }
    destroyMainSurface(*m_data);
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (streamline.initialized() && !streamline.shutdown()) {
        std::cerr << streamline.lastError() << '\n';
    }
#endif
    if (m_data->device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_data->device, nullptr);
    }
    if (m_data->debugMessenger != VK_NULL_HANDLE && m_data->instance != VK_NULL_HANDLE) {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_data->instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger != nullptr) {
            destroyMessenger(m_data->instance, m_data->debugMessenger, nullptr);
        }
    }
    if (m_data->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_data->instance, nullptr);
    }
    m_data.reset();
    m_capabilities = {};
    m_deviceId = 0u;
    m_lastSubmittedSequence = 0u;
    m_initialized = false;
}

RhiBackend VkRhiDevice::backend() const {
    return RhiBackend::Vulkan;
}

const RhiCapabilities& VkRhiDevice::capabilities() const {
    return m_capabilities;
}

RhiMemoryStats VkRhiDevice::memoryStats() const {
    RhiMemoryStats stats;
    if (!m_initialized || m_data == nullptr) {
        return stats;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    stats.valid = true;
    stats.accuracy = RhiMemoryStatsAccuracy::Exact;
    for (const auto& [_, record] : m_data->buffers) {
        if (record.allocation == VK_NULL_HANDLE || record.allocationBytes == 0u ||
            !stats.add(record.desc.memoryCategory, record.allocationBytes, 1u, 1u)) {
            return {};
        }
    }
    for (const auto& [_, record] : m_data->textures) {
        const bool hasAllocation = record.allocation != VK_NULL_HANDLE;
        if ((hasAllocation && record.allocationBytes == 0u) ||
            !stats.add(record.desc.memoryCategory, record.allocationBytes, hasAllocation ? 1u : 0u, 1u)) {
            return {};
        }
    }
    for (const auto& [_, record] : m_data->textureMemories) {
        if (record.allocation == VK_NULL_HANDLE || record.sizeBytes == 0u ||
            !stats.add(record.category, record.sizeBytes, 1u, 0u)) {
            return {};
        }
    }
    if (!stats.add(RhiMemoryCategory::AccelerationStructure, 0u, 0u,
                   m_data->accelerationStructureHandles.liveCount() + m_data->micromapHandles.liveCount())) {
        return {};
    }
    return stats;
}

RhiBufferHandle VkRhiDevice::createBuffer(const RhiBufferDesc& desc, const void* initialData,
                                          const size_t initialDataSize) {
    constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
        rhiFlag(RhiBufferUsage::AccelerationStructureStorage) | rhiFlag(RhiBufferUsage::DeviceAddress);
    const bool accelerationStructureStorage =
        (desc.usage & rhiFlag(RhiBufferUsage::AccelerationStructureStorage)) != 0u;
    const bool accelerationStructureBuildInput =
        (desc.usage & rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput)) != 0u;
    const bool accelerationStructureBuildScratch =
        desc.initialState == RhiResourceState::AccelerationStructureBuildScratch;
    const bool micromapStorage = (desc.usage & rhiFlag(RhiBufferUsage::MicromapStorage)) != 0u;
    const bool micromapBuildInput = (desc.usage & rhiFlag(RhiBufferUsage::MicromapBuildInput)) != 0u;
    const bool micromapBuildScratch = desc.initialState == RhiResourceState::MicromapBuildScratch;
    if (!m_initialized || !rhiMemoryCategoryValid(desc.memoryCategory) || desc.size == 0u || desc.usage == 0u ||
        toVkBufferUsage(desc.usage) == 0u || (initialData == nullptr && initialDataSize != 0u) ||
        initialDataSize > desc.size) {
        return {};
    }
    if (((micromapStorage || micromapBuildInput || micromapBuildScratch) && !m_capabilities.opacityMicromap) ||
        (accelerationStructureStorage &&
         ((desc.usage & kAccelerationStructureStorageUsages) != kAccelerationStructureStorageUsages ||
          desc.memoryUsage != RhiMemoryUsage::GpuOnly)) ||
        (accelerationStructureBuildInput && (desc.usage & rhiFlag(RhiBufferUsage::DeviceAddress)) == 0u) ||
        (accelerationStructureBuildScratch &&
         ((desc.usage & (rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress))) !=
              (rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress)) ||
          desc.memoryUsage != RhiMemoryUsage::GpuOnly)) ||
        (micromapStorage && ((desc.usage & rhiFlag(RhiBufferUsage::DeviceAddress)) == 0u ||
                             desc.memoryUsage != RhiMemoryUsage::GpuOnly)) ||
        (micromapBuildInput && (desc.usage & rhiFlag(RhiBufferUsage::DeviceAddress)) == 0u) ||
        (micromapBuildScratch &&
         ((desc.usage & (rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress))) !=
              (rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress)) ||
          desc.memoryUsage != RhiMemoryUsage::GpuOnly))) {
        logRhiError("acceleration-structure buffers require explicit device-address usage");
        return {};
    }
    if (initialData != nullptr && desc.memoryUsage == RhiMemoryUsage::GpuOnly &&
        desc.initialState == RhiResourceState::Undefined) {
        return {};
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    if (initialData != nullptr) {
        bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = desc.memoryUsage == RhiMemoryUsage::GpuOnly ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                                                                       : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    if (accelerationStructureBuildScratch) {
        allocationInfo.minAlignment = m_capabilities.minAccelerationStructureScratchOffsetAlignment;
    } else if (micromapBuildInput || micromapBuildScratch) {
        allocationInfo.minAlignment = 256u;
    }
    if (desc.memoryUsage != RhiMemoryUsage::GpuOnly) {
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(vmaCreateBuffer(m_data->allocator, &bufferInfo, &allocationInfo, &buffer, &allocation,
                                     &nativeAllocationInfo),
                     "vmaCreateBuffer")) {
        return {};
    }
    if (initialData != nullptr) {
        if (desc.memoryUsage != RhiMemoryUsage::GpuOnly) {
            std::memcpy(nativeAllocationInfo.pMappedData, initialData, initialDataSize);
            vmaFlushAllocation(m_data->allocator, allocation, 0u, initialDataSize);
        } else {
            VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            stagingInfo.size = initialDataSize;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo stagingAllocationInfo{};
            stagingAllocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            stagingAllocationInfo.flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VkBuffer staging = VK_NULL_HANDLE;
            VmaAllocation stagingAllocation = VK_NULL_HANDLE;
            VmaAllocationInfo stagingNativeInfo{};
            if (!vkSucceeded(vmaCreateBuffer(m_data->allocator, &stagingInfo, &stagingAllocationInfo, &staging,
                                             &stagingAllocation, &stagingNativeInfo),
                             "vmaCreateBuffer(initial upload)")) {
                vmaDestroyBuffer(m_data->allocator, buffer, allocation);
                return {};
            }
            std::memcpy(stagingNativeInfo.pMappedData, initialData, initialDataSize);
            vmaFlushAllocation(m_data->allocator, stagingAllocation, 0u, initialDataSize);
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.queueFamilyIndex = m_data->queueFamilies.graphics;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkCommandPool commandPool = VK_NULL_HANDLE;
            if (vkCreateCommandPool(m_data->device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
                vmaDestroyBuffer(m_data->allocator, staging, stagingAllocation);
                vmaDestroyBuffer(m_data->allocator, buffer, allocation);
                return {};
            }
            pool = commandPool;
            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = pool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1u;
            vkAllocateCommandBuffers(m_data->device, &allocateInfo, &commandBuffer);
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &beginInfo);
            const VkBufferCopy region{0u, 0u, initialDataSize};
            vkCmdCopyBuffer(commandBuffer, staging, buffer, 1u, &region);
            const auto initialState = toVkResourceState(desc.initialState);
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = initialState.stages;
            barrier.dstAccessMask = initialState.access;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer;
            barrier.offset = 0u;
            barrier.size = initialDataSize;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1u;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &dependency);
            vkEndCommandBuffer(commandBuffer);
            const VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr,
                                                        commandBuffer, 0u};
            VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            submitInfo.commandBufferInfoCount = 1u;
            submitInfo.pCommandBufferInfos = &commandInfo;
            if (vkQueueSubmit2(m_data->graphicsQueue, 1u, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS ||
                vkQueueWaitIdle(m_data->graphicsQueue) != VK_SUCCESS) {
                vkDestroyCommandPool(m_data->device, pool, nullptr);
                vmaDestroyBuffer(m_data->allocator, staging, stagingAllocation);
                vmaDestroyBuffer(m_data->allocator, buffer, allocation);
                return {};
            }
            vkDestroyCommandPool(m_data->device, pool, nullptr);
            vmaDestroyBuffer(m_data->allocator, staging, stagingAllocation);
        }
    }
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiBufferHandle handle = m_data->bufferHandles.allocate();
    m_data->buffers.emplace(handleKey(handle), VkRhiDeviceData::Buffer{buffer,
                                                                       allocation,
                                                                       desc,
                                                                       desc.debugName != nullptr ? desc.debugName : "",
                                                                       nativeAllocationInfo.pMappedData,
                                                                       {},
                                                                       nativeAllocationInfo.size});
    nameObject(*m_data, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer), desc.debugName);
    return handle;
}

namespace {
/// Validates a texture description and fills the matching VkImageCreateInfo.
/// Shared by createTexture, createPlacedTexture, and the memory-requirements
/// query so a placed image is byte-identical to what was measured.
/// @return False when the description cannot form a valid Vulkan image.
bool fillImageCreateInfo(const RhiTextureDesc& desc, VkImageCreateInfo& imageInfo) {
    if (desc.width == 0u || desc.height == 0u || desc.depthOrLayers == 0u || desc.mipLevels == 0u ||
        toVkFormat(desc.format) == VK_FORMAT_UNDEFINED || toVkSampleCount(desc.sampleCount) == 0u) {
        return false;
    }
    const bool cube = desc.dimension == RhiTextureDimension::Cube;
    const bool cubeArray = desc.dimension == RhiTextureDimension::CubeArray;
    if ((cube || cubeArray) && (desc.width != desc.height || (cube && desc.depthOrLayers != 6u) ||
                                (cubeArray && desc.depthOrLayers % 6u != 0u))) {
        return false;
    }
    imageInfo = VkImageCreateInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags =
        cube || cubeArray ? VkImageCreateFlags{VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT} : VkImageCreateFlags{};
    imageInfo.imageType = toVkImageType(desc.dimension);
    imageInfo.format = toVkFormat(desc.format);
    imageInfo.extent = {desc.width, desc.height,
                        desc.dimension == RhiTextureDimension::Texture3D ? desc.depthOrLayers : 1u};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.dimension == RhiTextureDimension::Texture3D ? 1u : desc.depthOrLayers;
    imageInfo.samples = toVkSampleCount(desc.sampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return true;
}

/// Applies the requested logical queue-sharing contract to one Vulkan image.
/// @param desc Backend-independent texture description.
/// @param data Vulkan device state containing the selected queue families.
/// @param imageInfo Image description updated before creation or measurement.
/// @param queueFamilies Caller-owned storage retained through the Vulkan call.
void configureImageQueueSharing(const RhiTextureDesc& desc, const VkRhiDeviceData& data, VkImageCreateInfo& imageInfo,
                                std::array<uint32_t, 2>& queueFamilies) {
    if (desc.queueSharing != RhiTextureQueueSharing::GraphicsComputeConcurrent) {
        return;
    }
    queueFamilies[0] = data.queueFamilies.graphics;
    queueFamilies[1] = data.queueFamilies.compute;
    if (queueFamilies[0] == queueFamilies[1]) {
        return;
    }
    imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
    imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilies.size());
    imageInfo.pQueueFamilyIndices = queueFamilies.data();
}
} // namespace

RhiTextureHandle VkRhiDevice::createTexture(const RhiTextureDesc& desc, const RhiTextureInitialData* initialData) {
    VkImageCreateInfo imageInfo;
    if (!m_initialized || !rhiMemoryCategoryValid(desc.memoryCategory) || !fillImageCreateInfo(desc, imageInfo)) {
        return {};
    }
    std::array<uint32_t, 2> queueFamilies{};
    configureImageQueueSharing(desc, *m_data, imageInfo, queueFamilies);
    if (initialData != nullptr) {
        if (initialData->pixels == nullptr || initialData->sizeBytes == 0u) {
            return {};
        }
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(
            vmaCreateImage(m_data->allocator, &imageInfo, &allocationInfo, &image, &allocation, &nativeAllocationInfo),
            "vmaCreateImage")) {
        return {};
    }
    RhiTextureHandle handle;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        handle = m_data->textureHandles.allocate();
        m_data->textures.emplace(handleKey(handle),
                                 VkRhiDeviceData::Texture{image,
                                                          allocation,
                                                          desc,
                                                          desc.debugName != nullptr ? desc.debugName : "",
                                                          false,
                                                          false,
                                                          {},
                                                          nativeAllocationInfo.size});
    }
    nameObject(*m_data, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image), desc.debugName);
    if (initialData != nullptr && !uploadTextureInitialData(*m_data, image, desc, *initialData)) {
        std::cerr << "VkRhiDevice: texture initial data upload failed ["
                  << (desc.debugName != nullptr ? desc.debugName : "unnamed") << "]\n";
        destroyTexture(handle);
        return {};
    }
    return handle;
}

bool VkRhiDevice::getTextureMemoryRequirements(const RhiTextureDesc& desc, RhiTextureMemoryRequirements& requirements) {
    VkImageCreateInfo imageInfo;
    if (!m_initialized || !fillImageCreateInfo(desc, imageInfo)) {
        return false;
    }
    std::array<uint32_t, 2> queueFamilies{};
    configureImageQueueSharing(desc, *m_data, imageInfo, queueFamilies);
    VkDeviceImageMemoryRequirements query{VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS};
    query.pCreateInfo = &imageInfo;
    VkMemoryRequirements2 memoryRequirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceImageMemoryRequirements(m_data->device, &query, &memoryRequirements);
    requirements.sizeBytes = memoryRequirements.memoryRequirements.size;
    requirements.alignment = memoryRequirements.memoryRequirements.alignment;
    requirements.memoryTypeBits = memoryRequirements.memoryRequirements.memoryTypeBits;
    return requirements.sizeBytes != 0u && requirements.memoryTypeBits != 0u;
}

RhiMemoryHandle VkRhiDevice::allocateTextureMemory(const RhiTextureMemoryRequirements& requirements,
                                                   const RhiMemoryCategory category, const char* debugName) {
    if (!m_initialized || !rhiMemoryCategoryValid(category) || requirements.sizeBytes == 0u ||
        requirements.memoryTypeBits == 0u) {
        return {};
    }
    VkMemoryRequirements memoryRequirements{};
    memoryRequirements.size = requirements.sizeBytes;
    memoryRequirements.alignment = std::max<uint64_t>(1u, requirements.alignment);
    memoryRequirements.memoryTypeBits = requirements.memoryTypeBits;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
    allocationInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(vmaAllocateMemory(m_data->allocator, &memoryRequirements, &allocationInfo, &allocation,
                                       &nativeAllocationInfo),
                     "vmaAllocateMemory")) {
        return {};
    }
    if (debugName != nullptr) {
        vmaSetAllocationName(m_data->allocator, allocation, debugName);
    }
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiMemoryHandle handle = m_data->textureMemoryHandles.allocate();
    m_data->textureMemories.emplace(handleKey(handle),
                                    VkRhiDeviceData::TextureMemory{allocation, nativeAllocationInfo.size, category,
                                                                   debugName != nullptr ? debugName : ""});
    return handle;
}

RhiTextureHandle VkRhiDevice::createPlacedTexture(const RhiTextureDesc& desc, const RhiMemoryHandle memory) {
    if (!m_initialized || !rhiMemoryCategoryValid(desc.memoryCategory)) {
        return {};
    }
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto memoryIt = m_data->textureMemories.find(handleKey(memory));
    if (memoryIt == m_data->textureMemories.end()) {
        return {};
    }
    VkImageCreateInfo imageInfo;
    if (!fillImageCreateInfo(desc, imageInfo)) {
        return {};
    }
    std::array<uint32_t, 2> queueFamilies{};
    configureImageQueueSharing(desc, *m_data, imageInfo, queueFamilies);
    // A block accepted at allocation time may still be too small or of an
    // incompatible type for this description; re-check before binding.
    VkDeviceImageMemoryRequirements query{VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS};
    query.pCreateInfo = &imageInfo;
    VkMemoryRequirements2 memoryRequirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetDeviceImageMemoryRequirements(m_data->device, &query, &memoryRequirements);
    VmaAllocationInfo allocationInfo{};
    vmaGetAllocationInfo(m_data->allocator, memoryIt->second.allocation, &allocationInfo);
    if (memoryRequirements.memoryRequirements.size > memoryIt->second.sizeBytes ||
        (memoryRequirements.memoryRequirements.memoryTypeBits & (1u << allocationInfo.memoryType)) == 0u ||
        allocationInfo.offset % std::max<uint64_t>(1u, memoryRequirements.memoryRequirements.alignment) != 0u) {
        return {};
    }
    VkImage image = VK_NULL_HANDLE;
    if (!vkSucceeded(vmaCreateAliasingImage(m_data->allocator, memoryIt->second.allocation, &imageInfo, &image),
                     "vmaCreateAliasingImage")) {
        return {};
    }
    // A null allocation marks the record as placed: destroyTexture's deferred
    // path then destroys only the image and leaves the shared block alive.
    const RhiTextureHandle handle = m_data->textureHandles.allocate();
    m_data->textures.emplace(
        handleKey(handle),
        VkRhiDeviceData::Texture{
            image, VK_NULL_HANDLE, desc, desc.debugName != nullptr ? desc.debugName : "", false, false, {}, 0u});
    nameObject(*m_data, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image), desc.debugName);
    return handle;
}

void VkRhiDevice::destroyTextureMemory(const RhiMemoryHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->textureMemories.find(handleKey(handle));
        if (it == m_data->textureMemories.end() || !m_data->textureMemoryHandles.release(handle)) {
            return;
        }
        // Placed textures do not stamp per-resource lifetimes onto the block, so
        // conservatively wait for everything submitted up to this point.
        enqueueDeferred(m_data->deferredMemories,
                        VkRhiDeviceData::DeferredMemory{m_lastSubmittedSequence, it->second.allocation});
        m_data->textureMemories.erase(it);
    }
    reclaimCompletedWork();
}

bool VkRhiDevice::getBufferDesc(const RhiBufferHandle buffer, RhiBufferDesc& desc) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->buffers, buffer) : nullptr;
    if (!m_initialized || record == nullptr)
        return false;
    desc = record->desc;
    desc.debugName = record->debugName.c_str();
    return true;
}

bool VkRhiDevice::getTextureDesc(const RhiTextureHandle texture, RhiTextureDesc& desc) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->textures, texture) : nullptr;
    if (!m_initialized || record == nullptr)
        return false;
    desc = record->desc;
    desc.debugName = record->debugName.c_str();
    return true;
}

bool VkRhiDevice::getTextureViewDesc(const RhiTextureViewHandle textureView, RhiTextureViewDesc& desc) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->textureViews, textureView) : nullptr;
    if (!m_initialized || record == nullptr)
        return false;
    desc = record->desc;
    return true;
}

bool VkRhiDevice::getSamplerDesc(const RhiSamplerHandle sampler, RhiSamplerDesc& desc) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->samplers, sampler) : nullptr;
    if (!m_initialized || record == nullptr)
        return false;
    desc = record->desc;
    return true;
}

RhiAccelerationStructureHandle VkRhiDevice::createAccelerationStructure(const RhiAccelerationStructureDesc& desc) {
    if (!m_initialized || m_data == nullptr || desc.size == 0u || desc.offset % 256u != 0u ||
        toVkAccelerationStructureType(desc.type) == VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR) {
        return {};
    }
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* buffer = findRecord(m_data->buffers, desc.buffer);
    constexpr RhiBufferUsageFlags kRequiredUsages =
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
    if (buffer == nullptr || !bufferHasUsages(*buffer, kRequiredUsages) ||
        buffer->desc.memoryCategory != RhiMemoryCategory::AccelerationStructure ||
        !rangeFits(buffer->desc.size, desc.offset, desc.size)) {
        return {};
    }
    const bool overlaps = std::any_of(
        m_data->accelerationStructures.begin(), m_data->accelerationStructures.end(), [&](const auto& entry) {
            const RhiAccelerationStructureDesc& existing = entry.second.desc;
            return existing.buffer.index == desc.buffer.index && existing.buffer.generation == desc.buffer.generation &&
                   rangesOverlap(desc.offset, desc.size, existing.offset, existing.size);
        });
    const bool overlapsMicromap =
        std::any_of(m_data->micromaps.begin(), m_data->micromaps.end(), [&](const auto& entry) {
            const RhiMicromapDesc& existing = entry.second.desc;
            return existing.buffer.index == desc.buffer.index && existing.buffer.generation == desc.buffer.generation &&
                   rangesOverlap(desc.offset, desc.size, existing.offset, existing.size);
        });
    const bool overlapsDeferred =
        std::any_of(m_data->deferredObjects.begin(), m_data->deferredObjects.end(), [&](const auto& item) {
            return (item.type == VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR ||
                    item.type == VK_OBJECT_TYPE_MICROMAP_EXT) &&
                   item.accelerationStructureBuffer.index == desc.buffer.index &&
                   item.accelerationStructureBuffer.generation == desc.buffer.generation &&
                   rangesOverlap(desc.offset, desc.size, item.accelerationStructureOffset,
                                 item.accelerationStructureSize);
        });
    if (overlaps || overlapsMicromap || overlapsDeferred) {
        logRhiError("createAccelerationStructure received an overlapping backing-buffer range");
        return {};
    }

    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = buffer->buffer;
    createInfo.offset = desc.offset;
    createInfo.size = desc.size;
    createInfo.type = toVkAccelerationStructureType(desc.type);
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    if (!vkSucceeded(m_data->createAccelerationStructure(m_data->device, &createInfo, nullptr, &accelerationStructure),
                     "vkCreateAccelerationStructureKHR")) {
        return {};
    }

    const RhiAccelerationStructureHandle handle = m_data->accelerationStructureHandles.allocate();
    VkRhiDeviceData::AccelerationStructure record;
    record.accelerationStructure = accelerationStructure;
    record.desc = desc;
    record.debugName = desc.debugName != nullptr ? desc.debugName : "";
    const auto insertion = m_data->accelerationStructures.emplace(handleKey(handle), std::move(record));
    insertion.first->second.desc.debugName = insertion.first->second.debugName.c_str();
    nameObject(*m_data, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, reinterpret_cast<uint64_t>(accelerationStructure),
               desc.debugName);
    return handle;
}

bool VkRhiDevice::getAccelerationStructureDesc(const RhiAccelerationStructureHandle accelerationStructure,
                                               RhiAccelerationStructureDesc& desc) const {
    if (!m_initialized || m_data == nullptr) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_data->accelerationStructures, accelerationStructure);
    if (record == nullptr) {
        return false;
    }
    desc = record->desc;
    desc.debugName = record->debugName.c_str();
    return true;
}

RhiMicromapHandle VkRhiDevice::createMicromap(const RhiMicromapDesc& desc) {
    if (!m_initialized || m_data == nullptr || !m_capabilities.opacityMicromap || desc.size == 0u ||
        desc.offset % 256u != 0u) {
        return {};
    }
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* buffer = findRecord(m_data->buffers, desc.buffer);
    constexpr RhiBufferUsageFlags kRequiredUsages =
        rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::MicromapStorage);
    if (buffer == nullptr || !bufferHasUsages(*buffer, kRequiredUsages) ||
        buffer->desc.memoryCategory != RhiMemoryCategory::AccelerationStructure ||
        !rangeFits(buffer->desc.size, desc.offset, desc.size)) {
        return {};
    }
    const bool overlaps = std::any_of(m_data->micromaps.begin(), m_data->micromaps.end(), [&](const auto& entry) {
        const RhiMicromapDesc& existing = entry.second.desc;
        return existing.buffer.index == desc.buffer.index && existing.buffer.generation == desc.buffer.generation &&
               rangesOverlap(desc.offset, desc.size, existing.offset, existing.size);
    });
    const bool overlapsAccelerationStructure = std::any_of(
        m_data->accelerationStructures.begin(), m_data->accelerationStructures.end(), [&](const auto& entry) {
            const RhiAccelerationStructureDesc& existing = entry.second.desc;
            return existing.buffer.index == desc.buffer.index && existing.buffer.generation == desc.buffer.generation &&
                   rangesOverlap(desc.offset, desc.size, existing.offset, existing.size);
        });
    const bool overlapsDeferred =
        std::any_of(m_data->deferredObjects.begin(), m_data->deferredObjects.end(), [&](const auto& item) {
            return (item.type == VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR ||
                    item.type == VK_OBJECT_TYPE_MICROMAP_EXT) &&
                   item.accelerationStructureBuffer.index == desc.buffer.index &&
                   item.accelerationStructureBuffer.generation == desc.buffer.generation &&
                   rangesOverlap(desc.offset, desc.size, item.accelerationStructureOffset,
                                 item.accelerationStructureSize);
        });
    if (overlaps || overlapsAccelerationStructure || overlapsDeferred) {
        logRhiError("createMicromap received an overlapping backing-buffer range");
        return {};
    }

    VkMicromapCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
    createInfo.buffer = buffer->buffer;
    createInfo.offset = desc.offset;
    createInfo.size = desc.size;
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    VkMicromapEXT micromap = VK_NULL_HANDLE;
    if (!vkSucceeded(m_data->createMicromap(m_data->device, &createInfo, nullptr, &micromap), "vkCreateMicromapEXT")) {
        return {};
    }

    const RhiMicromapHandle handle = m_data->micromapHandles.allocate();
    VkRhiDeviceData::Micromap record;
    record.micromap = micromap;
    record.desc = desc;
    record.debugName = desc.debugName != nullptr ? desc.debugName : "";
    const auto insertion = m_data->micromaps.emplace(handleKey(handle), std::move(record));
    insertion.first->second.desc.debugName = insertion.first->second.debugName.c_str();
    nameObject(*m_data, VK_OBJECT_TYPE_MICROMAP_EXT, reinterpret_cast<uint64_t>(micromap), desc.debugName);
    return handle;
}

bool VkRhiDevice::getMicromapDesc(const RhiMicromapHandle micromap, RhiMicromapDesc& desc) const {
    if (!m_initialized || m_data == nullptr) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_data->micromaps, micromap);
    if (record == nullptr) {
        return false;
    }
    desc = record->desc;
    desc.debugName = record->debugName.c_str();
    return true;
}

bool VkRhiDevice::queryMicromapBuildSizes(const RhiMicromapBuildInput& input, RhiMicromapBuildSizes& sizes) const {
    if (!m_initialized || m_data == nullptr || m_data->getMicromapBuildSizes == nullptr) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    NativeMicromapBuildInput native;
    if (!fillNativeMicromapBuildInput(*m_data, m_capabilities, input, native, nullptr)) {
        return false;
    }
    VkMicromapBuildSizesInfoEXT nativeSizes{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
    m_data->getMicromapBuildSizes(m_data->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &native.info,
                                  &nativeSizes);
    if (nativeSizes.micromapSize == 0u || nativeSizes.buildScratchSize == 0u) {
        return false;
    }
    sizes = {nativeSizes.micromapSize, nativeSizes.buildScratchSize};
    return true;
}

bool VkRhiDevice::queryAccelerationStructureBuildSizes(const RhiAccelerationStructureBuildInput& input,
                                                       RhiAccelerationStructureBuildSizes& sizes) const {
    if (!m_initialized || m_data == nullptr) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    NativeAccelerationStructureBuildInput native;
    if (!fillNativeAccelerationStructureBuildInput(*m_data, m_capabilities, input, native, nullptr)) {
        return false;
    }
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = toVkAccelerationStructureType(input.type);
    buildInfo.flags = toVkAccelerationStructureBuildFlags(input.flags);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = input.geometryCount;
    buildInfo.pGeometries = native.geometries.data();
    VkAccelerationStructureBuildSizesInfoKHR nativeSizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_data->getAccelerationStructureBuildSizes(m_data->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                               &buildInfo, native.primitiveCounts.data(), &nativeSizes);
    if (nativeSizes.accelerationStructureSize == 0u || nativeSizes.buildScratchSize == 0u) {
        return false;
    }
    sizes = {nativeSizes.accelerationStructureSize, nativeSizes.buildScratchSize, nativeSizes.updateScratchSize};
    return true;
}

uint64_t VkRhiDevice::bufferDeviceAddress(const RhiBufferHandle buffer) const {
    if (!m_initialized || m_data == nullptr) {
        return 0u;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_data->buffers, buffer);
    if (record == nullptr || (record->desc.usage & rhiFlag(RhiBufferUsage::DeviceAddress)) == 0u) {
        return 0u;
    }
    return nativeBufferDeviceAddress(*m_data, *record);
}

uint64_t
VkRhiDevice::accelerationStructureDeviceAddress(const RhiAccelerationStructureHandle accelerationStructure) const {
    if (!m_initialized || m_data == nullptr) {
        return 0u;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_data->accelerationStructures, accelerationStructure);
    if (record == nullptr) {
        return 0u;
    }
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = record->accelerationStructure;
    return m_data->getAccelerationStructureDeviceAddress(m_data->device, &addressInfo);
}

RhiTextureViewHandle VkRhiDevice::createTextureView(const RhiTextureViewDesc& desc) {
    // Exclusive: the texture record pointer is used across the native view
    // creation and the registry insertion below.
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* texture = m_data != nullptr ? findRecord(m_data->textures, desc.texture) : nullptr;
    if (texture == nullptr)
        return {};
    const bool texture3D = texture->desc.dimension == RhiTextureDimension::Texture3D;
    const uint32_t textureLayers = texture->desc.depthOrLayers;
    const uint32_t mipCount = desc.mipCount == kRhiRemainingMipLevels
                                  ? texture->desc.mipLevels - std::min(desc.baseMip, texture->desc.mipLevels)
                                  : desc.mipCount;
    const uint32_t layerCount = desc.layerCount == kRhiRemainingArrayLayers
                                    ? textureLayers - std::min(desc.baseLayer, textureLayers)
                                    : desc.layerCount;
    if (desc.baseMip >= texture->desc.mipLevels || mipCount == 0u ||
        mipCount > texture->desc.mipLevels - desc.baseMip || desc.baseLayer >= textureLayers || layerCount == 0u ||
        layerCount > textureLayers - desc.baseLayer) {
        return {};
    }
    if (texture3D && (desc.viewType != RhiTextureViewType::Texture3D || desc.baseLayer != 0u ||
                      layerCount != texture->desc.depthOrLayers)) {
        return {};
    }
    const bool cubeView = desc.viewType == RhiTextureViewType::Cube;
    const bool cubeArrayView = desc.viewType == RhiTextureViewType::CubeArray;
    const bool cubeTexture = texture->desc.dimension == RhiTextureDimension::Cube ||
                             texture->desc.dimension == RhiTextureDimension::CubeArray;
    if ((cubeView || cubeArrayView) && (!cubeTexture || desc.baseLayer % 6u != 0u || (cubeView && layerCount != 6u) ||
                                        (cubeArrayView && layerCount % 6u != 0u))) {
        return {};
    }
    const RhiTextureFormat format = desc.format == RhiTextureFormat::Undefined ? texture->desc.format : desc.format;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = texture->image;
    info.viewType = toVkImageViewType(desc.viewType);
    info.format = toVkFormat(format);
    info.subresourceRange.aspectMask = defaultAspectForFormat(format);
    info.subresourceRange.baseMipLevel = desc.baseMip;
    info.subresourceRange.levelCount = mipCount;
    info.subresourceRange.baseArrayLayer = texture3D ? 0u : desc.baseLayer;
    info.subresourceRange.layerCount = texture3D ? 1u : layerCount;
    VkImageView view = VK_NULL_HANDLE;
    if (info.viewType == VK_IMAGE_VIEW_TYPE_MAX_ENUM || info.format == VK_FORMAT_UNDEFINED ||
        !vkSucceeded(vkCreateImageView(m_data->device, &info, nullptr, &view), "vkCreateImageView")) {
        return {};
    }
    const RhiTextureViewHandle handle = m_data->textureViewHandles.allocate();
    m_data->textureViews.emplace(handleKey(handle), VkRhiDeviceData::TextureView{view, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(view), texture->debugName.c_str());
    return handle;
}

RhiSamplerHandle VkRhiDevice::createSampler(const RhiSamplerDesc& desc) {
    if (!m_initialized || desc.maxAnisotropy < 1.0f || desc.maxAnisotropy > m_capabilities.maxSamplerAnisotropy)
        return {};
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = toVkFilter(desc.magFilter);
    info.minFilter = toVkFilter(desc.minFilter);
    info.mipmapMode = toVkMipmapMode(desc.mipmapMode);
    info.addressModeU = toVkAddressMode(desc.addressU);
    info.addressModeV = toVkAddressMode(desc.addressV);
    info.addressModeW = toVkAddressMode(desc.addressW);
    info.anisotropyEnable = desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = desc.maxAnisotropy;
    info.compareEnable = desc.compareEnabled ? VK_TRUE : VK_FALSE;
    info.compareOp = toVkCompareOp(desc.compareOp);
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.borderColor = toVkBorderColor(desc.borderColor);
    VkSampler sampler = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateSampler(m_data->device, &info, nullptr, &sampler), "vkCreateSampler"))
        return {};
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiSamplerHandle handle = m_data->samplerHandles.allocate();
    m_data->samplers.emplace(handleKey(handle), VkRhiDeviceData::Sampler{sampler, desc});
    return handle;
}

RhiShaderHandle VkRhiDevice::createShader(const RhiShaderDesc& desc) {
    if (!m_initialized)
        return {};
    std::string error;
    auto compiled = renderer::rhi::compileShaderToSpirv(desc, renderer::rhi::RhiShaderBackend::Vulkan, error);
    if (!compiled.has_value()) {
        std::cerr << "VkRhiDevice: shader compilation failed: " << error << '\n';
        return {};
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = compiled->spirv.size() * sizeof(uint32_t);
    info.pCode = compiled->spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateShaderModule(m_data->device, &info, nullptr, &module), "vkCreateShaderModule"))
        return {};
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiShaderHandle handle = m_data->shaderHandles.allocate();
    m_data->shaders.emplace(handleKey(handle), VkRhiDeviceData::Shader{module, compiled->stage, compiled->entryPoint,
                                                                       std::move(compiled->reflection)});
    nameObject(*m_data, VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(module), desc.debugName);
    return handle;
}

RhiBindGroupLayoutHandle VkRhiDevice::createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) {
    if (!m_initialized)
        return {};
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    bindings.reserve(desc.entries.size());
    bindingFlags.reserve(desc.entries.size());
    std::set<uint32_t> usedBindings;
    std::optional<uint32_t> variableBinding;
    uint32_t maximumBinding = 0u;
    bool updateAfterBind = false;
    constexpr RhiBindingFlags kKnownFlags =
        rhiFlag(RhiBindingFlag::PartiallyBound) | rhiFlag(RhiBindingFlag::UpdateAfterBind) |
        rhiFlag(RhiBindingFlag::UpdateUnusedWhilePending) | rhiFlag(RhiBindingFlag::VariableDescriptorCount);
    for (const auto& entry : desc.entries) {
        const VkDescriptorType type = toVkDescriptorType(entry.type);
        if (entry.arrayCount == 0u || entry.stages == 0u || type == VK_DESCRIPTOR_TYPE_MAX_ENUM ||
            (entry.flags & ~kKnownFlags) != 0u || !usedBindings.insert(entry.binding).second)
            return {};
        if (rhiHasBindingFlag(entry.flags, RhiBindingFlag::PartiallyBound) &&
            !m_capabilities.descriptorBindingPartiallyBound)
            return {};
        if (rhiHasBindingFlag(entry.flags, RhiBindingFlag::VariableDescriptorCount)) {
            if (!m_capabilities.descriptorBindingVariableDescriptorCount || variableBinding.has_value())
                return {};
            variableBinding = entry.binding;
        }
        if (rhiHasBindingFlag(entry.flags, RhiBindingFlag::UpdateUnusedWhilePending) &&
            !m_capabilities.descriptorBindingUpdateUnusedWhilePending)
            return {};
        if (rhiHasBindingFlag(entry.flags, RhiBindingFlag::UpdateAfterBind)) {
            const bool supported =
                (entry.type == RhiBindingType::UniformBuffer &&
                 m_capabilities.descriptorBindingUniformBufferUpdateAfterBind) ||
                ((entry.type == RhiBindingType::SampledTexture || entry.type == RhiBindingType::Sampler ||
                  entry.type == RhiBindingType::CombinedTextureSampler) &&
                 m_capabilities.descriptorBindingSampledImageUpdateAfterBind) ||
                (entry.type == RhiBindingType::StorageTexture &&
                 m_capabilities.descriptorBindingStorageImageUpdateAfterBind) ||
                (entry.type == RhiBindingType::StorageBuffer &&
                 m_capabilities.descriptorBindingStorageBufferUpdateAfterBind) ||
                (entry.type == RhiBindingType::AccelerationStructure &&
                 m_capabilities.descriptorBindingAccelerationStructureUpdateAfterBind);
            if (!supported)
                return {};
            updateAfterBind = true;
        }
        maximumBinding = std::max(maximumBinding, entry.binding);
        bindings.push_back({entry.binding, type, entry.arrayCount, toVkShaderStageFlags(entry.stages), nullptr});
        bindingFlags.push_back(toVkDescriptorBindingFlags(entry.flags));
    }
    if (variableBinding.has_value() && *variableBinding != maximumBinding)
        return {};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = bindingFlags.empty() ? nullptr : &bindingFlagsInfo;
    info.flags =
        updateAfterBind
            ? static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
            : VkDescriptorSetLayoutCreateFlags{0u};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateDescriptorSetLayout(m_data->device, &info, nullptr, &layout),
                     "vkCreateDescriptorSetLayout"))
        return {};
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiBindGroupLayoutHandle handle = m_data->bindGroupLayoutHandles.allocate();
    m_data->bindGroupLayouts.emplace(handleKey(handle), VkRhiDeviceData::BindGroupLayout{layout, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(layout), desc.debugName);
    return handle;
}

RhiPipelineLayoutHandle VkRhiDevice::createPipelineLayout(const RhiPipelineLayoutDesc& desc) {
    if (!m_initialized)
        return {};
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(desc.bindGroupLayouts.size());
    for (const auto handle : desc.bindGroupLayouts) {
        const auto* record = findRecord(m_data->bindGroupLayouts, handle);
        if (record == nullptr)
            return {};
        layouts.push_back(record->layout);
    }
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = toVkShaderStageFlags(desc.pushConstantStages);
    pushRange.size = desc.pushConstantBytes;
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    if (desc.pushConstantBytes != 0u) {
        if (pushRange.stageFlags == 0u || desc.pushConstantBytes > m_data->properties.limits.maxPushConstantsSize)
            return {};
        info.pushConstantRangeCount = 1u;
        info.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreatePipelineLayout(m_data->device, &info, nullptr, &layout), "vkCreatePipelineLayout"))
        return {};
    const RhiPipelineLayoutHandle handle = m_data->pipelineLayoutHandles.allocate();
    m_data->pipelineLayouts.emplace(handleKey(handle), VkRhiDeviceData::PipelineLayout{layout, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(layout), desc.debugName);
    return handle;
}

RhiPipelineHandle VkRhiDevice::createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) {
    // Exclusive: shader and layout record pointers stay live across the
    // native pipeline creation and the registry insertion below.
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* vertex = m_data != nullptr ? findRecord(m_data->shaders, desc.vertexShader) : nullptr;
    const auto* fragment = m_data != nullptr ? findRecord(m_data->shaders, desc.fragmentShader) : nullptr;
    const auto* layout = m_data != nullptr ? findRecord(m_data->pipelineLayouts, desc.layout) : nullptr;
    if (vertex == nullptr || fragment == nullptr || layout == nullptr || vertex->stage != RhiShaderStage::Vertex ||
        fragment->stage != RhiShaderStage::Fragment || desc.colorFormats.size() > m_capabilities.maxColorAttachments)
        return {};
    const std::array<VkPipelineShaderStageCreateInfo, 2u> stages{
        {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT, vertex->module,
          vertex->entryPoint.c_str(), nullptr},
         {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT,
          fragment->module, fragment->entryPoint.c_str(), nullptr}}};
    std::vector<VkVertexInputBindingDescription> bindings;
    bindings.reserve(desc.vertexInput.bindings.size());
    for (const auto& binding : desc.vertexInput.bindings) {
        bindings.push_back({binding.binding, binding.stride,
                            binding.inputRate == RhiVertexInputRate::Vertex ? VK_VERTEX_INPUT_RATE_VERTEX
                                                                            : VK_VERTEX_INPUT_RATE_INSTANCE});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.vertexInput.attributes.size());
    for (const auto& attribute : desc.vertexInput.attributes) {
        const VkFormat format = toVkVertexFormat(attribute.format);
        if (format == VK_FORMAT_UNDEFINED)
            return {};
        attributes.push_back({attribute.location, attribute.binding, format, attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = toVkPrimitiveTopology(desc.topology);
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineViewportDepthClipControlCreateInfoEXT depthClipControl{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT};
    depthClipControl.negativeOneToOne = VK_TRUE;
    viewportState.pNext = &depthClipControl;
    viewportState.viewportCount = 1u;
    viewportState.scissorCount = 1u;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = desc.raster.depthClampEnabled ? VK_TRUE : VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = toVkCullMode(desc.raster.cullMode);
    raster.frontFace = toVkFrontFace(desc.raster.frontFace);
    raster.depthBiasEnable = desc.raster.depthBiasEnabled ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
    raster.depthBiasSlopeFactor = desc.raster.depthBiasSlopeFactor;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompare);
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    blendAttachments.reserve(desc.colorFormats.size());
    for (size_t i = 0u; i < desc.colorFormats.size(); ++i) {
        RhiBlendAttachmentState source{};
        if (i < desc.blend.attachments.size())
            source = desc.blend.attachments[i];
        VkPipelineColorBlendAttachmentState target{};
        target.blendEnable = source.blendEnabled ? VK_TRUE : VK_FALSE;
        target.srcColorBlendFactor = toVkBlendFactor(source.srcColor);
        target.dstColorBlendFactor = toVkBlendFactor(source.dstColor);
        target.colorBlendOp = toVkBlendOp(source.colorOp);
        target.srcAlphaBlendFactor = toVkBlendFactor(source.srcAlpha);
        target.dstAlphaBlendFactor = toVkBlendFactor(source.dstAlpha);
        target.alphaBlendOp = toVkBlendOp(source.alphaOp);
        target.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(target);
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    const std::array<VkDynamicState, 2u> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (const auto format : desc.colorFormats) {
        const VkFormat native = toVkFormat(format);
        if (native == VK_FORMAT_UNDEFINED)
            return {};
        colorFormats.push_back(native);
    }
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = toVkFormat(desc.depthFormat);
    rendering.stencilAttachmentFormat =
        desc.depthFormat == RhiTextureFormat::Depth24Stencil8 ? rendering.depthAttachmentFormat : VK_FORMAT_UNDEFINED;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout->layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateGraphicsPipelines(m_data->device, m_data->pipelineCache, 1u, &info, nullptr, &pipeline),
                     "vkCreateGraphicsPipelines"))
        return {};
    const RhiPipelineHandle handle = m_data->pipelineHandles.allocate();
    m_data->pipelines.emplace(handleKey(handle),
                              VkRhiDeviceData::Pipeline{pipeline, VK_PIPELINE_BIND_POINT_GRAPHICS, desc.layout});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline), desc.debugName);
    return handle;
}

RhiPipelineHandle VkRhiDevice::createComputePipeline(const RhiComputePipelineDesc& desc) {
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* shader = m_data != nullptr ? findRecord(m_data->shaders, desc.computeShader) : nullptr;
    const auto* layout = m_data != nullptr ? findRecord(m_data->pipelineLayouts, desc.layout) : nullptr;
    if (shader == nullptr || layout == nullptr || shader->stage != RhiShaderStage::Compute)
        return {};
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  nullptr,
                  0u,
                  VK_SHADER_STAGE_COMPUTE_BIT,
                  shader->module,
                  shader->entryPoint.c_str(),
                  nullptr};
    info.layout = layout->layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateComputePipelines(m_data->device, m_data->pipelineCache, 1u, &info, nullptr, &pipeline),
                     "vkCreateComputePipelines"))
        return {};
    const RhiPipelineHandle handle = m_data->pipelineHandles.allocate();
    m_data->pipelines.emplace(handleKey(handle),
                              VkRhiDeviceData::Pipeline{pipeline, VK_PIPELINE_BIND_POINT_COMPUTE, desc.layout});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline), desc.debugName);
    return handle;
}

RhiBindGroupHandle VkRhiDevice::createBindGroup(const RhiBindGroupDesc& desc) {
    // Exclusive: also serializes descriptor-set allocation from the shared
    // descriptor pool, which Vulkan requires to be externally synchronized.
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* layout = m_data != nullptr ? findRecord(m_data->bindGroupLayouts, desc.layout) : nullptr;
    if (layout == nullptr)
        return {};
    const auto variableEntryIt =
        std::find_if(layout->desc.entries.begin(), layout->desc.entries.end(), [](const auto& entry) {
            return rhiHasBindingFlag(entry.flags, RhiBindingFlag::VariableDescriptorCount);
        });
    if ((variableEntryIt == layout->desc.entries.end() && desc.variableDescriptorCount != 0u) ||
        (variableEntryIt != layout->desc.entries.end() &&
         (desc.variableDescriptorCount == 0u || desc.variableDescriptorCount > variableEntryIt->arrayCount)))
        return {};
    RhiBindGroupDesc storedDesc = desc;
    std::sort(storedDesc.entries.begin(), storedDesc.entries.end(),
              [](const RhiBindGroupEntry& lhs, const RhiBindGroupEntry& rhs) {
                  return lhs.binding != rhs.binding ? lhs.binding < rhs.binding : lhs.arrayElement < rhs.arrayElement;
              });
    const auto duplicate =
        std::adjacent_find(storedDesc.entries.begin(), storedDesc.entries.end(),
                           [](const RhiBindGroupEntry& lhs, const RhiBindGroupEntry& rhs) {
                               return lhs.binding == rhs.binding && lhs.arrayElement == rhs.arrayElement;
                           });
    if (duplicate != storedDesc.entries.end())
        return {};
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
    variableCountInfo.descriptorSetCount = 1u;
    variableCountInfo.pDescriptorCounts = &desc.variableDescriptorCount;
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.pNext = variableEntryIt == layout->desc.entries.end() ? nullptr : &variableCountInfo;
    allocateInfo.descriptorPool = m_data->descriptorPool;
    allocateInfo.descriptorSetCount = 1u;
    allocateInfo.pSetLayouts = &layout->layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!vkSucceeded(vkAllocateDescriptorSets(m_data->device, &allocateInfo, &set), "vkAllocateDescriptorSets"))
        return {};
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkAccelerationStructureKHR> accelerationStructureHandles;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfos;
    writes.reserve(storedDesc.entries.size());
    bufferInfos.reserve(storedDesc.entries.size());
    imageInfos.reserve(storedDesc.entries.size());
    accelerationStructureHandles.reserve(storedDesc.entries.size());
    accelerationStructureInfos.reserve(storedDesc.entries.size());
    std::vector<uint32_t> writtenBindingCounts(layout->desc.entries.size(), 0u);
    for (const auto& entry : storedDesc.entries) {
        const auto layoutEntryIt = std::find_if(layout->desc.entries.begin(), layout->desc.entries.end(),
                                                [&](const auto& item) { return item.binding == entry.binding; });
        const uint32_t descriptorCount =
            layoutEntryIt != layout->desc.entries.end() &&
                    rhiHasBindingFlag(layoutEntryIt->flags, RhiBindingFlag::VariableDescriptorCount)
                ? desc.variableDescriptorCount
                : (layoutEntryIt != layout->desc.entries.end() ? layoutEntryIt->arrayCount : 0u);
        if (layoutEntryIt == layout->desc.entries.end() || entry.arrayElement >= descriptorCount) {
            vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
            return {};
        }
        ++writtenBindingCounts[static_cast<size_t>(layoutEntryIt - layout->desc.entries.begin())];
        const VkDescriptorType type = toVkDescriptorType(layoutEntryIt->type);
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = entry.binding;
        write.dstArrayElement = entry.arrayElement;
        write.descriptorCount = 1u;
        write.descriptorType = type;
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            const auto* buffer = findRecord(m_data->buffers, entry.resource.buffer.buffer);
            if (buffer == nullptr) {
                vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
                return {};
            }
            bufferInfos.push_back({buffer->buffer, entry.resource.buffer.offset,
                                   entry.resource.buffer.range == 0u ? VK_WHOLE_SIZE : entry.resource.buffer.range});
            write.pBufferInfo = &bufferInfos.back();
        } else if (type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
            const auto* accelerationStructure =
                findRecord(m_data->accelerationStructures, entry.resource.accelerationStructure);
            if (accelerationStructure == nullptr ||
                accelerationStructure->desc.type != RhiAccelerationStructureType::TopLevel) {
                vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
                return {};
            }
            accelerationStructureHandles.push_back(accelerationStructure->accelerationStructure);
            accelerationStructureInfos.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                                                  nullptr, 1u, &accelerationStructureHandles.back()});
            write.pNext = &accelerationStructureInfos.back();
        } else {
            RhiTextureViewHandle viewHandle{};
            RhiSamplerHandle samplerHandle{};
            if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                viewHandle = entry.resource.combinedTextureSampler.textureView;
                samplerHandle = entry.resource.combinedTextureSampler.sampler;
            } else if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                viewHandle = entry.resource.textureView;
            } else {
                samplerHandle = entry.resource.sampler;
            }
            const auto* view = viewHandle.isValid() ? findRecord(m_data->textureViews, viewHandle) : nullptr;
            const auto* sampler = samplerHandle.isValid() ? findRecord(m_data->samplers, samplerHandle) : nullptr;
            if ((viewHandle.isValid() && view == nullptr) || (samplerHandle.isValid() && sampler == nullptr)) {
                vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
                return {};
            }
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            } else if (view != nullptr) {
                const auto* texture = findRecord(m_data->textures, view->desc.texture);
                if (texture == nullptr) {
                    vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
                    return {};
                }
                const RhiTextureFormat viewFormat =
                    view->desc.format == RhiTextureFormat::Undefined ? texture->desc.format : view->desc.format;
                imageLayout = (defaultAspectForFormat(viewFormat) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u
                                  ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            imageInfos.push_back({sampler != nullptr ? sampler->sampler : VK_NULL_HANDLE,
                                  view != nullptr ? view->view : VK_NULL_HANDLE, imageLayout});
            write.pImageInfo = &imageInfos.back();
        }
        writes.push_back(write);
    }
    for (size_t layoutEntryIndex = 0u; layoutEntryIndex < layout->desc.entries.size(); ++layoutEntryIndex) {
        const RhiBindGroupLayoutEntry& layoutEntry = layout->desc.entries[layoutEntryIndex];
        if (rhiHasBindingFlag(layoutEntry.flags, RhiBindingFlag::PartiallyBound))
            continue;
        const uint32_t descriptorCount = rhiHasBindingFlag(layoutEntry.flags, RhiBindingFlag::VariableDescriptorCount)
                                             ? desc.variableDescriptorCount
                                             : layoutEntry.arrayCount;
        if (writtenBindingCounts[layoutEntryIndex] != descriptorCount) {
            vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
            return {};
        }
    }
    vkUpdateDescriptorSets(m_data->device, static_cast<uint32_t>(writes.size()), writes.data(), 0u, nullptr);
    const RhiBindGroupHandle handle = m_data->bindGroupHandles.allocate();
    m_data->bindGroups.emplace(handleKey(handle), VkRhiDeviceData::BindGroup{set, desc.layout, std::move(storedDesc)});
    return handle;
}

bool VkRhiDevice::updateBindGroups(const RhiBindGroupUpdate* updates, const uint32_t updateCount) {
    if (!m_initialized || updates == nullptr || updateCount == 0u) {
        logRhiError("updateBindGroups requires a non-empty batch");
        return false;
    }

    size_t totalResourceCount = 0u;
    for (uint32_t updateIndex = 0u; updateIndex < updateCount; ++updateIndex) {
        const RhiBindGroupUpdate& update = updates[updateIndex];
        if (update.resources == nullptr || update.resourceCount == 0u ||
            update.resourceCount > std::numeric_limits<size_t>::max() - totalResourceCount) {
            logRhiError("updateBindGroups received an invalid resource range");
            return false;
        }
        totalResourceCount += update.resourceCount;
    }

    const std::lock_guard<std::mutex> commandLock(m_data->commandRegistryMutex);
    const std::unique_lock<std::shared_mutex> resourceLock(m_data->resourceRegistryMutex);
    std::vector<uint64_t> recordedBindGroups;
    for (RhiCommandList* baseCommandList : m_data->commandLists) {
        auto* commandList = static_cast<VkRhiCommandList*>(baseCommandList);
        if (commandList == nullptr || (commandList->m_data->state != RhiCommandListState::Recording &&
                                       commandList->m_data->state != RhiCommandListState::Executable)) {
            continue;
        }
        for (const RhiBindGroupHandle handle : commandList->m_data->resourceReferences.bindGroups) {
            recordedBindGroups.push_back(handleKey(handle));
        }
    }
    std::sort(recordedBindGroups.begin(), recordedBindGroups.end());
    recordedBindGroups.erase(std::unique(recordedBindGroups.begin(), recordedBindGroups.end()),
                             recordedBindGroups.end());

    struct StagedSlot {
        VkRhiDeviceData::BindGroup* group = nullptr;
        uint64_t groupKey = 0u;
        uint32_t binding = 0u;
        uint32_t arrayElement = 0u;
        RhiBindingResource resource;
    };
    struct PendingResourceStamp {
        uint64_t sequence = 0u;
        VkRhiCommandResourceReferences references;
    };
    std::vector<StagedSlot> stagedSlots;
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkAccelerationStructureKHR> accelerationStructureHandles;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfos;
    stagedSlots.reserve(totalResourceCount);
    descriptorWrites.reserve(updateCount);
    bufferInfos.reserve(totalResourceCount);
    imageInfos.reserve(totalResourceCount);
    accelerationStructureHandles.reserve(totalResourceCount);
    accelerationStructureInfos.reserve(updateCount);
    std::unordered_map<uint64_t, PendingResourceStamp> pendingResourceStamps;
    pendingResourceStamps.reserve(updateCount);

    for (uint32_t updateIndex = 0u; updateIndex < updateCount; ++updateIndex) {
        const RhiBindGroupUpdate& update = updates[updateIndex];
        const uint64_t groupKey = handleKey(update.bindGroup);
        VkRhiDeviceData::BindGroup* group = findRecord(m_data->bindGroups, update.bindGroup);
        if (group == nullptr) {
            logRhiError("updateBindGroups received an invalid bind group or binding");
            return false;
        }
        const VkRhiDeviceData::BindGroupLayout* layout = findRecord(m_data->bindGroupLayouts, group->layoutHandle);
        if (layout == nullptr) {
            logRhiError("updateBindGroups received an invalid bind group or binding");
            return false;
        }
        const auto layoutEntry =
            std::find_if(layout->desc.entries.begin(), layout->desc.entries.end(),
                         [&](const RhiBindGroupLayoutEntry& candidate) { return candidate.binding == update.binding; });
        if (layoutEntry == layout->desc.entries.end()) {
            logRhiError("updateBindGroups received an invalid bind group or binding");
            return false;
        }
        const uint32_t descriptorCount = rhiHasBindingFlag(layoutEntry->flags, RhiBindingFlag::VariableDescriptorCount)
                                             ? group->desc.variableDescriptorCount
                                             : layoutEntry->arrayCount;
        if (update.firstArrayElement > descriptorCount ||
            update.resourceCount > descriptorCount - update.firstArrayElement) {
            logRhiError("updateBindGroups received an out-of-range descriptor array update");
            return false;
        }

        const bool partiallyBound = rhiHasBindingFlag(layoutEntry->flags, RhiBindingFlag::PartiallyBound);
        const bool updateAfterBind = rhiHasBindingFlag(layoutEntry->flags, RhiBindingFlag::UpdateAfterBind);
        const bool updateUnused = rhiHasBindingFlag(layoutEntry->flags, RhiBindingFlag::UpdateUnusedWhilePending);
        const bool pending = group->lifetime.lastUseSequence > m_data->completedSubmissionSequence;
        const bool recorded = std::binary_search(recordedBindGroups.begin(), recordedBindGroups.end(), groupKey);
        // The RHI cannot prove static descriptor non-use, so pending UpdateUnused writes also require
        // PartiallyBound and rely on the caller to select array elements not dynamically accessed.
        if ((pending && (!partiallyBound || !updateUnused)) ||
            (!pending && recorded && !updateAfterBind && (!partiallyBound || !updateUnused))) {
            logRhiError("updateBindGroups rejected a descriptor update that violates its command lifecycle flags");
            return false;
        }

        PendingResourceStamp* pendingStamp = nullptr;
        if (pending) {
            const auto stampInsertion = pendingResourceStamps.try_emplace(groupKey);
            pendingStamp = &stampInsertion.first->second;
            pendingStamp->sequence = std::max(pendingStamp->sequence, group->lifetime.lastUseSequence);
            switch (layoutEntry->type) {
            case RhiBindingType::UniformBuffer:
            case RhiBindingType::StorageBuffer:
                pendingStamp->references.buffers.reserve(pendingStamp->references.buffers.size() +
                                                         update.resourceCount);
                break;
            case RhiBindingType::SampledTexture:
            case RhiBindingType::StorageTexture:
                pendingStamp->references.textures.reserve(pendingStamp->references.textures.size() +
                                                          update.resourceCount);
                pendingStamp->references.textureViews.reserve(pendingStamp->references.textureViews.size() +
                                                              update.resourceCount);
                break;
            case RhiBindingType::Sampler:
                pendingStamp->references.samplers.reserve(pendingStamp->references.samplers.size() +
                                                          update.resourceCount);
                break;
            case RhiBindingType::CombinedTextureSampler:
                pendingStamp->references.textures.reserve(pendingStamp->references.textures.size() +
                                                          update.resourceCount);
                pendingStamp->references.textureViews.reserve(pendingStamp->references.textureViews.size() +
                                                              update.resourceCount);
                pendingStamp->references.samplers.reserve(pendingStamp->references.samplers.size() +
                                                          update.resourceCount);
                break;
            case RhiBindingType::AccelerationStructure:
                pendingStamp->references.buffers.reserve(pendingStamp->references.buffers.size() +
                                                         update.resourceCount);
                pendingStamp->references.accelerationStructures.reserve(
                    pendingStamp->references.accelerationStructures.size() + update.resourceCount);
                break;
            }
        }

        const VkDescriptorType descriptorType = toVkDescriptorType(layoutEntry->type);
        VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        descriptorWrite.dstSet = group->set;
        descriptorWrite.dstBinding = update.binding;
        descriptorWrite.dstArrayElement = update.firstArrayElement;
        descriptorWrite.descriptorCount = update.resourceCount;
        descriptorWrite.descriptorType = descriptorType;
        const size_t bufferInfoStart = bufferInfos.size();
        const size_t imageInfoStart = imageInfos.size();
        const size_t accelerationStructureStart = accelerationStructureHandles.size();

        for (uint32_t resourceIndex = 0u; resourceIndex < update.resourceCount; ++resourceIndex) {
            const uint32_t arrayElement = update.firstArrayElement + resourceIndex;
            const RhiBindingResource& resource = update.resources[resourceIndex];
            if (descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                const VkRhiDeviceData::Buffer* buffer = findRecord(m_data->buffers, resource.buffer.buffer);
                const RhiBufferUsage requiredUsage = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                         ? RhiBufferUsage::Uniform
                                                         : RhiBufferUsage::Storage;
                const uint64_t requiredAlignment = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                       ? m_data->properties.limits.minUniformBufferOffsetAlignment
                                                       : m_data->properties.limits.minStorageBufferOffsetAlignment;
                const uint64_t range = buffer != nullptr && resource.buffer.range != 0u
                                           ? resource.buffer.range
                                           : (buffer != nullptr && resource.buffer.offset <= buffer->desc.size
                                                  ? buffer->desc.size - resource.buffer.offset
                                                  : 0u);
                const uint64_t maximumRange = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                  ? m_data->properties.limits.maxUniformBufferRange
                                                  : m_data->properties.limits.maxStorageBufferRange;
                if (buffer == nullptr || (buffer->desc.usage & rhiFlag(requiredUsage)) == 0u ||
                    resource.buffer.offset % requiredAlignment != 0u || range == 0u || range > maximumRange ||
                    resource.buffer.offset > buffer->desc.size || range > buffer->desc.size - resource.buffer.offset) {
                    std::cerr << "VkRhiDevice: updateBindGroups received an invalid buffer resource"
                              << " binding=" << update.binding << " arrayElement=" << arrayElement
                              << " handle=" << resource.buffer.buffer.index << ':' << resource.buffer.buffer.generation
                              << " offset=" << resource.buffer.offset << " requestedRange=" << resource.buffer.range
                              << " resolvedRange=" << range << " requiredUsage=0x" << std::hex << rhiFlag(requiredUsage)
                              << " requiredAlignment=" << std::dec << requiredAlignment
                              << " maximumRange=" << maximumRange;
                    if (buffer == nullptr) {
                        std::cerr << " buffer=null";
                    } else {
                        std::cerr << " bufferSize=" << buffer->desc.size << " bufferUsage=0x" << std::hex
                                  << static_cast<uint32_t>(buffer->desc.usage) << std::dec;
                    }
                    std::cerr << '\n';
                    return false;
                }
                bufferInfos.push_back({buffer->buffer, resource.buffer.offset,
                                       resource.buffer.range == 0u ? VK_WHOLE_SIZE : resource.buffer.range});
                if (pendingStamp != nullptr) {
                    pendingStamp->references.buffers.push_back(resource.buffer.buffer);
                }
            } else if (descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
                const auto* accelerationStructure =
                    findRecord(m_data->accelerationStructures, resource.accelerationStructure);
                if (accelerationStructure == nullptr ||
                    accelerationStructure->desc.type != RhiAccelerationStructureType::TopLevel) {
                    logRhiError("updateBindGroups received an invalid acceleration structure");
                    return false;
                }
                accelerationStructureHandles.push_back(accelerationStructure->accelerationStructure);
                if (pendingStamp != nullptr) {
                    pendingStamp->references.buffers.push_back(accelerationStructure->desc.buffer);
                    pendingStamp->references.accelerationStructures.push_back(resource.accelerationStructure);
                }
            } else {
                RhiTextureViewHandle viewHandle{};
                RhiSamplerHandle samplerHandle{};
                if (descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    viewHandle = resource.combinedTextureSampler.textureView;
                    samplerHandle = resource.combinedTextureSampler.sampler;
                } else if (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                           descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                    viewHandle = resource.textureView;
                } else if (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    samplerHandle = resource.sampler;
                } else {
                    logRhiError("updateBindGroups received an unsupported descriptor type");
                    return false;
                }
                const VkRhiDeviceData::TextureView* view =
                    viewHandle.isValid() ? findRecord(m_data->textureViews, viewHandle) : nullptr;
                const VkRhiDeviceData::Sampler* sampler =
                    samplerHandle.isValid() ? findRecord(m_data->samplers, samplerHandle) : nullptr;
                if ((descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                     (view == nullptr || sampler == nullptr)) ||
                    ((descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                      descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) &&
                     view == nullptr) ||
                    (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER && sampler == nullptr)) {
                    logRhiError("updateBindGroups received an invalid image or sampler resource");
                    return false;
                }
                VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (view != nullptr) {
                    const VkRhiDeviceData::Texture* texture = findRecord(m_data->textures, view->desc.texture);
                    const RhiTextureUsage requiredUsage = descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                              ? RhiTextureUsage::Storage
                                                              : RhiTextureUsage::Sampled;
                    if (texture == nullptr || (texture->desc.usage & rhiFlag(requiredUsage)) == 0u) {
                        logRhiError("updateBindGroups received an incompatible texture-view resource");
                        return false;
                    }
                    if (descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                        imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    } else {
                        const RhiTextureFormat viewFormat =
                            view->desc.format == RhiTextureFormat::Undefined ? texture->desc.format : view->desc.format;
                        imageLayout = (defaultAspectForFormat(viewFormat) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u
                                          ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }
                }
                imageInfos.push_back({sampler != nullptr ? sampler->sampler : VK_NULL_HANDLE,
                                      view != nullptr ? view->view : VK_NULL_HANDLE, imageLayout});
                if (pendingStamp != nullptr) {
                    if (view != nullptr) {
                        pendingStamp->references.textures.push_back(view->desc.texture);
                        pendingStamp->references.textureViews.push_back(viewHandle);
                    }
                    if (sampler != nullptr) {
                        pendingStamp->references.samplers.push_back(samplerHandle);
                    }
                }
            }
            stagedSlots.push_back({group, groupKey, update.binding, arrayElement, resource});
        }

        if (descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            descriptorWrite.pBufferInfo = bufferInfos.data() + bufferInfoStart;
        } else if (descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
            accelerationStructureInfos.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                                                  nullptr, update.resourceCount,
                                                  accelerationStructureHandles.data() + accelerationStructureStart});
            descriptorWrite.pNext = &accelerationStructureInfos.back();
        } else {
            descriptorWrite.pImageInfo = imageInfos.data() + imageInfoStart;
        }
        descriptorWrites.push_back(descriptorWrite);
    }

    const auto stagedLess = [](const StagedSlot& lhs, const StagedSlot& rhs) {
        if (lhs.groupKey != rhs.groupKey) {
            return lhs.groupKey < rhs.groupKey;
        }
        return lhs.binding != rhs.binding ? lhs.binding < rhs.binding : lhs.arrayElement < rhs.arrayElement;
    };
    std::sort(stagedSlots.begin(), stagedSlots.end(), stagedLess);
    const auto duplicate =
        std::adjacent_find(stagedSlots.begin(), stagedSlots.end(), [](const StagedSlot& lhs, const StagedSlot& rhs) {
            return lhs.groupKey == rhs.groupKey && lhs.binding == rhs.binding && lhs.arrayElement == rhs.arrayElement;
        });
    if (duplicate != stagedSlots.end()) {
        logRhiError("updateBindGroups received overlapping descriptor array ranges");
        return false;
    }

    const auto sortUniqueHandles = [](auto& handles) {
        std::sort(handles.begin(), handles.end(), [](const auto lhs, const auto rhs) {
            return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
        });
        handles.erase(std::unique(handles.begin(), handles.end(),
                                  [](const auto lhs, const auto rhs) {
                                      return lhs.index == rhs.index && lhs.generation == rhs.generation;
                                  }),
                      handles.end());
    };
    for (auto& [_, stamp] : pendingResourceStamps) {
        sortUniqueHandles(stamp.references.buffers);
        sortUniqueHandles(stamp.references.textures);
        sortUniqueHandles(stamp.references.textureViews);
        sortUniqueHandles(stamp.references.samplers);
        sortUniqueHandles(stamp.references.accelerationStructures);
    }

    vkUpdateDescriptorSets(m_data->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0u,
                           nullptr);
    size_t stagedBegin = 0u;
    while (stagedBegin < stagedSlots.size()) {
        VkRhiDeviceData::BindGroup* group = stagedSlots[stagedBegin].group;
        const uint64_t groupKey = stagedSlots[stagedBegin].groupKey;
        size_t stagedEnd = stagedBegin + 1u;
        while (stagedEnd < stagedSlots.size() && stagedSlots[stagedEnd].groupKey == groupKey) {
            ++stagedEnd;
        }
        std::vector<RhiBindGroupEntry> mergedEntries;
        mergedEntries.reserve(group->desc.entries.size() + stagedEnd - stagedBegin);
        size_t existingIndex = 0u;
        size_t stagedIndex = stagedBegin;
        while (existingIndex < group->desc.entries.size() && stagedIndex < stagedEnd) {
            const RhiBindGroupEntry& existing = group->desc.entries[existingIndex];
            const StagedSlot& staged = stagedSlots[stagedIndex];
            if (existing.binding < staged.binding ||
                (existing.binding == staged.binding && existing.arrayElement < staged.arrayElement)) {
                mergedEntries.push_back(existing);
                ++existingIndex;
            } else if (staged.binding < existing.binding ||
                       (staged.binding == existing.binding && staged.arrayElement < existing.arrayElement)) {
                mergedEntries.push_back({staged.binding, staged.arrayElement, staged.resource});
                ++stagedIndex;
            } else {
                mergedEntries.push_back({staged.binding, staged.arrayElement, staged.resource});
                ++existingIndex;
                ++stagedIndex;
            }
        }
        mergedEntries.insert(mergedEntries.end(),
                             group->desc.entries.begin() + static_cast<std::ptrdiff_t>(existingIndex),
                             group->desc.entries.end());
        for (; stagedIndex < stagedEnd; ++stagedIndex) {
            const StagedSlot& staged = stagedSlots[stagedIndex];
            mergedEntries.push_back({staged.binding, staged.arrayElement, staged.resource});
        }
        group->desc.entries = std::move(mergedEntries);
        stagedBegin = stagedEnd;
    }
    for (const auto& [_, stamp] : pendingResourceStamps) {
        markResourceReferencesUsed(*m_data, stamp.references, stamp.sequence);
    }
    return true;
}

RhiQueryPoolHandle VkRhiDevice::createQueryPool(const RhiQueryPoolDesc& desc) {
    if (!m_initialized || desc.queryCount == 0u)
        return {};
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    switch (desc.type) {
    case RhiQueryType::Timestamp: info.queryType = VK_QUERY_TYPE_TIMESTAMP; break;
    case RhiQueryType::AccelerationStructureCompactedSize:
        info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        break;
    }
    info.queryCount = desc.queryCount;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateQueryPool(m_data->device, &info, nullptr, &pool), "vkCreateQueryPool"))
        return {};
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const RhiQueryPoolHandle handle = m_data->queryPoolHandles.allocate();
    m_data->queryPools.emplace(handleKey(handle), VkRhiDeviceData::QueryPool{pool, desc.queryCount, desc.type});
    nameObject(*m_data, VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<uint64_t>(pool), desc.debugName);
    return handle;
}

bool VkRhiDevice::resetQueryPool(const RhiQueryPoolHandle pool, const uint32_t firstQuery, const uint32_t queryCount) {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->queryPools, pool) : nullptr;
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread || record == nullptr || queryCount == 0u ||
        firstQuery > record->count || queryCount > record->count - firstQuery) {
        return false;
    }
    vkResetQueryPool(m_data->device, record->pool, firstQuery, queryCount);
    return true;
}

void* VkRhiDevice::mapBuffer(const RhiBufferHandle buffer, const uint64_t offset, const uint64_t size) {
    // Exclusive: the record's cached mapping pointer may be written below.
    const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    auto* record = m_data != nullptr ? findRecord(m_data->buffers, buffer) : nullptr;
    if (record == nullptr || record->desc.memoryUsage == RhiMemoryUsage::GpuOnly || offset > record->desc.size ||
        size > record->desc.size - offset)
        return nullptr;
    void* base = record->mapped;
    if (base == nullptr && vmaMapMemory(m_data->allocator, record->allocation, &base) != VK_SUCCESS) {
        return nullptr;
    }
    record->mapped = base;
    if (record->desc.memoryUsage == RhiMemoryUsage::GpuToCpu) {
        vmaInvalidateAllocation(m_data->allocator, record->allocation, offset, size);
    }
    return static_cast<std::byte*>(base) + offset;
}

void VkRhiDevice::unmapBuffer(const RhiBufferHandle buffer) {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    auto* record = m_data != nullptr ? findRecord(m_data->buffers, buffer) : nullptr;
    if (record == nullptr || record->mapped == nullptr)
        return;
    if (record->desc.memoryUsage == RhiMemoryUsage::CpuToGpu) {
        vmaFlushAllocation(m_data->allocator, record->allocation, 0u, VK_WHOLE_SIZE);
    }
}

bool VkRhiDevice::areQueryResultsAvailable(const RhiQueryPoolHandle pool, const uint32_t firstQuery,
                                           const uint32_t queryCount) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->queryPools, pool) : nullptr;
    if (record == nullptr || queryCount == 0u || firstQuery > record->count || queryCount > record->count - firstQuery)
        return false;
    std::vector<uint64_t> values(queryCount * 2u);
    if (vkGetQueryPoolResults(m_data->device, record->pool, firstQuery, queryCount, values.size() * sizeof(uint64_t),
                              values.data(), 2u * sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != VK_SUCCESS)
        return false;
    for (uint32_t i = 0u; i < queryCount; ++i) {
        if (values[i * 2u + 1u] == 0u)
            return false;
    }
    return true;
}

bool VkRhiDevice::getQueryResults(const RhiQueryPoolHandle pool, const uint32_t firstQuery, const uint32_t queryCount,
                                  uint64_t* results) const {
    const std::shared_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
    const auto* record = m_data != nullptr ? findRecord(m_data->queryPools, pool) : nullptr;
    if (record == nullptr || results == nullptr || queryCount == 0u || firstQuery > record->count ||
        queryCount > record->count - firstQuery)
        return false;
    std::vector<uint64_t> ticks(queryCount);
    if (vkGetQueryPoolResults(m_data->device, record->pool, firstQuery, queryCount, ticks.size() * sizeof(uint64_t),
                              ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return false;
    if (record->type == RhiQueryType::Timestamp) {
        for (uint32_t i = 0u; i < queryCount; ++i) {
            results[i] =
                static_cast<uint64_t>(static_cast<double>(ticks[i]) * m_data->properties.limits.timestampPeriod);
        }
    } else {
        std::copy(ticks.begin(), ticks.end(), results);
    }
    return true;
}

RhiTextureViewHandle VkRhiDevice::currentSwapchainColorView() const {
    if (m_data == nullptr || !m_data->frameAcquired || m_data->acquiredImage >= m_data->swapchainViews.size())
        return {};
    return m_data->swapchainViews[m_data->acquiredImage];
}

RhiTextureViewHandle VkRhiDevice::currentSwapchainDepthStencilView() const {
    if (m_data == nullptr || !m_data->frameAcquired || m_data->acquiredImage >= m_data->depthViews.size())
        return {};
    return m_data->depthViews[m_data->acquiredImage];
}

RhiTextureHandle VkRhiDevice::currentSwapchainColorTexture() const {
    if (m_data == nullptr || !m_data->frameAcquired || m_data->acquiredImage >= m_data->swapchainTextures.size())
        return {};
    return m_data->swapchainTextures[m_data->acquiredImage];
}

RhiTextureFormat VkRhiDevice::swapchainColorFormat() const {
    return m_data != nullptr ? fromVkFormat(m_data->swapchainFormat) : RhiTextureFormat::Undefined;
}

RhiTextureFormat VkRhiDevice::swapchainDepthStencilFormat() const {
    return m_initialized ? RhiTextureFormat::Depth32Float : RhiTextureFormat::Undefined;
}

bool VkRhiDevice::vsyncEnabled() const {
    return m_initialized && m_data != nullptr && m_data->vsyncEnabled;
}

bool VkRhiDevice::setVsyncEnabled(const bool enabled) {
    if (!m_initialized || m_data == nullptr || m_data->frameAcquired || std::this_thread::get_id() != m_deviceThread) {
        return false;
    }
    if (!enabled && !m_data->immediatePresentSupported) {
        return false;
    }
    if (m_data->vsyncEnabled == enabled) {
        return true;
    }
    m_data->vsyncEnabled = enabled;
    m_data->swapchainDirty = true;
    return true;
}

void VkRhiDevice::refreshSwapchainCapabilities() {
    m_capabilities.swapchainImageCount = static_cast<uint32_t>(m_data->swapchainTextures.size());
    m_capabilities.swapchainColorSpace = RhiColorSpace::SrgbNonlinear;
    m_capabilities.swapchainPresentMode =
        m_data->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? RhiPresentMode::Immediate : RhiPresentMode::Fifo;
    m_capabilities.vsyncControl = m_data->immediatePresentSupported;
}

bool VkRhiDevice::resizeSwapchain(const uint32_t width, const uint32_t height) {
    if (!m_initialized || width == 0u || height == 0u || m_data->frameAcquired)
        return false;
    if (m_data->requestedWidth == width && m_data->requestedHeight == height) {
        return true;
    }
    m_data->requestedWidth = width;
    m_data->requestedHeight = height;
    m_data->swapchainDirty = true;
    return true;
}

RhiFrameAcquireResult VkRhiDevice::acquireFrame() {
    RhiFrameAcquireResult result{};
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread || m_data->frameAcquired) {
        result.status = RhiFrameStatus::Error;
        return result;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_data->window, &width, &height);
    if (width <= 0 || height <= 0) {
        result.status = RhiFrameStatus::Minimized;
        return result;
    }
    if (m_data->surfaceLost || m_data->swapchainDirty) {
        const bool recreated = m_data->surfaceLost ? recreateSurfaceAndSwapchain(*m_data) : recreateSwapchain(*m_data);
        if (!recreated) {
            result.status = m_data->surfaceLost ? RhiFrameStatus::SurfaceLost : RhiFrameStatus::Error;
            return result;
        }
        refreshSwapchainCapabilities();
    }
    auto& frame = m_data->frames[m_data->frameSlot];
    if (frame.fencePending) {
        const VkResult waitResult = vkWaitForFences(m_data->device, 1u, &frame.fence, VK_TRUE, UINT64_MAX);
        if (waitResult == VK_ERROR_DEVICE_LOST) {
            logVkError("vkWaitForFences(acquire)", waitResult);
            result.status = RhiFrameStatus::DeviceLost;
            return result;
        }
        if (waitResult != VK_SUCCESS) {
            logVkError("vkWaitForFences(acquire)", waitResult);
            result.status = RhiFrameStatus::Error;
            return result;
        }
        vkResetFences(m_data->device, 1u, &frame.fence);
        frame.fencePending = false;
    }
    const VkResult acquireResult =
        acquireMainSwapchainImage(*m_data, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, m_data->acquiredImage);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        m_data->swapchainDirty = true;
        result.status = RhiFrameStatus::OutOfDate;
        return result;
    }
    if (acquireResult == VK_ERROR_SURFACE_LOST_KHR) {
        m_data->surfaceLost = true;
        result.status = RhiFrameStatus::SurfaceLost;
        return result;
    }
    if (acquireResult == VK_ERROR_DEVICE_LOST) {
        logVkError("vkAcquireNextImageKHR", acquireResult);
        result.status = RhiFrameStatus::DeviceLost;
        return result;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        logVkError("vkAcquireNextImageKHR", acquireResult);
        result.status = RhiFrameStatus::Error;
        return result;
    }
    m_data->frameAcquired = true;
    m_data->frameSubmitted = false;
    m_data->frameImageAvailableWaited = false;
#if defined(MECRAFT_ENABLE_STREAMLINE)
    m_data->frameGenerationAcquireLayoutPending = StreamlineRuntime::instance().dlssFrameGenerationLoaded();
#endif
    m_data->frameLastGraphicsSequence = 0u;
    ++m_data->acquiredFrameIndex;
    result.status = acquireResult == VK_SUBOPTIMAL_KHR ? RhiFrameStatus::Suboptimal : RhiFrameStatus::Success;
    if (acquireResult == VK_SUBOPTIMAL_KHR)
        m_data->swapchainDirty = true;
    result.frameIndex = m_data->acquiredFrameIndex;
    result.imageIndex = m_data->acquiredImage;
    result.width = m_data->swapchainExtent.width;
    result.height = m_data->swapchainExtent.height;
    result.colorTexture = m_data->swapchainTextures[m_data->acquiredImage];
    result.colorView = m_data->swapchainViews[m_data->acquiredImage];
    result.depthStencilView = m_data->depthViews[m_data->acquiredImage];
    return result;
}

RhiFrameStatus VkRhiDevice::cancelAcquiredFrame(const RhiPresentInfo& info) {
    if (!m_initialized || m_data == nullptr || std::this_thread::get_id() != m_deviceThread || !m_data->frameAcquired ||
        info.frameIndex != m_data->acquiredFrameIndex || info.imageIndex != m_data->acquiredImage) {
        return RhiFrameStatus::Error;
    }
    auto& frame = m_data->frames[m_data->frameSlot];
    std::array<VkSemaphoreSubmitInfo, 2u> waits{};
    uint32_t waitCount = 0u;
    const bool consumeExternalFrameCompletion = m_data->externalFrameCompletionSemaphore != VK_NULL_HANDLE;
    if (consumeExternalFrameCompletion) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,  nullptr,
                              m_data->externalFrameCompletionSemaphore, m_data->externalFrameCompletionValue,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,     0u};
    }
    if (!m_data->frameImageAvailableWaited) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, frame.imageAvailable, 0u,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0u};
    }
    if (waitCount != 0u) {
        VkSubmitInfo2 releaseSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        releaseSubmit.waitSemaphoreInfoCount = waitCount;
        releaseSubmit.pWaitSemaphoreInfos = waits.data();
        const VkResult submitResult = vkQueueSubmit2(m_data->graphicsQueue, 1u, &releaseSubmit, VK_NULL_HANDLE);
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            logVkError("vkQueueSubmit2(frame cancel)", submitResult);
            return RhiFrameStatus::DeviceLost;
        }
        if (!vkSucceeded(submitResult, "vkQueueSubmit2(frame cancel)")) {
            return RhiFrameStatus::Error;
        }
        if (consumeExternalFrameCompletion) {
            m_data->externalFrameCompletionSemaphore = VK_NULL_HANDLE;
            m_data->externalFrameCompletionValue = 0u;
        }
    }
    const VkResult idleResult = waitDeviceIdle(*m_data);
    if (idleResult == VK_ERROR_DEVICE_LOST) {
        logVkError("vkDeviceWaitIdle(frame cancel)", idleResult);
        return RhiFrameStatus::DeviceLost;
    }
    if (!vkSucceeded(idleResult, "vkDeviceWaitIdle(frame cancel)")) {
        return RhiFrameStatus::Error;
    }
    reclaimCompletedWork();
    destroySwapchainResources(*m_data);
    if (!createSwapchain(*m_data)) {
        m_data->swapchainDirty = true;
        return RhiFrameStatus::Error;
    }
    refreshSwapchainCapabilities();
    return RhiFrameStatus::Success;
}

bool VkRhiDevice::cancelFrame(const RhiPresentInfo& info) {
    return cancelAcquiredFrame(info) == RhiFrameStatus::Success;
}

RhiFrameStatus VkRhiDevice::presentFrame(const RhiPresentInfo& info) {
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread || !m_data->frameAcquired ||
        info.frameIndex != m_data->acquiredFrameIndex || info.imageIndex != m_data->acquiredImage) {
        return RhiFrameStatus::Error;
    }
    auto& frame = m_data->frames[m_data->frameSlot];
    if (!m_data->frameSubmitted) {
        const RhiFrameStatus cancelStatus = cancelAcquiredFrame(info);
        return cancelStatus == RhiFrameStatus::Success ? RhiFrameStatus::OutOfDate : cancelStatus;
    }
    std::array<VkSemaphoreSubmitInfo, 2u> waits{};
    uint32_t waitCount = 0u;
    if (!m_data->frameImageAvailableWaited) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, frame.imageAvailable, 0u,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0u};
    }
    if (m_data->frameLastGraphicsSequence != 0u) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                              nullptr,
                              m_data->queueTimelines[queueTimelineIndex(RhiQueueType::Graphics)],
                              m_data->frameLastGraphicsSequence,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                              0u};
    }
    const VkSemaphore presentReady = m_data->presentReadySemaphores[m_data->acquiredImage];
    const VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, presentReady, 0u,
                                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0u};
    VkSubmitInfo2 tailSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    tailSubmit.waitSemaphoreInfoCount = waitCount;
    tailSubmit.pWaitSemaphoreInfos = waits.data();
    tailSubmit.signalSemaphoreInfoCount = 1u;
    tailSubmit.pSignalSemaphoreInfos = &signal;
    const VkResult tailResult = vkQueueSubmit2(m_data->graphicsQueue, 1u, &tailSubmit, frame.fence);
    if (tailResult == VK_ERROR_DEVICE_LOST) {
        logVkError("vkQueueSubmit2(frame tail)", tailResult);
        return RhiFrameStatus::DeviceLost;
    }
    if (tailResult != VK_SUCCESS) {
        logVkError("vkQueueSubmit2(frame tail)", tailResult);
        return RhiFrameStatus::Error;
    }
    frame.fencePending = true;
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (info.trackingFrameIndex != 0u &&
        !StreamlineRuntime::instance().setPclMarker(info.trackingFrameIndex, StreamlinePclMarker::RenderSubmitEnd)) {
        std::cerr << StreamlineRuntime::instance().lastError() << '\n';
        return RhiFrameStatus::Error;
    }
#endif
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &presentReady;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &m_data->swapchain;
    presentInfo.pImageIndices = &m_data->acquiredImage;
    VkResult swapchainResult = VK_SUCCESS;
    presentInfo.pResults = &swapchainResult;
#if defined(MECRAFT_ENABLE_STREAMLINE)
    if (info.trackingFrameIndex != 0u &&
        !StreamlineRuntime::instance().setPclMarker(info.trackingFrameIndex, StreamlinePclMarker::PresentStart)) {
        std::cerr << StreamlineRuntime::instance().lastError() << '\n';
        return RhiFrameStatus::Error;
    }
    const VkResult proxyResult = StreamlineRuntime::instance().presentVulkanFrame(m_data->presentQueue, presentInfo);
    const VkResult nativeResult = swapchainResult != VK_SUCCESS ? swapchainResult : proxyResult;
    if (info.trackingFrameIndex != 0u &&
        !StreamlineRuntime::instance().setPclMarker(info.trackingFrameIndex, StreamlinePclMarker::PresentEnd)) {
        std::cerr << StreamlineRuntime::instance().lastError() << '\n';
        return RhiFrameStatus::Error;
    }
#else
    const VkResult nativeResult = vkQueuePresentKHR(m_data->presentQueue, &presentInfo);
#endif
    m_data->frameAcquired = false;
    m_data->frameSubmitted = false;
    m_data->frameImageAvailableWaited = false;
    m_data->frameLastGraphicsSequence = 0u;
    m_data->acquiredImage = UINT32_MAX;
    m_data->frameSlot = (m_data->frameSlot + 1u) % static_cast<uint32_t>(m_data->frames.size());
    if (nativeResult == VK_SUCCESS)
        return RhiFrameStatus::Success;
    if (nativeResult == VK_SUBOPTIMAL_KHR) {
        m_data->swapchainDirty = true;
        return RhiFrameStatus::Suboptimal;
    }
    if (nativeResult == VK_ERROR_OUT_OF_DATE_KHR) {
        m_data->swapchainDirty = true;
        return RhiFrameStatus::OutOfDate;
    }
    if (nativeResult == VK_ERROR_SURFACE_LOST_KHR) {
        m_data->surfaceLost = true;
        return RhiFrameStatus::SurfaceLost;
    }
    if (nativeResult == VK_ERROR_DEVICE_LOST) {
        logVkError("vkQueuePresentKHR", nativeResult);
        return RhiFrameStatus::DeviceLost;
    }
    logVkError("vkQueuePresentKHR", nativeResult);
    return RhiFrameStatus::Error;
}

void VkRhiDevice::destroyBuffer(const RhiBufferHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->buffers.find(handleKey(handle));
        const bool hostsAccelerationStructure = std::any_of(
            m_data->accelerationStructures.begin(), m_data->accelerationStructures.end(), [handle](const auto& entry) {
                return entry.second.desc.buffer.index == handle.index &&
                       entry.second.desc.buffer.generation == handle.generation;
            });
        const bool hostsMicromap =
            std::any_of(m_data->micromaps.begin(), m_data->micromaps.end(), [handle](const auto& entry) {
                return entry.second.desc.buffer.index == handle.index &&
                       entry.second.desc.buffer.generation == handle.generation;
            });
        if (it == m_data->buffers.end() || hostsAccelerationStructure || hostsMicromap ||
            !m_data->bufferHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredBuffers,
                        VkRhiDeviceData::DeferredBuffer{it->second.lifetime.lastUseSequence, it->second.buffer,
                                                        it->second.allocation});
        m_data->buffers.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyTexture(const RhiTextureHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->textures.find(handleKey(handle));
        if (it == m_data->textures.end() || it->second.swapchainOwned)
            return;
        if (!m_data->textureHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredImages,
                        VkRhiDeviceData::DeferredImage{it->second.lifetime.lastUseSequence, handleKey(handle),
                                                       it->second.image, it->second.allocation});
        m_data->textures.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyTextureView(const RhiTextureViewHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->textureViews.find(handleKey(handle));
        if (it == m_data->textureViews.end() || !m_data->textureViewHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_IMAGE_VIEW,
                                                        reinterpret_cast<uint64_t>(it->second.view)});
        m_data->textureViews.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroySampler(const RhiSamplerHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->samplers.find(handleKey(handle));
        if (it == m_data->samplers.end() || !m_data->samplerHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_SAMPLER,
                                                        reinterpret_cast<uint64_t>(it->second.sampler)});
        m_data->samplers.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyShader(const RhiShaderHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->shaders.find(handleKey(handle));
        if (it == m_data->shaders.end() || !m_data->shaderHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects, VkRhiDeviceData::DeferredObject{
                                                     it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_SHADER_MODULE,
                                                     reinterpret_cast<uint64_t>(it->second.module)});
        m_data->shaders.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyBindGroupLayout(const RhiBindGroupLayoutHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->bindGroupLayouts.find(handleKey(handle));
        if (it == m_data->bindGroupLayouts.end() || !m_data->bindGroupLayoutHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence,
                                                        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                                        reinterpret_cast<uint64_t>(it->second.layout)});
        m_data->bindGroupLayouts.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyPipelineLayout(const RhiPipelineLayoutHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->pipelineLayouts.find(handleKey(handle));
        if (it == m_data->pipelineLayouts.end() || !m_data->pipelineLayoutHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence,
                                                        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                                                        reinterpret_cast<uint64_t>(it->second.layout)});
        m_data->pipelineLayouts.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyPipeline(const RhiPipelineHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->pipelines.find(handleKey(handle));
        if (it == m_data->pipelines.end() || !m_data->pipelineHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_PIPELINE,
                                                        reinterpret_cast<uint64_t>(it->second.pipeline)});
        m_data->pipelines.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyBindGroup(const RhiBindGroupHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->bindGroups.find(handleKey(handle));
        if (it == m_data->bindGroups.end() || !m_data->bindGroupHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects, VkRhiDeviceData::DeferredObject{
                                                     it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                                                     reinterpret_cast<uint64_t>(it->second.set)});
        m_data->bindGroups.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyQueryPool(const RhiQueryPoolHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->queryPools.find(handleKey(handle));
        if (it == m_data->queryPools.end() || !m_data->queryPoolHandles.release(handle))
            return;
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_QUERY_POOL,
                                                        reinterpret_cast<uint64_t>(it->second.pool)});
        m_data->queryPools.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyAccelerationStructure(const RhiAccelerationStructureHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->accelerationStructures.find(handleKey(handle));
        if (it == m_data->accelerationStructures.end() || !m_data->accelerationStructureHandles.release(handle)) {
            return;
        }
        enqueueDeferred(m_data->deferredObjects,
                        VkRhiDeviceData::DeferredObject{
                            it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
                            reinterpret_cast<uint64_t>(it->second.accelerationStructure), it->second.desc.buffer,
                            it->second.desc.offset, it->second.desc.size});
        m_data->accelerationStructures.erase(it);
    }
    reclaimCompletedWork();
}

void VkRhiDevice::destroyMicromap(const RhiMicromapHandle handle) {
    if (m_data == nullptr)
        return;
    {
        const std::unique_lock<std::shared_mutex> registryLock(m_data->resourceRegistryMutex);
        const auto it = m_data->micromaps.find(handleKey(handle));
        if (it == m_data->micromaps.end() || !m_data->micromapHandles.release(handle)) {
            return;
        }
        enqueueDeferred(
            m_data->deferredObjects,
            VkRhiDeviceData::DeferredObject{it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_MICROMAP_EXT,
                                            reinterpret_cast<uint64_t>(it->second.micromap), it->second.desc.buffer,
                                            it->second.desc.offset, it->second.desc.size});
        m_data->micromaps.erase(it);
    }
    reclaimCompletedWork();
}

std::unique_ptr<RhiCommandListPool> VkRhiDevice::createCommandListPool(const RhiCommandListPoolDesc& desc) {
    if (!m_initialized || desc.initialCommandListCapacity == 0u)
        return nullptr;
    return std::make_unique<VkRhiCommandListPool>(*this, desc);
}

bool VkRhiDevice::submit(const RhiSubmitInfo& info, RhiSubmissionToken* completionToken) {
    if (!m_initialized || info.commandLists == nullptr || info.commandListCount == 0u ||
        info.queue == RhiQueueType::Present || std::this_thread::get_id() != m_deviceThread ||
        (info.waitCount == 0u) != (info.waits == nullptr))
        return false;
    const std::lock_guard<std::mutex> registryLock(m_data->commandRegistryMutex);
    // Exclusive resource lock: submission resolves resource references,
    // stamps lifetimes, and reclaims deferred destructions. Lock order is
    // commandRegistryMutex before resourceRegistryMutex everywhere.
    const std::unique_lock<std::shared_mutex> resourceLock(m_data->resourceRegistryMutex);
    std::vector<VkCommandBufferSubmitInfo> commandInfos;
    std::vector<VkRhiCommandList*> lists;
    std::vector<VkRhiCommandResourceReferences> resourceReferences;
    commandInfos.reserve(info.commandListCount);
    lists.reserve(info.commandListCount);
    resourceReferences.reserve(info.commandListCount);
    for (uint32_t i = 0u; i < info.commandListCount; ++i) {
        if (m_data->commandLists.find(info.commandLists[i]) == m_data->commandLists.end())
            return false;
        auto* list = static_cast<VkRhiCommandList*>(info.commandLists[i]);
        if (list == nullptr || list->m_device != this || !list->isExecutable())
            return false;
        if (std::find(lists.begin(), lists.end(), list) != lists.end())
            return false;
        const bool queueMatches =
            (info.queue == RhiQueueType::Graphics && list->m_data->type == RhiCommandListType::Graphics) ||
            (info.queue == RhiQueueType::Compute && list->m_data->type == RhiCommandListType::Compute) ||
            (info.queue == RhiQueueType::Transfer && list->m_data->type == RhiCommandListType::Transfer);
        if (!queueMatches)
            return false;
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = static_cast<VkCommandBuffer>(list->nativeCommandBuffer());
        VkRhiCommandResourceReferences resolved = list->m_data->resourceReferences;
        if (!resolveResourceReferences(*m_data, resolved))
            return false;
        commandInfos.push_back(commandInfo);
        lists.push_back(list);
        resourceReferences.push_back(std::move(resolved));
    }
    const uint64_t sequence = m_lastSubmittedSequence + 1u;
    const size_t signalQueueIndex = queueTimelineIndex(info.queue);
    if (signalQueueIndex >= m_data->queueTimelines.size())
        return false;
    const uint64_t queueValue = m_data->lastQueueTimelineValues[signalQueueIndex] + 1u;
    std::vector<VkSemaphoreSubmitInfo> waits;
    waits.reserve(info.waitCount + 2u);
    for (uint32_t i = 0u; i < info.waitCount; ++i) {
        const auto& wait = info.waits[i];
        if (!validateSubmissionToken(wait.token) || wait.token.sequence > m_lastSubmittedSequence)
            return false;
        const size_t waitQueueIndex = queueTimelineIndex(wait.token.queue);
        if (waitQueueIndex >= m_data->queueTimelines.size())
            return false;
        const uint64_t value = wait.value != 0u ? wait.value : wait.token.timelineValue();
        if (value > wait.token.timelineValue())
            return false;
        waits.push_back({VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, m_data->queueTimelines[waitQueueIndex],
                         value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u});
    }
    const bool consumeExternalFrameCompletion =
        info.queue == RhiQueueType::Graphics && m_data->externalFrameCompletionSemaphore != VK_NULL_HANDLE;
    if (consumeExternalFrameCompletion) {
        waits.push_back({VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, m_data->externalFrameCompletionSemaphore,
                         m_data->externalFrameCompletionValue, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u});
    }
    const bool consumeAcquire =
        info.queue == RhiQueueType::Graphics && m_data->frameAcquired && !m_data->frameImageAvailableWaited;
    VkRhiDeviceData::FrameContext* frame = nullptr;
    if (consumeAcquire) {
        frame = &m_data->frames[m_data->frameSlot];
        waits.push_back({VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, frame->imageAvailable, 0u,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u});
    }
    const VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,  nullptr,
                                       m_data->queueTimelines[signalQueueIndex], queueValue,
                                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,     0u};
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submitInfo.pWaitSemaphoreInfos = waits.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
    submitInfo.pCommandBufferInfos = commandInfos.data();
    submitInfo.signalSemaphoreInfoCount = 1u;
    submitInfo.pSignalSemaphoreInfos = &signal;
    const VkResult nativeResult = vkQueueSubmit2(queueForType(*m_data, info.queue), 1u, &submitInfo, VK_NULL_HANDLE);
    if (nativeResult != VK_SUCCESS) {
        logVkError("vkQueueSubmit2", nativeResult);
        return false;
    }
    if (consumeExternalFrameCompletion) {
        m_data->externalFrameCompletionSemaphore = VK_NULL_HANDLE;
        m_data->externalFrameCompletionValue = 0u;
    }
    m_lastSubmittedSequence = sequence;
    m_data->lastQueueTimelineValues[signalQueueIndex] = queueValue;
    m_data->pendingSubmissions.push_back({sequence, info.queue, queueValue});
    for (size_t index = 0u; index < lists.size(); ++index) {
        auto* list = lists[index];
        markResourceReferencesUsed(*m_data, resourceReferences[index], sequence);
        for (const uint32_t imageIndex : list->m_data->initializedSwapchainImages) {
            if (imageIndex < m_data->swapchainImageInitialized.size()) {
                m_data->swapchainImageInitialized[imageIndex] = true;
            }
        }
        list->markPending(sequence);
        m_data->pendingLists.push_back({sequence, info.queue, queueValue, list});
        for (const auto& transient : list->m_data->transientBuffers) {
            enqueueDeferred(m_data->deferredBuffers,
                            VkRhiDeviceData::DeferredBuffer{sequence, transient.first, transient.second});
        }
        list->m_data->transientBuffers.clear();
    }
    if (m_data->frameAcquired && info.queue == RhiQueueType::Graphics) {
        m_data->frameSubmitted = true;
        m_data->frameImageAvailableWaited = true;
        m_data->frameLastGraphicsSequence = queueValue;
    }
    if (completionToken != nullptr) {
        *completionToken = {m_deviceId, sequence, info.queue, queueValue};
    }
    reclaimCompletedWorkUnlocked();
    return true;
}

bool VkRhiDevice::validateSubmissionToken(const RhiSubmissionToken token) const {
    if (!token.isValid() || token.deviceId != m_deviceId || token.sequence > m_lastSubmittedSequence)
        return false;
    const size_t index = queueTimelineIndex(token.queue);
    return index < m_data->lastQueueTimelineValues.size() &&
           token.timelineValue() <= m_data->lastQueueTimelineValues[index];
}

bool VkRhiDevice::isSubmissionComplete(const RhiSubmissionToken token, bool& complete) {
    complete = false;
    if (!validateSubmissionToken(token))
        return false;
    const size_t index = queueTimelineIndex(token.queue);
    uint64_t counter = 0u;
    if (vkGetSemaphoreCounterValue(m_data->device, m_data->queueTimelines[index], &counter) != VK_SUCCESS)
        return false;
    complete = counter >= token.timelineValue();
    if (complete)
        reclaimCompletedWork();
    return true;
}

bool VkRhiDevice::waitForSubmission(const RhiSubmissionToken token) {
    if (!validateSubmissionToken(token))
        return false;
    const size_t index = queueTimelineIndex(token.queue);
    const uint64_t value = token.timelineValue();
    const VkSemaphore timeline = m_data->queueTimelines[index];
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1u;
    waitInfo.pSemaphores = &timeline;
    waitInfo.pValues = &value;
    if (vkWaitSemaphores(m_data->device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
        return false;
    reclaimCompletedWork();
    return true;
}

void VkRhiDevice::waitIdle() {
    if (m_initialized && waitDeviceIdle(*m_data) == VK_SUCCESS)
        reclaimCompletedWork();
}

uint64_t VkRhiDevice::validationErrorCount() const {
    return m_data != nullptr ? m_data->validationErrorCount.load(std::memory_order_relaxed) : 0u;
}

void VkRhiDevice::reclaimCompletedWork() {
    if (!m_initialized)
        return;
    const std::lock_guard<std::mutex> lock(m_data->commandRegistryMutex);
    const std::unique_lock<std::shared_mutex> resourceLock(m_data->resourceRegistryMutex);
    reclaimCompletedWorkUnlocked();
}

// Requires both commandRegistryMutex and an exclusive resourceRegistryMutex
// lock: it walks pending lists and the deferred-destruction queues, and
// scans the texture-view registry before releasing deferred images.
void VkRhiDevice::reclaimCompletedWorkUnlocked() {
    if (!m_initialized)
        return;
    std::array<uint64_t, 3u> completedQueueValues{};
    for (size_t index = 0u; index < completedQueueValues.size(); ++index) {
        if (vkGetSemaphoreCounterValue(m_data->device, m_data->queueTimelines[index], &completedQueueValues[index]) !=
            VK_SUCCESS)
            return;
    }
    while (!m_data->pendingSubmissions.empty()) {
        const auto& submission = m_data->pendingSubmissions.front();
        const size_t queueIndex = queueTimelineIndex(submission.queue);
        if (queueIndex >= completedQueueValues.size() || completedQueueValues[queueIndex] < submission.queueValue)
            break;
        m_data->completedSubmissionSequence = submission.sequence;
        m_data->pendingSubmissions.pop_front();
    }
    const uint64_t completed = m_data->completedSubmissionSequence;
    for (auto it = m_data->pendingLists.begin(); it != m_data->pendingLists.end();) {
        const size_t queueIndex = queueTimelineIndex(it->queue);
        if (queueIndex < completedQueueValues.size() && completedQueueValues[queueIndex] >= it->queueValue) {
            it->list->markComplete();
            it = m_data->pendingLists.erase(it);
        } else {
            ++it;
        }
    }
    while (!m_data->deferredObjects.empty() && m_data->deferredObjects.front().sequence <= completed) {
        const auto item = m_data->deferredObjects.front();
        destroyDeferredObject(*m_data, item);
        m_data->deferredObjects.pop_front();
    }
    while (!m_data->deferredBuffers.empty() && m_data->deferredBuffers.front().sequence <= completed) {
        const auto item = m_data->deferredBuffers.front();
        vmaDestroyBuffer(m_data->allocator, item.buffer, item.allocation);
        m_data->deferredBuffers.pop_front();
    }
    while (!m_data->deferredImages.empty() && m_data->deferredImages.front().sequence <= completed) {
        const auto item = m_data->deferredImages.front();
        const bool hasLiveView =
            std::any_of(m_data->textureViews.begin(), m_data->textureViews.end(),
                        [&item](const auto& view) { return handleKey(view.second.desc.texture) == item.textureKey; });
        if (hasLiveView)
            break;
        vmaDestroyImage(m_data->allocator, item.image, item.allocation);
        m_data->deferredImages.pop_front();
    }
    while (!m_data->deferredMemories.empty() && m_data->deferredMemories.front().sequence <= completed) {
        vmaFreeMemory(m_data->allocator, m_data->deferredMemories.front().allocation);
        m_data->deferredMemories.pop_front();
    }
}

VkRhiCommandListPool::VkRhiCommandListPool(VkRhiDevice& device, const RhiCommandListPoolDesc& desc)
    : m_device(&device), m_ownerThread(std::this_thread::get_id()) {
    const std::array<uint32_t, 3u> families{device.m_data->queueFamilies.graphics, device.m_data->queueFamilies.compute,
                                            device.m_data->queueFamilies.transfer};
    for (size_t i = 0u; i < families.size(); ++i) {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = families[i];
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(device.m_data->device, &info, nullptr, &pool) != VK_SUCCESS) {
            for (void* created : m_commandPools) {
                if (created != nullptr) {
                    vkDestroyCommandPool(device.m_data->device, static_cast<VkCommandPool>(created), nullptr);
                }
            }
            m_commandPools = {};
            m_device = nullptr;
            return;
        }
        m_commandPools[i] = pool;
        nameObject(*device.m_data, VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(pool), desc.debugName);
    }
    m_commandLists.reserve(desc.initialCommandListCapacity);
    const std::lock_guard<std::mutex> registryLock(device.m_data->commandRegistryMutex);
    device.m_data->commandListPools.insert(this);
}

VkRhiCommandListPool::~VkRhiCommandListPool() {
    if (m_device != nullptr) {
        if (std::this_thread::get_id() != m_ownerThread) {
            // Cross-thread destruction is tolerated when the owner thread no
            // longer uses the pool (worker-thread pools are torn down by the
            // render graph on the main thread); a list still in the
            // Recording state indicates genuinely concurrent use.
            for (const auto& list : m_commandLists) {
                if (list->state() == RhiCommandListState::Recording) {
                    std::cerr << "VkRhiDevice: command-list pool destroyed "
                                 "while its owner thread is recording\n";
                    break;
                }
            }
        }
        const std::lock_guard<std::mutex> registryLock(m_device->m_data->commandRegistryMutex);
        const std::unique_lock<std::shared_mutex> resourceLock(m_device->m_data->resourceRegistryMutex);
        if (waitDeviceIdle(*m_device->m_data) == VK_SUCCESS) {
            m_device->reclaimCompletedWorkUnlocked();
        }
        for (const auto& list : m_commandLists) {
            m_device->m_data->commandLists.erase(list.get());
            for (const auto& transient : list->m_data->transientBuffers) {
                vmaDestroyBuffer(m_device->m_data->allocator, transient.first, transient.second);
            }
            list->m_data->transientBuffers.clear();
        }
        for (void* pool : m_commandPools) {
            if (pool != nullptr) {
                vkDestroyCommandPool(m_device->m_data->device, static_cast<VkCommandPool>(pool), nullptr);
            }
        }
        m_device->m_data->commandListPools.erase(this);
    }
}

RhiCommandList* VkRhiCommandListPool::acquire(const RhiCommandListType type) {
    if (m_device == nullptr || std::this_thread::get_id() != m_ownerThread)
        return nullptr;
    // Recycle lists whose GPU completion was observed on a foreign thread;
    // the command-buffer reset must run here on the pool's owner thread.
    for (auto& list : m_commandLists) {
        if (list->m_data->completedAwaitingReset.exchange(false, std::memory_order_acq_rel)) {
            list->markComplete();
        }
    }
    for (auto& list : m_commandLists) {
        if (list->state() == RhiCommandListState::Initial && list->m_data->type == type) {
            return list.get();
        }
    }
    const size_t poolIndex = static_cast<size_t>(type);
    if (poolIndex >= m_commandPools.size() || m_commandPools[poolIndex] == nullptr)
        return nullptr;
    auto list = std::unique_ptr<VkRhiCommandList>(new VkRhiCommandList(*m_device, m_commandPools[poolIndex], type));
    if (list->nativeCommandBuffer() == nullptr)
        return nullptr;
    auto* result = list.get();
    {
        const std::lock_guard<std::mutex> registryLock(m_device->m_data->commandRegistryMutex);
        m_device->m_data->commandLists.insert(result);
    }
    m_commandLists.push_back(std::move(list));
    return result;
}

bool VkRhiCommandListPool::reset() {
    if (m_device == nullptr || std::this_thread::get_id() != m_ownerThread)
        return false;
    for (const auto& list : m_commandLists) {
        if (list->state() == RhiCommandListState::Recording || list->state() == RhiCommandListState::Pending)
            return false;
    }
    for (void* pool : m_commandPools) {
        if (vkResetCommandPool(m_device->m_data->device, static_cast<VkCommandPool>(pool), 0u) != VK_SUCCESS) {
            for (auto& list : m_commandLists)
                list->m_data->valid = false;
            return false;
        }
    }
    for (auto& list : m_commandLists) {
        for (const auto& transient : list->m_data->transientBuffers) {
            vmaDestroyBuffer(m_device->m_data->allocator, transient.first, transient.second);
        }
        list->m_data->transientBuffers.clear();
    }
    for (auto& list : m_commandLists)
        list->resetForPoolReuse();
    return true;
}

VkRhiCommandList::VkRhiCommandList(VkRhiDevice& device, void* nativeCommandPool, const RhiCommandListType type)
    : m_device(&device), m_data(std::make_unique<VkRhiCommandListData>()) {
    m_data->pool = static_cast<VkCommandPool>(nativeCommandPool);
    m_data->type = type;
    m_data->ownerThread = std::this_thread::get_id();
    VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = m_data->pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device.m_data->device, &info, &m_data->commandBuffer) != VK_SUCCESS) {
        m_data->commandBuffer = VK_NULL_HANDLE;
    }
}

VkRhiCommandList::~VkRhiCommandList() = default;

bool VkRhiCommandList::begin(const RhiCommandListDesc& desc) {
    if (m_device == nullptr || m_data->commandBuffer == VK_NULL_HANDLE ||
        m_data->state != RhiCommandListState::Initial || desc.type != m_data->type ||
        std::this_thread::get_id() != m_data->ownerThread)
        return false;
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_data->commandBuffer, &info) != VK_SUCCESS)
        return false;
    m_data->state = RhiCommandListState::Recording;
    m_data->valid = true;
    m_data->resourceReferences.clear();
    nameObject(*m_device->m_data, VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(m_data->commandBuffer),
               desc.debugName);
    return true;
}

bool VkRhiCommandList::end() {
    if (m_data->state != RhiCommandListState::Recording || std::this_thread::get_id() != m_data->ownerThread)
        return false;
    if (m_data->rendering || !m_data->valid || vkEndCommandBuffer(m_data->commandBuffer) != VK_SUCCESS) {
        if (vkResetCommandBuffer(m_data->commandBuffer, 0u) == VK_SUCCESS) {
            for (const auto& transient : m_data->transientBuffers) {
                vmaDestroyBuffer(m_device->m_data->allocator, transient.first, transient.second);
            }
            resetForPoolReuse();
        } else {
            m_data->valid = false;
        }
        return false;
    }
    m_data->state = RhiCommandListState::Executable;
    return true;
}

RhiCommandListState VkRhiCommandList::state() const {
    return m_data->state;
}

void VkRhiCommandList::resetForPoolReuse() {
    m_data->state = RhiCommandListState::Initial;
    m_data->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    m_data->pipelineLayout = VK_NULL_HANDLE;
    m_data->pipelineLayoutHandle = {};
    m_data->pendingSequence = 0u;
    m_data->rendering = false;
    m_data->graphicsPipelineBound = false;
    m_data->renderingTargetWidth = 0u;
    m_data->renderingTargetHeight = 0u;
    m_data->valid = true;
    m_data->transientBuffers.clear();
    m_data->initializedSwapchainImages.clear();
    m_data->resourceReferences.clear();
}

void VkRhiCommandList::markPending(const uint64_t sequence) {
    m_data->state = RhiCommandListState::Pending;
    m_data->pendingSequence = sequence;
}

void VkRhiCommandList::markComplete() {
    if (m_data->state != RhiCommandListState::Pending) {
        return;
    }
    if (std::this_thread::get_id() != m_data->ownerThread) {
        // vkResetCommandBuffer requires external synchronization of the
        // owning command pool. Completion observed on a foreign thread only
        // flags the list; the owner thread resets it on its next acquire.
        m_data->completedAwaitingReset.store(true, std::memory_order_release);
        return;
    }
    if (vkResetCommandBuffer(m_data->commandBuffer, 0u) == VK_SUCCESS) {
        resetForPoolReuse();
    } else {
        m_data->valid = false;
    }
}

bool VkRhiCommandList::isExecutable() const {
    return m_data->state == RhiCommandListState::Executable && m_data->valid;
}

void* VkRhiCommandList::nativeCommandBuffer() const {
    return m_data->commandBuffer;
}

void VkRhiCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    if (m_data->state != RhiCommandListState::Recording || name == nullptr || m_device->m_data->beginLabel == nullptr)
        return;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name;
    std::memcpy(label.color, &color[0], sizeof(label.color));
    m_device->m_data->beginLabel(m_data->commandBuffer, &label);
}

void VkRhiCommandList::endDebugLabel() {
    if (m_data->state == RhiCommandListState::Recording && m_device->m_data->endLabel != nullptr) {
        m_device->m_data->endLabel(m_data->commandBuffer);
    }
}

void VkRhiCommandList::insertDebugMarker(const char* name, const glm::vec4& color) {
    if (m_data->state != RhiCommandListState::Recording || name == nullptr || m_device->m_data->insertLabel == nullptr)
        return;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name;
    std::memcpy(label.color, &color[0], sizeof(label.color));
    m_device->m_data->insertLabel(m_data->commandBuffer, &label);
}

void VkRhiCommandList::textureBarrier(const RhiTextureBarrier& barrier) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    auto* texture = findRecord(m_device->m_data->textures, barrier.texture);
    const uint32_t layers = texture != nullptr && texture->desc.dimension != RhiTextureDimension::Texture3D
                                ? texture->desc.depthOrLayers
                                : 1u;
    const uint32_t mipCount = texture != nullptr && barrier.mipCount == kRhiRemainingMipLevels
                                  ? texture->desc.mipLevels - std::min(barrier.baseMip, texture->desc.mipLevels)
                                  : barrier.mipCount;
    const uint32_t layerCount = barrier.layerCount == kRhiRemainingArrayLayers
                                    ? layers - std::min(barrier.baseLayer, layers)
                                    : barrier.layerCount;
    if (m_data->state != RhiCommandListState::Recording || texture == nullptr ||
        barrier.baseMip >= texture->desc.mipLevels || mipCount == 0u ||
        mipCount > texture->desc.mipLevels - barrier.baseMip || barrier.baseLayer >= layers || layerCount == 0u ||
        layerCount > layers - barrier.baseLayer) {
        m_data->valid = false;
        return;
    }
    auto oldState = toVkCommandResourceState(barrier.oldState, m_data->type);
    auto newState = toVkCommandResourceState(barrier.newState, m_data->type);
    if (barrier.oldState == RhiResourceState::Undefined) {
        // A discarding transition still needs an execution dependency: when
        // the image aliases memory another texture just used, its first
        // write must not overlap that texture's in-flight GPU work.
        oldState.stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        oldState.access = VK_ACCESS_2_NONE;
    }
#if defined(MECRAFT_ENABLE_STREAMLINE)
    const bool frameGenerationSwapchain =
        texture->swapchainOwned && StreamlineRuntime::instance().dlssFrameGenerationLoaded();
    if (frameGenerationSwapchain && barrier.oldState == RhiResourceState::Present) {
        if (m_device->m_data->frameGenerationAcquireLayoutPending) {
            oldState = {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_GENERAL};
            m_device->m_data->frameGenerationAcquireLayoutPending = false;
        } else {
            oldState = toVkResourceState(RhiResourceState::TransferSrc);
        }
    }
    if (frameGenerationSwapchain && barrier.newState == RhiResourceState::Present) {
        newState = toVkResourceState(RhiResourceState::TransferSrc);
    }
#endif
    VkImageMemoryBarrier2 native{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    native.srcStageMask = oldState.stages;
    native.srcAccessMask = oldState.access;
    native.dstStageMask = newState.stages;
    native.dstAccessMask = newState.access;
    if (barrier.phase == RhiBarrierPhase::Release) {
        native.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        native.dstAccessMask = VK_ACCESS_2_NONE;
    } else if (barrier.phase == RhiBarrierPhase::Acquire) {
        native.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        native.srcAccessMask = VK_ACCESS_2_NONE;
    }
    native.oldLayout = oldState.layout;
    if (texture->swapchainOwned && barrier.oldState == RhiResourceState::Present) {
        const auto swapchainIt = std::find_if(
            m_device->m_data->swapchainTextures.begin(), m_device->m_data->swapchainTextures.end(),
            [&barrier](const RhiTextureHandle handle) {
                return handle.index == barrier.texture.index && handle.generation == barrier.texture.generation;
            });
        if (swapchainIt != m_device->m_data->swapchainTextures.end()) {
            const size_t imageIndex =
                static_cast<size_t>(std::distance(m_device->m_data->swapchainTextures.begin(), swapchainIt));
            if (!m_device->m_data->swapchainImageInitialized[imageIndex]) {
                native.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                native.srcAccessMask = VK_ACCESS_2_NONE;
                native.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (std::find(m_data->initializedSwapchainImages.begin(), m_data->initializedSwapchainImages.end(),
                              static_cast<uint32_t>(imageIndex)) == m_data->initializedSwapchainImages.end()) {
                    m_data->initializedSwapchainImages.push_back(static_cast<uint32_t>(imageIndex));
                }
            }
        }
    }
    native.newLayout = newState.layout;
    native.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
    native.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
    native.image = texture->image;
    native.subresourceRange.aspectMask = toVkImageAspectFlags(barrier.aspect, texture->desc.format);
    native.subresourceRange.baseMipLevel = barrier.baseMip;
    native.subresourceRange.levelCount = mipCount;
    native.subresourceRange.baseArrayLayer = barrier.baseLayer;
    native.subresourceRange.layerCount = layerCount;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1u;
    dependency.pImageMemoryBarriers = &native;
    vkCmdPipelineBarrier2(m_data->commandBuffer, &dependency);
    m_data->resourceReferences.reference(barrier.texture);
}

void VkRhiCommandList::bufferBarrier(const RhiBufferBarrier& barrier) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    auto* buffer = findRecord(m_device->m_data->buffers, barrier.buffer);
    const uint64_t size = buffer != nullptr && barrier.size == kRhiWholeSize
                              ? buffer->desc.size - std::min(barrier.offset, buffer->desc.size)
                              : barrier.size;
    if (m_data->state != RhiCommandListState::Recording || buffer == nullptr || barrier.offset >= buffer->desc.size ||
        size == 0u || size > buffer->desc.size - barrier.offset) {
        m_data->valid = false;
        return;
    }
    const auto oldState = toVkCommandResourceState(barrier.oldState, m_data->type);
    const auto newState = toVkCommandResourceState(barrier.newState, m_data->type);
    VkBufferMemoryBarrier2 native{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    native.srcStageMask = oldState.stages;
    native.srcAccessMask = oldState.access;
    native.dstStageMask = newState.stages;
    native.dstAccessMask = newState.access;
    if (barrier.phase == RhiBarrierPhase::Release) {
        native.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        native.dstAccessMask = VK_ACCESS_2_NONE;
    } else if (barrier.phase == RhiBarrierPhase::Acquire) {
        native.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        native.srcAccessMask = VK_ACCESS_2_NONE;
    }
    native.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
    native.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
    native.buffer = buffer->buffer;
    native.offset = barrier.offset;
    native.size = size;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1u;
    dependency.pBufferMemoryBarriers = &native;
    vkCmdPipelineBarrier2(m_data->commandBuffer, &dependency);
    m_data->resourceReferences.reference(barrier.buffer);
}

bool VkRhiCommandList::accelerationStructureBarrier(const RhiAccelerationStructureBarrier& barrier) {
    const auto isAccelerationStructureState = [](const RhiResourceState state) {
        return state == RhiResourceState::AccelerationStructureBuildWrite ||
               state == RhiResourceState::AccelerationStructureRead;
    };
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        m_data->type == RhiCommandListType::Transfer || !isAccelerationStructureState(barrier.oldState) ||
        !isAccelerationStructureState(barrier.newState)) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    if (findRecord(m_device->m_data->accelerationStructures, barrier.accelerationStructure) == nullptr) {
        return false;
    }
    const auto source = toVkCommandResourceState(barrier.oldState, m_data->type);
    const auto destination = toVkCommandResourceState(barrier.newState, m_data->type);
    VkMemoryBarrier2 nativeBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    nativeBarrier.srcStageMask = source.stages;
    nativeBarrier.srcAccessMask = source.access;
    nativeBarrier.dstStageMask = destination.stages;
    nativeBarrier.dstAccessMask = destination.access;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1u;
    dependency.pMemoryBarriers = &nativeBarrier;
    vkCmdPipelineBarrier2(m_data->commandBuffer, &dependency);
    m_data->resourceReferences.reference(barrier.accelerationStructure);
    return true;
}

void VkRhiCommandList::beginRendering(const RhiRenderingInfo& info) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        info.colorAttachmentCount > m_device->m_capabilities.maxColorAttachments) {
        m_data->valid = false;
        return;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    uint32_t targetWidth = 0u;
    uint32_t targetHeight = 0u;
    const auto registerAttachmentExtent = [&](const RhiTextureViewHandle viewHandle) {
        const auto* view = findRecord(m_device->m_data->textureViews, viewHandle);
        const auto* texture = view != nullptr ? findRecord(m_device->m_data->textures, view->desc.texture) : nullptr;
        if (view == nullptr || texture == nullptr) {
            return false;
        }
        const uint32_t width = std::max(texture->desc.width >> view->desc.baseMip, 1u);
        const uint32_t height = std::max(texture->desc.height >> view->desc.baseMip, 1u);
        if (targetWidth == 0u && targetHeight == 0u) {
            targetWidth = width;
            targetHeight = height;
            return true;
        }
        return targetWidth == width && targetHeight == height;
    };
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(info.colorAttachmentCount);
    for (uint32_t i = 0u; i < info.colorAttachmentCount; ++i) {
        const auto* view = findRecord(m_device->m_data->textureViews, info.colorAttachments[i].view);
        const auto* texture = view != nullptr ? findRecord(m_device->m_data->textures, view->desc.texture) : nullptr;
        const RhiTextureFormat viewFormat =
            view != nullptr && texture != nullptr
                ? (view->desc.format == RhiTextureFormat::Undefined ? texture->desc.format : view->desc.format)
                : RhiTextureFormat::Undefined;
        const bool unsignedInteger = rhiTextureFormatIsUnsignedInteger(viewFormat);
        if (view == nullptr || texture == nullptr || !registerAttachmentExtent(info.colorAttachments[i].view) ||
            (info.colorAttachments[i].loadOp == RhiLoadOp::Clear &&
             unsignedInteger != (info.colorAttachments[i].clearValueType == RhiColorClearValueType::Uint))) {
            m_data->valid = false;
            return;
        }
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = view->view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = toVkLoadOp(info.colorAttachments[i].loadOp);
        attachment.storeOp = toVkStoreOp(info.colorAttachments[i].storeOp);
        if (info.colorAttachments[i].clearValueType == RhiColorClearValueType::Uint) {
            std::memcpy(attachment.clearValue.color.uint32, info.colorAttachments[i].clearColorUint,
                        sizeof(uint32_t) * 4u);
        } else {
            std::memcpy(attachment.clearValue.color.float32, info.colorAttachments[i].clearColor, sizeof(float) * 4u);
        }
        colors.push_back(attachment);
        m_data->resourceReferences.reference(info.colorAttachments[i].view);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (info.depthStencilAttachment != nullptr) {
        const auto* view = findRecord(m_device->m_data->textureViews, info.depthStencilAttachment->view);
        if (view == nullptr || !registerAttachmentExtent(info.depthStencilAttachment->view)) {
            m_data->valid = false;
            return;
        }
        depth.imageView = view->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = toVkLoadOp(info.depthStencilAttachment->depthLoadOp);
        depth.storeOp = toVkStoreOp(info.depthStencilAttachment->depthStoreOp);
        depth.clearValue.depthStencil = {info.depthStencilAttachment->clearDepth,
                                         info.depthStencilAttachment->clearStencil};
        m_data->resourceReferences.reference(info.depthStencilAttachment->view);
    }
    if (targetWidth == 0u || targetHeight == 0u || info.renderArea.x < 0 || info.renderArea.y < 0 ||
        static_cast<uint64_t>(info.renderArea.x) + info.renderArea.width > targetWidth ||
        static_cast<uint64_t>(info.renderArea.y) + info.renderArea.height > targetHeight) {
        m_data->valid = false;
        return;
    }
    const int32_t nativeRenderAreaY =
        static_cast<int32_t>(targetHeight) - info.renderArea.y - static_cast<int32_t>(info.renderArea.height);
    VkRenderingInfo native{VK_STRUCTURE_TYPE_RENDERING_INFO};
    native.renderArea = {{info.renderArea.x, nativeRenderAreaY}, {info.renderArea.width, info.renderArea.height}};
    native.layerCount = 1u;
    native.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    native.pColorAttachments = colors.data();
    native.pDepthAttachment = info.depthStencilAttachment != nullptr ? &depth : nullptr;
    native.pStencilAttachment = nullptr;
    const float nativeViewportY = static_cast<float>(static_cast<int32_t>(targetHeight) - info.renderArea.y);
    const float nativeViewportHeight = -static_cast<float>(info.renderArea.height);
    const VkViewport viewport{static_cast<float>(info.renderArea.x),
                              nativeViewportY,
                              static_cast<float>(info.renderArea.width),
                              nativeViewportHeight,
                              0.0f,
                              1.0f};
    const VkRect2D scissor{{info.renderArea.x, nativeRenderAreaY}, {info.renderArea.width, info.renderArea.height}};
    vkCmdSetViewport(m_data->commandBuffer, 0u, 1u, &viewport);
    vkCmdSetScissor(m_data->commandBuffer, 0u, 1u, &scissor);
    vkCmdBeginRendering(m_data->commandBuffer, &native);
    m_data->rendering = true;
    m_data->renderingTargetWidth = targetWidth;
    m_data->renderingTargetHeight = targetHeight;
}

void VkRhiCommandList::endRendering() {
    if (m_data->state != RhiCommandListState::Recording || !m_data->rendering) {
        m_data->valid = false;
        return;
    }
    vkCmdEndRendering(m_data->commandBuffer);
    m_data->rendering = false;
    m_data->renderingTargetWidth = 0u;
    m_data->renderingTargetHeight = 0u;
}

void VkRhiCommandList::clearDepthAttachment(const float depth, const RhiRect2D& rect) {
    if (!m_data->rendering) {
        m_data->valid = false;
        return;
    }
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachment.clearValue.depthStencil.depth = depth;
    const VkRect2D nativeRect = toVkClippedScissor(rect, m_data->renderingTargetWidth, m_data->renderingTargetHeight);
    VkClearRect clearRect{nativeRect, 0u, 1u};
    vkCmdClearAttachments(m_data->commandBuffer, 1u, &attachment, 1u, &clearRect);
}

void VkRhiCommandList::setViewport(const RhiViewport& viewport) {
    if (m_data->state != RhiCommandListState::Recording || !m_data->rendering || viewport.x < 0.0f ||
        viewport.y < 0.0f || viewport.width <= 0.0f || viewport.height <= 0.0f ||
        viewport.x + viewport.width > static_cast<float>(m_data->renderingTargetWidth) ||
        viewport.y + viewport.height > static_cast<float>(m_data->renderingTargetHeight)) {
        m_data->valid = false;
        return;
    }
    const float nativeY = static_cast<float>(m_data->renderingTargetHeight) - viewport.y;
    const float nativeHeight = -viewport.height;
    const VkViewport native{viewport.x, nativeY, viewport.width, nativeHeight, viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(m_data->commandBuffer, 0u, 1u, &native);
}

void VkRhiCommandList::setScissor(const RhiRect2D& rect) {
    if (!m_data->rendering) {
        m_data->valid = false;
        return;
    }
    const VkRect2D native = toVkClippedScissor(rect, m_data->renderingTargetWidth, m_data->renderingTargetHeight);
    vkCmdSetScissor(m_data->commandBuffer, 0u, 1u, &native);
}

void VkRhiCommandList::setGraphicsPipeline(const RhiPipelineHandle pipeline) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->pipelines, pipeline);
    if (record == nullptr || record->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        m_data->valid = false;
        return;
    }
    const auto* layout = findRecord(m_device->m_data->pipelineLayouts, record->layoutHandle);
    if (layout == nullptr) {
        m_data->valid = false;
        return;
    }
    vkCmdBindPipeline(m_data->commandBuffer, record->bindPoint, record->pipeline);
    m_data->bindPoint = record->bindPoint;
    m_data->pipelineLayout = layout->layout;
    m_data->pipelineLayoutHandle = record->layoutHandle;
    m_data->graphicsPipelineBound = true;
    m_data->resourceReferences.reference(pipeline);
}

void VkRhiCommandList::setComputePipeline(const RhiPipelineHandle pipeline) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->pipelines, pipeline);
    if (record == nullptr || record->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE || m_data->rendering) {
        m_data->valid = false;
        return;
    }
    const auto* layout = findRecord(m_device->m_data->pipelineLayouts, record->layoutHandle);
    if (layout == nullptr) {
        m_data->valid = false;
        return;
    }
    vkCmdBindPipeline(m_data->commandBuffer, record->bindPoint, record->pipeline);
    m_data->bindPoint = record->bindPoint;
    m_data->pipelineLayout = layout->layout;
    m_data->pipelineLayoutHandle = record->layoutHandle;
    m_data->resourceReferences.reference(pipeline);
}

void VkRhiCommandList::setBindGroup(const uint32_t setIndex, const RhiBindGroupHandle bindGroup) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->bindGroups, bindGroup);
    const auto* pipelineLayout = findRecord(m_device->m_data->pipelineLayouts, m_data->pipelineLayoutHandle);
    if (record == nullptr || pipelineLayout == nullptr || setIndex >= pipelineLayout->desc.bindGroupLayouts.size() ||
        pipelineLayout->desc.bindGroupLayouts[setIndex].index != record->layoutHandle.index ||
        pipelineLayout->desc.bindGroupLayouts[setIndex].generation != record->layoutHandle.generation) {
        m_data->valid = false;
        return;
    }
    vkCmdBindDescriptorSets(m_data->commandBuffer, m_data->bindPoint, m_data->pipelineLayout, setIndex, 1u,
                            &record->set, 0u, nullptr);
    m_data->resourceReferences.reference(bindGroup);
}

void VkRhiCommandList::setVertexBuffer(const uint32_t slot, const RhiBufferHandle buffer, const uint64_t offset) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || offset >= record->desc.size) {
        m_data->valid = false;
        return;
    }
    vkCmdBindVertexBuffers(m_data->commandBuffer, slot, 1u, &record->buffer, &offset);
    m_data->resourceReferences.reference(buffer);
}

void VkRhiCommandList::setIndexBuffer(const RhiBufferHandle buffer, const RhiIndexFormat format,
                                      const uint64_t offset) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || offset >= record->desc.size) {
        m_data->valid = false;
        return;
    }
    vkCmdBindIndexBuffer(m_data->commandBuffer, record->buffer, offset,
                         format == RhiIndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    m_data->resourceReferences.reference(buffer);
}

void VkRhiCommandList::pushConstants(const void* data, const size_t size, const RhiShaderStageFlags stages) {
    if (data == nullptr || size == 0u || m_data->pipelineLayout == VK_NULL_HANDLE ||
        size > m_device->m_data->properties.limits.maxPushConstantsSize) {
        m_data->valid = false;
        return;
    }
    vkCmdPushConstants(m_data->commandBuffer, m_data->pipelineLayout, toVkShaderStageFlags(stages), 0u,
                       static_cast<uint32_t>(size), data);
}

void VkRhiCommandList::draw(const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t firstVertex,
                            const uint32_t firstInstance) {
    if (!m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        m_data->valid = false;
        return;
    }
    vkCmdDraw(m_data->commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VkRhiCommandList::drawIndexed(const uint32_t indexCount, const uint32_t instanceCount, const uint32_t firstIndex,
                                   const int32_t vertexOffset, const uint32_t firstInstance) {
    if (!m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        m_data->valid = false;
        return;
    }
    vkCmdDrawIndexed(m_data->commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VkRhiCommandList::drawIndirect(const RhiBufferHandle indirectBuffer, const uint64_t offset,
                                    const uint32_t drawCount, const uint32_t stride) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->buffers, indirectBuffer);
    if (record == nullptr || !m_data->rendering) {
        m_data->valid = false;
        return;
    }
    vkCmdDrawIndirect(m_data->commandBuffer, record->buffer, offset, drawCount, stride);
    m_data->resourceReferences.reference(indirectBuffer);
}

void VkRhiCommandList::dispatch(const uint32_t groupCountX, const uint32_t groupCountY, const uint32_t groupCountZ) {
    if (m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        m_data->valid = false;
        return;
    }
    vkCmdDispatch(m_data->commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VkRhiCommandList::updateBuffer(const RhiBufferHandle buffer, const uint64_t offset, const void* data,
                                    const size_t size) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || data == nullptr || size == 0u || offset > record->desc.size ||
        size > record->desc.size - offset || m_data->state != RhiCommandListState::Recording) {
        m_data->valid = false;
        return;
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeInfo{};
    if (vmaCreateBuffer(m_device->m_data->allocator, &bufferInfo, &allocationInfo, &staging, &allocation,
                        &nativeInfo) != VK_SUCCESS) {
        m_data->valid = false;
        return;
    }
    std::memcpy(nativeInfo.pMappedData, data, size);
    vmaFlushAllocation(m_device->m_data->allocator, allocation, 0u, size);
    const VkBufferCopy region{0u, offset, size};
    vkCmdCopyBuffer(m_data->commandBuffer, staging, record->buffer, 1u, &region);
    m_data->transientBuffers.emplace_back(staging, allocation);
    m_data->resourceReferences.reference(buffer);
}

void VkRhiCommandList::copyBuffer(const RhiBufferCopy& copy) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* source = findRecord(m_device->m_data->buffers, copy.src);
    const auto* destination = findRecord(m_device->m_data->buffers, copy.dst);
    if (source == nullptr || destination == nullptr || copy.size == 0u) {
        m_data->valid = false;
        return;
    }
    const VkBufferCopy region{copy.srcOffset, copy.dstOffset, copy.size};
    vkCmdCopyBuffer(m_data->commandBuffer, source->buffer, destination->buffer, 1u, &region);
    m_data->resourceReferences.reference(copy.src);
    m_data->resourceReferences.reference(copy.dst);
}

void VkRhiCommandList::copyBufferToTexture(const RhiBufferTextureCopy& copy) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* source = findRecord(m_device->m_data->buffers, copy.srcBuffer);
    const auto* destination = findRecord(m_device->m_data->textures, copy.dstTexture);
    if (source == nullptr || destination == nullptr) {
        m_data->valid = false;
        return;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = copy.bufferOffset;
    region.imageSubresource = {defaultAspectForFormat(destination->desc.format), copy.mipLevel, copy.arrayLayer, 1u};
    region.imageOffset = {static_cast<int32_t>(copy.dstX), static_cast<int32_t>(copy.dstY),
                          static_cast<int32_t>(copy.dstZ)};
    region.imageExtent = {copy.width, copy.height, copy.depth};
    vkCmdCopyBufferToImage(m_data->commandBuffer, source->buffer, destination->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    m_data->resourceReferences.reference(copy.srcBuffer);
    m_data->resourceReferences.reference(copy.dstTexture);
}

void VkRhiCommandList::copyTextureToBuffer(const RhiTextureBufferCopy& copy) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* source = findRecord(m_device->m_data->textures, copy.srcTexture);
    const auto* destination = findRecord(m_device->m_data->buffers, copy.dstBuffer);
    if (source == nullptr || destination == nullptr) {
        m_data->valid = false;
        return;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = copy.bufferOffset;
    const uint32_t byteSize = formatByteSize(source->desc.format);
    if (copy.bytesPerRow != 0u && (byteSize == 0u || copy.bytesPerRow % byteSize != 0u)) {
        m_data->valid = false;
        return;
    }
    region.bufferRowLength = copy.bytesPerRow == 0u ? 0u : copy.bytesPerRow / byteSize;
    region.bufferImageHeight = copy.rowsPerImage;
    region.imageSubresource = {defaultAspectForFormat(source->desc.format), copy.mipLevel, copy.arrayLayer, 1u};
    region.imageOffset = {static_cast<int32_t>(copy.srcX), static_cast<int32_t>(copy.srcY),
                          static_cast<int32_t>(copy.srcZ)};
    region.imageExtent = {copy.width, copy.height, copy.depth};
    vkCmdCopyImageToBuffer(m_data->commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           destination->buffer, 1u, &region);
    m_data->resourceReferences.reference(copy.srcTexture);
    m_data->resourceReferences.reference(copy.dstBuffer);
}

void VkRhiCommandList::copyTexture(const RhiTextureCopy& copy) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* source = findRecord(m_device->m_data->textures, copy.src);
    const auto* destination = findRecord(m_device->m_data->textures, copy.dst);
    if (source == nullptr || destination == nullptr) {
        m_data->valid = false;
        return;
    }
    VkImageCopy region{};
    region.srcSubresource = {defaultAspectForFormat(source->desc.format), copy.srcSubresource.mipLevel,
                             copy.srcSubresource.baseArrayLayer, copy.srcSubresource.layerCount};
    region.srcOffset = {static_cast<int32_t>(copy.srcOffset.x), static_cast<int32_t>(copy.srcOffset.y),
                        static_cast<int32_t>(copy.srcOffset.z)};
    region.dstSubresource = {defaultAspectForFormat(destination->desc.format), copy.dstSubresource.mipLevel,
                             copy.dstSubresource.baseArrayLayer, copy.dstSubresource.layerCount};
    region.dstOffset = {static_cast<int32_t>(copy.dstOffset.x), static_cast<int32_t>(copy.dstOffset.y),
                        static_cast<int32_t>(copy.dstOffset.z)};
    region.extent = {copy.extent.width, copy.extent.height, copy.extent.depth};
    vkCmdCopyImage(m_data->commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    m_data->resourceReferences.reference(copy.src);
    m_data->resourceReferences.reference(copy.dst);
}

void VkRhiCommandList::blitTexture(const RhiTextureBlit& blit) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    RhiTextureHandle sourceHandle = blit.src;
    RhiTextureHandle destinationHandle = blit.dst;
    const auto* sourceView =
        blit.srcView.isValid() ? findRecord(m_device->m_data->textureViews, blit.srcView) : nullptr;
    const auto* destinationView =
        blit.dstView.isValid() ? findRecord(m_device->m_data->textureViews, blit.dstView) : nullptr;
    if (!sourceHandle.isValid() && sourceView != nullptr) {
        sourceHandle = sourceView->desc.texture;
    }
    if (!destinationHandle.isValid() && destinationView != nullptr) {
        destinationHandle = destinationView->desc.texture;
    }
    const auto* source = findRecord(m_device->m_data->textures, sourceHandle);
    const auto* destination = findRecord(m_device->m_data->textures, destinationHandle);
    const VkFormat sourceFormat = source != nullptr ? toVkFormat(source->desc.format) : VK_FORMAT_UNDEFINED;
    const VkFormat destinationFormat =
        destination != nullptr ? toVkFormat(destination->desc.format) : VK_FORMAT_UNDEFINED;
    const VkImageAspectFlags sourceAspect = source != nullptr ? defaultAspectForFormat(source->desc.format) : 0u;
    const VkImageAspectFlags destinationAspect =
        destination != nullptr ? defaultAspectForFormat(destination->desc.format) : 0u;
    const bool colorBlit = (sourceAspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0u;
    const VkFormatFeatureFlags2 sourceFeatures =
        VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | (colorBlit ? VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT : 0u);
    if (source == nullptr || destination == nullptr || sourceAspect != destinationAspect ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice, sourceFormat, sourceFeatures) ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice, destinationFormat,
                                VK_FORMAT_FEATURE_2_BLIT_DST_BIT)) {
        m_data->valid = false;
        return;
    }
    VkImageBlit region{};
    region.srcSubresource = {defaultAspectForFormat(source->desc.format), blit.srcMipLevel, 0u, 1u};
    const int32_t sourceWidth = static_cast<int32_t>(std::max(source->desc.width >> blit.srcMipLevel, 1u));
    const int32_t sourceHeight = static_cast<int32_t>(std::max(source->desc.height >> blit.srcMipLevel, 1u));
    region.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
    region.dstSubresource = {defaultAspectForFormat(destination->desc.format), blit.dstMipLevel, 0u, 1u};
    const int32_t destinationWidth = static_cast<int32_t>(std::max(destination->desc.width >> blit.dstMipLevel, 1u));
    const int32_t destinationHeight = static_cast<int32_t>(std::max(destination->desc.height >> blit.dstMipLevel, 1u));
    region.dstOffsets[1] = {destinationWidth, destinationHeight, 1};
    vkCmdBlitImage(m_data->commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region, colorBlit ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    m_data->resourceReferences.reference(sourceHandle);
    m_data->resourceReferences.reference(destinationHandle);
    m_data->resourceReferences.reference(blit.srcView);
    m_data->resourceReferences.reference(blit.dstView);
}

void VkRhiCommandList::generateMipmaps(const RhiTextureHandle textureHandle) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* texture = findRecord(m_device->m_data->textures, textureHandle);
    if (texture == nullptr || texture->desc.mipLevels < 2u ||
        (defaultAspectForFormat(texture->desc.format) & VK_IMAGE_ASPECT_COLOR_BIT) == 0u ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice, toVkFormat(texture->desc.format),
                                VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                                    VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        m_data->valid = false;
        return;
    }
    int32_t width = static_cast<int32_t>(texture->desc.width);
    int32_t height = static_cast<int32_t>(texture->desc.height);
    int32_t depth = texture->desc.dimension == RhiTextureDimension::Texture3D
                        ? static_cast<int32_t>(texture->desc.depthOrLayers)
                        : 1;
    const uint32_t layers =
        texture->desc.dimension == RhiTextureDimension::Texture3D ? 1u : texture->desc.depthOrLayers;
    for (uint32_t mip = 1u; mip < texture->desc.mipLevels; ++mip) {
        VkImageMemoryBarrier2 toSource{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toSource.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        toSource.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toSource.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        toSource.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSource.image = texture->image;
        toSource.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 1u, 0u, layers};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1u;
        dependency.pImageMemoryBarriers = &toSource;
        vkCmdPipelineBarrier2(m_data->commandBuffer, &dependency);
        const int32_t nextWidth = std::max(width / 2, 1);
        const int32_t nextHeight = std::max(height / 2, 1);
        const int32_t nextDepth = std::max(depth / 2, 1);
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1u, 0u, layers};
        region.srcOffsets[1] = {width, height, depth};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0u, layers};
        region.dstOffsets[1] = {nextWidth, nextHeight, nextDepth};
        vkCmdBlitImage(m_data->commandBuffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region, VK_FILTER_LINEAR);
        toSource.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSource.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier2(m_data->commandBuffer, &dependency);
        width = nextWidth;
        height = nextHeight;
        depth = nextDepth;
    }
    m_data->resourceReferences.reference(textureHandle);
}

void VkRhiCommandList::resetQueryPool(const RhiQueryPoolHandle pool, const uint32_t firstQuery,
                                      const uint32_t queryCount) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->queryPools, pool);
    if (record == nullptr || firstQuery > record->count || queryCount > record->count - firstQuery) {
        m_data->valid = false;
        return;
    }
    vkCmdResetQueryPool(m_data->commandBuffer, record->pool, firstQuery, queryCount);
    m_data->resourceReferences.reference(pool);
}

void VkRhiCommandList::writeTimestamp(const RhiQueryPoolHandle pool, const uint32_t queryIndex) {
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* record = findRecord(m_device->m_data->queryPools, pool);
    if (record == nullptr || record->type != RhiQueryType::Timestamp || queryIndex >= record->count) {
        m_data->valid = false;
        return;
    }
    vkCmdWriteTimestamp2(m_data->commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, record->pool, queryIndex);
    m_data->resourceReferences.reference(pool);
}

bool VkRhiCommandList::buildAccelerationStructures(const RhiAccelerationStructureBuildDesc* builds,
                                                   const uint32_t buildCount) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        m_data->type == RhiCommandListType::Transfer || builds == nullptr || buildCount == 0u) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    std::vector<NativeAccelerationStructureBuildInput> nativeInputs(buildCount);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> nativeBuilds(buildCount);
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> nativeRangePointers(buildCount);
    std::vector<uint64_t> destinationKeys;
    std::vector<uint64_t> sourceKeys;
    struct ScratchRange {
        RhiBufferHandle buffer;
        uint64_t offset = 0u;
        uint64_t size = 0u;
    };
    std::vector<ScratchRange> scratchRanges;
    destinationKeys.reserve(buildCount);
    sourceKeys.reserve(buildCount);
    scratchRanges.reserve(buildCount);
    VkRhiCommandResourceReferences stagedReferences;

    for (uint32_t buildIndex = 0u; buildIndex < buildCount; ++buildIndex) {
        const RhiAccelerationStructureBuildDesc& build = builds[buildIndex];
        if (!fillNativeAccelerationStructureBuildInput(*m_device->m_data, m_device->m_capabilities, build.input,
                                                       nativeInputs[buildIndex], &stagedReferences)) {
            return false;
        }
        const auto* destination = findRecord(m_device->m_data->accelerationStructures, build.destination);
        const auto* source =
            build.source.isValid() ? findRecord(m_device->m_data->accelerationStructures, build.source) : nullptr;
        const auto* scratch = findRecord(m_device->m_data->buffers, build.scratchBuffer);
        constexpr RhiBufferUsageFlags kScratchUsages =
            rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
        const bool update = build.mode == RhiAccelerationStructureBuildMode::Update;
        const bool modeValid = update || build.mode == RhiAccelerationStructureBuildMode::Build;
        if (!modeValid || destination == nullptr || destination->desc.type != build.input.type || scratch == nullptr ||
            !bufferHasUsages(*scratch, kScratchUsages) ||
            build.scratchOffset % m_device->m_capabilities.minAccelerationStructureScratchOffsetAlignment != 0u ||
            (!update && build.source.isValid()) ||
            (update && (source == nullptr || source->desc.type != build.input.type ||
                        (build.input.flags & rhiFlag(RhiAccelerationStructureBuildFlag::AllowUpdate)) == 0u))) {
            return false;
        }
        const uint64_t destinationKey = handleKey(build.destination);
        const uint64_t sourceKey = source != nullptr ? handleKey(build.source) : 0u;
        for (size_t previousIndex = 0u; previousIndex < destinationKeys.size(); ++previousIndex) {
            if (destinationKeys[previousIndex] == destinationKey ||
                (sourceKey != 0u && destinationKeys[previousIndex] == sourceKey) ||
                (sourceKeys[previousIndex] != 0u && sourceKeys[previousIndex] == destinationKey)) {
                return false;
            }
        }
        destinationKeys.push_back(destinationKey);
        sourceKeys.push_back(sourceKey);

        VkAccelerationStructureBuildGeometryInfoKHR sizeQuery{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        sizeQuery.type = toVkAccelerationStructureType(build.input.type);
        sizeQuery.flags = toVkAccelerationStructureBuildFlags(build.input.flags);
        sizeQuery.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        sizeQuery.geometryCount = build.input.geometryCount;
        sizeQuery.pGeometries = nativeInputs[buildIndex].geometries.data();
        VkAccelerationStructureBuildSizesInfoKHR nativeSizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        m_device->m_data->getAccelerationStructureBuildSizes(
            m_device->m_data->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &sizeQuery,
            nativeInputs[buildIndex].primitiveCounts.data(), &nativeSizes);
        const uint64_t scratchSize = update ? nativeSizes.updateScratchSize : nativeSizes.buildScratchSize;
        const VkDeviceAddress scratchAddress = nativeBufferDeviceAddress(*m_device->m_data, *scratch);
        if (nativeSizes.accelerationStructureSize == 0u || scratchSize == 0u ||
            destination->desc.size < nativeSizes.accelerationStructureSize ||
            !rangeFits(scratch->desc.size, build.scratchOffset, scratchSize) || scratchAddress == 0u ||
            (scratchAddress + build.scratchOffset) %
                    m_device->m_capabilities.minAccelerationStructureScratchOffsetAlignment !=
                0u) {
            return false;
        }
        for (const ScratchRange& previous : scratchRanges) {
            if (previous.buffer.index == build.scratchBuffer.index &&
                previous.buffer.generation == build.scratchBuffer.generation &&
                rangesOverlap(previous.offset, previous.size, build.scratchOffset, scratchSize)) {
                return false;
            }
        }
        const bool overlapsAccelerationStructure =
            std::any_of(m_device->m_data->accelerationStructures.begin(),
                        m_device->m_data->accelerationStructures.end(), [&](const auto& entry) {
                            const RhiAccelerationStructureDesc& accelerationStructure = entry.second.desc;
                            return accelerationStructure.buffer.index == build.scratchBuffer.index &&
                                   accelerationStructure.buffer.generation == build.scratchBuffer.generation &&
                                   rangesOverlap(accelerationStructure.offset, accelerationStructure.size,
                                                 build.scratchOffset, scratchSize);
                        });
        const bool overlapsMicromap =
            std::any_of(m_device->m_data->micromaps.begin(), m_device->m_data->micromaps.end(), [&](const auto& entry) {
                const RhiMicromapDesc& micromap = entry.second.desc;
                return micromap.buffer.index == build.scratchBuffer.index &&
                       micromap.buffer.generation == build.scratchBuffer.generation &&
                       rangesOverlap(micromap.offset, micromap.size, build.scratchOffset, scratchSize);
            });
        if (overlapsAccelerationStructure || overlapsMicromap) {
            return false;
        }
        scratchRanges.push_back({build.scratchBuffer, build.scratchOffset, scratchSize});

        VkAccelerationStructureBuildGeometryInfoKHR& nativeBuild = nativeBuilds[buildIndex];
        nativeBuild = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        nativeBuild.type = sizeQuery.type;
        nativeBuild.flags = sizeQuery.flags;
        nativeBuild.mode =
            update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        nativeBuild.srcAccelerationStructure = source != nullptr ? source->accelerationStructure : VK_NULL_HANDLE;
        nativeBuild.dstAccelerationStructure = destination->accelerationStructure;
        nativeBuild.geometryCount = build.input.geometryCount;
        nativeBuild.pGeometries = nativeInputs[buildIndex].geometries.data();
        nativeBuild.scratchData.deviceAddress = scratchAddress + build.scratchOffset;
        nativeRangePointers[buildIndex] = nativeInputs[buildIndex].ranges.data();
        stagedReferences.reference(build.destination);
        stagedReferences.reference(build.source);
        stagedReferences.reference(build.scratchBuffer);
    }

    const auto mergeHandles = [](const auto& handles, auto& references) {
        for (const auto handle : handles) {
            references.reference(handle);
        }
    };
    mergeHandles(stagedReferences.buffers, m_data->resourceReferences);
    mergeHandles(stagedReferences.accelerationStructures, m_data->resourceReferences);
    m_device->m_data->cmdBuildAccelerationStructures(m_data->commandBuffer, buildCount, nativeBuilds.data(),
                                                     nativeRangePointers.data());
    return true;
}

bool VkRhiCommandList::buildMicromaps(const RhiMicromapBuildDesc* builds, const uint32_t buildCount) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        m_data->type == RhiCommandListType::Transfer || builds == nullptr || buildCount == 0u ||
        m_device->m_data->cmdBuildMicromaps == nullptr) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    std::vector<NativeMicromapBuildInput> nativeInputs(buildCount);
    std::vector<uint64_t> destinationKeys;
    struct ScratchRange {
        RhiBufferHandle buffer;
        uint64_t offset = 0u;
        uint64_t size = 0u;
    };
    std::vector<ScratchRange> scratchRanges;
    destinationKeys.reserve(buildCount);
    scratchRanges.reserve(buildCount);
    VkRhiCommandResourceReferences stagedReferences;

    for (uint32_t buildIndex = 0u; buildIndex < buildCount; ++buildIndex) {
        const RhiMicromapBuildDesc& build = builds[buildIndex];
        if (!fillNativeMicromapBuildInput(*m_device->m_data, m_device->m_capabilities, build.input,
                                          nativeInputs[buildIndex], &stagedReferences)) {
            return false;
        }
        const auto* destination = findRecord(m_device->m_data->micromaps, build.destination);
        const auto* scratch = findRecord(m_device->m_data->buffers, build.scratchBuffer);
        constexpr RhiBufferUsageFlags kScratchUsages =
            rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
        if (destination == nullptr || scratch == nullptr || !bufferHasUsages(*scratch, kScratchUsages) ||
            build.scratchOffset % 256u != 0u) {
            return false;
        }
        const uint64_t destinationKey = handleKey(build.destination);
        if (std::find(destinationKeys.begin(), destinationKeys.end(), destinationKey) != destinationKeys.end()) {
            return false;
        }
        destinationKeys.push_back(destinationKey);

        VkMicromapBuildSizesInfoEXT nativeSizes{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
        m_device->m_data->getMicromapBuildSizes(m_device->m_data->device,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &nativeInputs[buildIndex].info, &nativeSizes);
        const VkDeviceAddress scratchAddress = nativeBufferDeviceAddress(*m_device->m_data, *scratch);
        if (nativeSizes.micromapSize == 0u || nativeSizes.buildScratchSize == 0u ||
            destination->desc.size < nativeSizes.micromapSize ||
            !rangeFits(scratch->desc.size, build.scratchOffset, nativeSizes.buildScratchSize) || scratchAddress == 0u ||
            (scratchAddress + build.scratchOffset) % 256u != 0u) {
            return false;
        }
        for (const ScratchRange& previous : scratchRanges) {
            if (previous.buffer.index == build.scratchBuffer.index &&
                previous.buffer.generation == build.scratchBuffer.generation &&
                rangesOverlap(previous.offset, previous.size, build.scratchOffset, nativeSizes.buildScratchSize)) {
                return false;
            }
        }
        const bool overlapsMicromap =
            std::any_of(m_device->m_data->micromaps.begin(), m_device->m_data->micromaps.end(), [&](const auto& entry) {
                const RhiMicromapDesc& micromap = entry.second.desc;
                return micromap.buffer.index == build.scratchBuffer.index &&
                       micromap.buffer.generation == build.scratchBuffer.generation &&
                       rangesOverlap(micromap.offset, micromap.size, build.scratchOffset, nativeSizes.buildScratchSize);
            });
        const bool overlapsAccelerationStructure =
            std::any_of(m_device->m_data->accelerationStructures.begin(),
                        m_device->m_data->accelerationStructures.end(), [&](const auto& entry) {
                            const RhiAccelerationStructureDesc& accelerationStructure = entry.second.desc;
                            return accelerationStructure.buffer.index == build.scratchBuffer.index &&
                                   accelerationStructure.buffer.generation == build.scratchBuffer.generation &&
                                   rangesOverlap(accelerationStructure.offset, accelerationStructure.size,
                                                 build.scratchOffset, nativeSizes.buildScratchSize);
                        });
        if (overlapsMicromap || overlapsAccelerationStructure) {
            return false;
        }
        scratchRanges.push_back({build.scratchBuffer, build.scratchOffset, nativeSizes.buildScratchSize});
        nativeInputs[buildIndex].info.dstMicromap = destination->micromap;
        nativeInputs[buildIndex].info.scratchData.deviceAddress = scratchAddress + build.scratchOffset;
        stagedReferences.reference(build.destination);
        stagedReferences.reference(build.scratchBuffer);
    }

    const auto mergeHandles = [](const auto& handles, auto& references) {
        for (const auto handle : handles) {
            references.reference(handle);
        }
    };
    mergeHandles(stagedReferences.buffers, m_data->resourceReferences);
    mergeHandles(stagedReferences.micromaps, m_data->resourceReferences);
    std::vector<VkMicromapBuildInfoEXT> nativeBuilds(buildCount);
    for (uint32_t index = 0u; index < buildCount; ++index) {
        nativeBuilds[index] = nativeInputs[index].info;
    }
    m_device->m_data->cmdBuildMicromaps(m_data->commandBuffer, buildCount, nativeBuilds.data());
    return true;
}

bool VkRhiCommandList::copyAccelerationStructure(const RhiAccelerationStructureCopyDesc& copy) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        m_data->type == RhiCommandListType::Transfer ||
        toVkAccelerationStructureCopyMode(copy.mode) == VK_COPY_ACCELERATION_STRUCTURE_MODE_MAX_ENUM_KHR ||
        (copy.source.index == copy.destination.index && copy.source.generation == copy.destination.generation)) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* source = findRecord(m_device->m_data->accelerationStructures, copy.source);
    const auto* destination = findRecord(m_device->m_data->accelerationStructures, copy.destination);
    if (source == nullptr || destination == nullptr || source->desc.type != destination->desc.type ||
        (copy.mode == RhiAccelerationStructureCopyMode::Clone && destination->desc.size < source->desc.size)) {
        return false;
    }
    VkCopyAccelerationStructureInfoKHR info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    info.src = source->accelerationStructure;
    info.dst = destination->accelerationStructure;
    info.mode = toVkAccelerationStructureCopyMode(copy.mode);
    m_device->m_data->cmdCopyAccelerationStructure(m_data->commandBuffer, &info);
    m_data->resourceReferences.reference(copy.source);
    m_data->resourceReferences.reference(copy.destination);
    return true;
}

bool VkRhiCommandList::writeAccelerationStructureProperties(const RhiAccelerationStructurePropertyQueryDesc& query) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        m_data->type == RhiCommandListType::Transfer || query.accelerationStructures == nullptr ||
        query.accelerationStructureCount == 0u) {
        return false;
    }
    const std::shared_lock<std::shared_mutex> registryLock(m_device->m_data->resourceRegistryMutex);
    const auto* pool = findRecord(m_device->m_data->queryPools, query.queryPool);
    if (pool == nullptr || pool->type != RhiQueryType::AccelerationStructureCompactedSize ||
        query.firstQuery > pool->count || query.accelerationStructureCount > pool->count - query.firstQuery) {
        return false;
    }
    std::vector<VkAccelerationStructureKHR> nativeAccelerationStructures;
    std::vector<uint64_t> keys;
    nativeAccelerationStructures.reserve(query.accelerationStructureCount);
    keys.reserve(query.accelerationStructureCount);
    for (uint32_t index = 0u; index < query.accelerationStructureCount; ++index) {
        const RhiAccelerationStructureHandle handle = query.accelerationStructures[index];
        const auto* accelerationStructure = findRecord(m_device->m_data->accelerationStructures, handle);
        const uint64_t key = handleKey(handle);
        if (accelerationStructure == nullptr || std::find(keys.begin(), keys.end(), key) != keys.end()) {
            return false;
        }
        keys.push_back(key);
        nativeAccelerationStructures.push_back(accelerationStructure->accelerationStructure);
    }
    m_device->m_data->cmdWriteAccelerationStructuresProperties(
        m_data->commandBuffer, query.accelerationStructureCount, nativeAccelerationStructures.data(),
        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, pool->pool, query.firstQuery);
    for (uint32_t index = 0u; index < query.accelerationStructureCount; ++index) {
        m_data->resourceReferences.reference(query.accelerationStructures[index]);
    }
    m_data->resourceReferences.reference(query.queryPool);
    return true;
}
