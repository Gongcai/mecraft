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
        return graphics != UINT32_MAX && compute != UINT32_MAX &&
               transfer != UINT32_MAX && present != UINT32_MAX &&
               (!requireOpticalFlow || opticalFlow != UINT32_MAX);
    }
};

namespace {

std::atomic<uint64_t> g_nextVkRhiDeviceId{1u};

template <typename Handle>
[[nodiscard]] uint64_t handleKey(const Handle handle) {
    return (static_cast<uint64_t>(handle.generation) << 32u) | handle.index;
}

void logVkError(const char* operation, const VkResult result) {
    std::cerr << "VkRhiDevice: " << operation << " failed with VkResult "
              << static_cast<int32_t>(result) << '\n';
}

[[nodiscard]] bool vkSucceeded(const VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    logVkError(operation, result);
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Vulkan validation: "
                  << (data != nullptr && data->pMessage != nullptr
                          ? data->pMessage
                          : "")
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
    }
    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

[[nodiscard]] VkImageUsageFlags toVkImageUsage(const RhiTextureUsageFlags usage) {
    VkImageUsageFlags result = 0u;
    if ((usage & rhiFlag(RhiTextureUsage::Sampled)) != 0u) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::Storage)) != 0u) result |= VK_IMAGE_USAGE_STORAGE_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::ColorAttachment)) != 0u) {
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((usage & rhiFlag(RhiTextureUsage::DepthStencilAttachment)) != 0u) {
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if ((usage & rhiFlag(RhiTextureUsage::TransferSrc)) != 0u) result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((usage & rhiFlag(RhiTextureUsage::TransferDst)) != 0u) result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
}

[[nodiscard]] VkBufferUsageFlags toVkBufferUsage(const RhiBufferUsageFlags usage) {
    VkBufferUsageFlags result = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Vertex)) != 0u) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Index)) != 0u) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Uniform)) != 0u) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Storage)) != 0u) result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::Indirect)) != 0u) result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::TransferSrc)) != 0u) result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if ((usage & rhiFlag(RhiBufferUsage::TransferDst)) != 0u) result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
    return face == RhiFrontFace::CounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE
        : VK_FRONT_FACE_CLOCKWISE;
}

[[nodiscard]] VkFilter toVkFilter(const RhiFilter filter) {
    return filter == RhiFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerMipmapMode toVkMipmapMode(const RhiMipmapMode mode) {
    return mode == RhiMipmapMode::Nearest
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST
        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
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
        case RhiTextureFormat::R8Unorm: return 1u;
        case RhiTextureFormat::Rg8Unorm:
        case RhiTextureFormat::R16Float:
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
        case RhiTextureFormat::Rgba16Float: return 8u;
        case RhiTextureFormat::Rgba32Float: return 16u;
        case RhiTextureFormat::Undefined: break;
    }
    return 0u;
}

[[nodiscard]] bool supportsFormatFeatures(const VkPhysicalDevice physicalDevice,
                                          const VkFormat format,
                                          const VkFormatFeatureFlags2 requiredFeatures) {
    VkFormatProperties3 properties3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 properties2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &properties3};
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &properties2);
    return (properties3.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}

[[nodiscard]] QueueFamilies queryQueueFamilies(const VkPhysicalDevice physicalDevice,
                                                const VkSurfaceKHR surface,
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
        if (families.transfer == UINT32_MAX &&
            (flags & VK_QUEUE_TRANSFER_BIT) != 0u &&
            (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                      VK_QUEUE_OPTICAL_FLOW_BIT_NV)) == 0u) {
            families.transfer = i;
        }
#if defined(VK_NV_optical_flow)
        if (requireOpticalFlow && families.opticalFlow == UINT32_MAX &&
            (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) != 0u &&
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
        if (families.compute == UINT32_MAX) families.compute = families.graphics;
        if (families.transfer == UINT32_MAX) families.transfer = families.graphics;
    }
    return families;
}

[[nodiscard]] std::vector<VkQueueFamilyProperties> queryQueueFamilyProperties(
    const VkPhysicalDevice physicalDevice) {
    uint32_t count = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, properties.data());
    return properties;
}

[[nodiscard]] uint32_t requiredQueuesForFamily(
    const QueueFamilies& families,
    const VulkanRequirementCollector& requirements,
    const uint32_t family) {
    uint32_t count = 0u;
    if (family == families.graphics || family == families.compute ||
        family == families.transfer || family == families.present) {
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

[[nodiscard]] bool queueRequirementsSupported(
    const VkPhysicalDevice physicalDevice,
    const QueueFamilies& families,
    const VulkanRequirementCollector& requirements) {
    const std::vector<VkQueueFamilyProperties> properties =
        queryQueueFamilyProperties(physicalDevice);
    const std::array<uint32_t, 5u> requestedFamilies{
        families.graphics, families.compute, families.transfer,
        families.present, families.opticalFlow
    };
    for (const uint32_t family : requestedFamilies) {
        if (family == UINT32_MAX) {
            continue;
        }
        if (family >= properties.size() ||
            requiredQueuesForFamily(families, requirements, family) >
                properties[family].queueCount) {
            return false;
        }
    }
    return true;
}

#if defined(MECRAFT_ENABLE_STREAMLINE)
[[nodiscard]] bool supportsStreamlineVulkan12Features(
    const VkPhysicalDeviceVulkan12Features& supported,
    const std::vector<const char*>& requiredNames) {
    for (const char* name : requiredNames) {
        if (std::strcmp(name, "timelineSemaphore") == 0) {
            if (supported.timelineSemaphore != VK_TRUE) return false;
        } else if (std::strcmp(name, "descriptorIndexing") == 0) {
            if (supported.descriptorIndexing != VK_TRUE) return false;
        } else if (std::strcmp(name, "bufferDeviceAddress") == 0) {
            if (supported.bufferDeviceAddress != VK_TRUE) return false;
        } else {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool supportsStreamlineVulkan13Features(
    const VkPhysicalDeviceVulkan13Features& supported,
    const std::vector<const char*>& requiredNames) {
    for (const char* name : requiredNames) {
        if (std::strcmp(name, "synchronization2") == 0) {
            if (supported.synchronization2 != VK_TRUE) return false;
        } else if (std::strcmp(name, "privateData") == 0) {
            if (supported.privateData != VK_TRUE) return false;
        } else {
            return false;
        }
    }
    return true;
}
#endif

} // namespace

#include "renderer/rhi/vulkan/VkRhiInternal.h"

std::optional<VkRhiDeviceInteropInfo> VkRhiInterop::deviceInfo(
    const VkRhiDevice& device) {
    if (!device.m_initialized || device.m_data == nullptr) {
        return std::nullopt;
    }
    const VkRhiDeviceData& data = *device.m_data;
    return VkRhiDeviceInteropInfo{
        data.instance, data.physicalDevice, data.device,
        data.graphicsQueue, data.computeQueue, data.transferQueue, data.presentQueue,
        data.queueFamilies.graphics, data.queueFamilies.compute,
        data.queueFamilies.transfer, data.queueFamilies.present
    };
}

std::optional<VkRhiTextureInteropInfo> VkRhiInterop::textureInfo(
    const VkRhiDevice& device,
    const RhiTextureHandle texture,
    const RhiTextureViewHandle view) {
    if (!device.m_initialized || device.m_data == nullptr ||
        !texture.isValid() || !view.isValid()) {
        return std::nullopt;
    }
    const auto* textureRecord = findRecord(device.m_data->textures, texture);
    const auto* viewRecord = findRecord(device.m_data->textureViews, view);
    if (textureRecord == nullptr || viewRecord == nullptr ||
        viewRecord->desc.texture.index != texture.index ||
        viewRecord->desc.texture.generation != texture.generation) {
        return std::nullopt;
    }
    const RhiTextureFormat viewFormat =
        viewRecord->desc.format == RhiTextureFormat::Undefined
            ? textureRecord->desc.format : viewRecord->desc.format;
    const uint32_t mipCount = viewRecord->desc.mipCount == kRhiRemainingMipLevels
        ? textureRecord->desc.mipLevels - viewRecord->desc.baseMip
        : viewRecord->desc.mipCount;
    const uint32_t textureLayers = textureRecord->desc.dimension ==
            RhiTextureDimension::Texture3D
        ? 1u : textureRecord->desc.depthOrLayers;
    const bool texture3D = textureRecord->desc.dimension ==
        RhiTextureDimension::Texture3D;
    const uint32_t layerCount = texture3D
        ? 1u
        : (viewRecord->desc.layerCount == kRhiRemainingArrayLayers
            ? textureLayers - viewRecord->desc.baseLayer
            : viewRecord->desc.layerCount);
    const uint32_t viewDepth = textureRecord->desc.dimension ==
            RhiTextureDimension::Texture3D
        ? std::max(1u, textureRecord->desc.depthOrLayers >> viewRecord->desc.baseMip)
        : 1u;
    return VkRhiTextureInteropInfo{
        textureRecord->image,
        viewRecord->view,
        toVkFormat(viewFormat),
        {std::max(1u, textureRecord->desc.width >> viewRecord->desc.baseMip),
         std::max(1u, textureRecord->desc.height >> viewRecord->desc.baseMip),
         viewDepth},
        toVkImageUsage(textureRecord->desc.usage),
        toVkImageType(textureRecord->desc.dimension),
        toVkImageViewType(viewRecord->desc.viewType),
        textureRecord->desc.mipLevels,
        textureLayers,
        viewRecord->desc.baseMip,
        mipCount,
        texture3D ? 0u : viewRecord->desc.baseLayer,
        layerCount,
        defaultAspectForFormat(viewFormat)
    };
}

std::optional<VkCommandBuffer> VkRhiInterop::commandBuffer(
    const VkRhiDevice& device,
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

namespace {

template <typename DeferredQueue, typename DeferredItem>
void enqueueDeferred(DeferredQueue& queue, DeferredItem item) {
    const auto insertion = std::upper_bound(
        queue.begin(), queue.end(), item.sequence,
        [](const uint64_t sequence, const auto& queued) {
            return sequence < queued.sequence;
        });
    queue.insert(insertion, std::move(item));
}

[[nodiscard]] bool resolveResourceReferences(
    VkRhiDeviceData& data, VkRhiCommandResourceReferences& references) {
    for (size_t index = 0u; index < references.pipelines.size(); ++index) {
        const auto* pipeline = findRecord(data.pipelines, references.pipelines[index]);
        if (pipeline == nullptr) return false;
        references.reference(pipeline->layoutHandle);
    }
    for (size_t index = 0u; index < references.pipelineLayouts.size(); ++index) {
        const auto* layout = findRecord(data.pipelineLayouts, references.pipelineLayouts[index]);
        if (layout == nullptr) return false;
        for (const RhiBindGroupLayoutHandle bindGroupLayout : layout->desc.bindGroupLayouts) {
            references.reference(bindGroupLayout);
        }
    }
    for (size_t index = 0u; index < references.bindGroups.size(); ++index) {
        const auto* bindGroup = findRecord(data.bindGroups, references.bindGroups[index]);
        if (bindGroup == nullptr) return false;
        references.reference(bindGroup->layoutHandle);
        for (const RhiBindGroupEntry& entry : bindGroup->desc.entries) {
            references.reference(entry.resource.buffer.buffer);
            references.reference(entry.resource.textureView);
            references.reference(entry.resource.sampler);
            references.reference(entry.resource.combinedTextureSampler.textureView);
            references.reference(entry.resource.combinedTextureSampler.sampler);
        }
    }
    for (size_t index = 0u; index < references.textureViews.size(); ++index) {
        const auto* view = findRecord(data.textureViews, references.textureViews[index]);
        if (view == nullptr) return false;
        references.reference(view->desc.texture);
    }
    const auto allExist = [](const auto& handles, const auto& records) {
        return std::all_of(handles.begin(), handles.end(), [&records](const auto handle) {
            return findRecord(records, handle) != nullptr;
        });
    };
    return allExist(references.buffers, data.buffers) &&
           allExist(references.textures, data.textures) &&
           allExist(references.textureViews, data.textureViews) &&
           allExist(references.samplers, data.samplers) &&
           allExist(references.bindGroupLayouts, data.bindGroupLayouts) &&
           allExist(references.pipelineLayouts, data.pipelineLayouts) &&
           allExist(references.pipelines, data.pipelines) &&
           allExist(references.bindGroups, data.bindGroups) &&
           allExist(references.queryPools, data.queryPools);
}

[[nodiscard]] VkRect2D toVkClippedScissor(const RhiRect2D& rect,
                                          const uint32_t targetWidth,
                                          const uint32_t targetHeight) {
    const int64_t minX = std::clamp<int64_t>(rect.x, 0, targetWidth);
    const int64_t minY = std::clamp<int64_t>(rect.y, 0, targetHeight);
    const int64_t maxX = std::clamp<int64_t>(
        static_cast<int64_t>(rect.x) + rect.width, minX, targetWidth);
    const int64_t maxY = std::clamp<int64_t>(
        static_cast<int64_t>(rect.y) + rect.height, minY, targetHeight);
    return {
        {static_cast<int32_t>(minX),
         static_cast<int32_t>(targetHeight - static_cast<uint32_t>(maxY))},
        {static_cast<uint32_t>(maxX - minX), static_cast<uint32_t>(maxY - minY)}
    };
}

void markResourceReferencesUsed(VkRhiDeviceData& data,
                                const VkRhiCommandResourceReferences& references,
                                const uint64_t sequence) {
    const auto markAll = [sequence](const auto& handles, auto& records) {
        for (const auto handle : handles) {
            auto* record = findRecord(records, handle);
            if (record != nullptr) record->lifetime.markUsed(sequence);
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
}

void destroyDeferredObject(VkRhiDeviceData& data,
                           const VkRhiDeviceData::DeferredObject& item) {
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
            vkDestroyDescriptorSetLayout(
                data.device, reinterpret_cast<VkDescriptorSetLayout>(item.object), nullptr);
            break;
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
            vkDestroyPipelineLayout(
                data.device, reinterpret_cast<VkPipelineLayout>(item.object), nullptr);
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
        default:
            break;
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
    if (data.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(data.device, data.swapchain, nullptr);
        data.swapchain = VK_NULL_HANDLE;
    }
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
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
    const bool submitted = vkQueueSubmit2(data.graphicsQueue, 1u, &submitInfo,
                                           VK_NULL_HANDLE) == VK_SUCCESS &&
                           vkQueueWaitIdle(data.graphicsQueue) == VK_SUCCESS;
    vkDestroyCommandPool(data.device, pool, nullptr);
    return submitted;
}

[[nodiscard]] bool createSwapchain(VkRhiDeviceData& data) {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                         data.physicalDevice, data.surface, &capabilities),
                     "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return false;
    }

    uint32_t formatCount = 0u;
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceFormatsKHR(
                         data.physicalDevice, data.surface, &formatCount, nullptr),
                     "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
        formatCount == 0u) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceFormatsKHR(
                         data.physicalDevice, data.surface, &formatCount, formats.data()),
                     "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        return false;
    }
    formats.resize(formatCount);
    const auto formatIt = std::find_if(formats.begin(), formats.end(), [](const auto& format) {
        return format.format == VK_FORMAT_B8G8R8A8_UNORM &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (formatIt == formats.end()) {
        std::cerr << "VkRhiDevice: the surface does not expose the required SDR presentation format\n";
        return false;
    }

    uint32_t presentModeCount = 0u;
    if (!vkSucceeded(vkGetPhysicalDeviceSurfacePresentModesKHR(
                         data.physicalDevice, data.surface, &presentModeCount, nullptr),
                     "vkGetPhysicalDeviceSurfacePresentModesKHR") ||
        presentModeCount == 0u) {
        return false;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (!vkSucceeded(vkGetPhysicalDeviceSurfacePresentModesKHR(
                         data.physicalDevice, data.surface, &presentModeCount,
                         presentModes.data()),
                     "vkGetPhysicalDeviceSurfacePresentModesKHR")) {
        return false;
    }
    presentModes.resize(presentModeCount);
    data.immediatePresentSupported =
        std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) !=
        presentModes.end();
    const VkPresentModeKHR requestedPresentMode = data.vsyncEnabled
        ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (std::find(presentModes.begin(), presentModes.end(), requestedPresentMode) ==
        presentModes.end()) {
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
        extent.width = std::clamp(static_cast<uint32_t>(framebufferWidth),
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(framebufferHeight),
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1u;
    if (capabilities.maxImageCount != 0u) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    const std::array<uint32_t, 2u> families{
        data.queueFamilies.graphics, data.queueFamilies.present};
    const VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
    if (!vkSucceeded(vkCreateSwapchainKHR(data.device, &swapchainInfo, nullptr,
                                         &data.swapchain),
                     "vkCreateSwapchainKHR")) {
        return false;
    }
    data.swapchainFormat = formatIt->format;
    data.swapchainExtent = extent;
    data.presentMode = requestedPresentMode;

    uint32_t actualImageCount = 0u;
    if (!vkSucceeded(vkGetSwapchainImagesKHR(
                         data.device, data.swapchain, &actualImageCount, nullptr),
                     "vkGetSwapchainImagesKHR") ||
        actualImageCount == 0u) {
        destroySwapchainResources(data);
        return false;
    }
    std::vector<VkImage> images(actualImageCount);
    if (!vkSucceeded(vkGetSwapchainImagesKHR(
                         data.device, data.swapchain, &actualImageCount, images.data()),
                     "vkGetSwapchainImagesKHR") ||
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
        if (!vkSucceeded(vkCreateSemaphore(data.device, &semaphoreInfo, nullptr,
                                           &presentReady),
                         "vkCreateSemaphore(present ready)")) {
            destroySwapchainResources(data);
            return false;
        }
        data.presentReadySemaphores.push_back(presentReady);
        const RhiTextureHandle textureHandle = data.textureHandles.allocate();
        RhiTextureDesc textureDesc{};
        textureDesc.debugName = "Vulkan swapchain image";
        textureDesc.format = RhiTextureFormat::Bgra8Unorm;
        textureDesc.width = extent.width;
        textureDesc.height = extent.height;
        textureDesc.usage = rhiFlag(RhiTextureUsage::ColorAttachment) |
                            rhiFlag(RhiTextureUsage::TransferSrc) |
                            rhiFlag(RhiTextureUsage::TransferDst) |
                            rhiFlag(RhiTextureUsage::Present);
        data.textures.emplace(handleKey(textureHandle),
                              VkRhiDeviceData::Texture{images[i], VK_NULL_HANDLE,
                                                       textureDesc, true, true});
        data.swapchainTextures.push_back(textureHandle);
        data.swapchainImageInitialized.push_back(false);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = data.swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        VkImageView view = VK_NULL_HANDLE;
        if (!vkSucceeded(vkCreateImageView(data.device, &viewInfo, nullptr, &view),
                         "vkCreateImageView(swapchain)")) {
            destroySwapchainResources(data);
            return false;
        }
        const RhiTextureViewHandle viewHandle = data.textureViewHandles.allocate();
        RhiTextureViewDesc rhiViewDesc{};
        rhiViewDesc.texture = textureHandle;
        rhiViewDesc.format = RhiTextureFormat::Bgra8Unorm;
        data.textureViews.emplace(handleKey(viewHandle),
                                  VkRhiDeviceData::TextureView{view, rhiViewDesc});
        data.swapchainViews.push_back(viewHandle);

        VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depthInfo.imageType = VK_IMAGE_TYPE_2D;
        depthInfo.format = VK_FORMAT_D32_SFLOAT;
        depthInfo.extent = {extent.width, extent.height, 1u};
        depthInfo.mipLevels = 1u;
        depthInfo.arrayLayers = 1u;
        depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        if (!vkSucceeded(vmaCreateImage(data.allocator, &depthInfo, &allocationInfo,
                                        &depthImage, &depthAllocation, nullptr),
                         "vmaCreateImage(swapchain depth)")) {
            destroySwapchainResources(data);
            return false;
        }
        const RhiTextureHandle depthHandle = data.textureHandles.allocate();
        RhiTextureDesc depthDesc{};
        depthDesc.debugName = "Vulkan swapchain depth";
        depthDesc.format = RhiTextureFormat::Depth32Float;
        depthDesc.width = extent.width;
        depthDesc.height = extent.height;
        depthDesc.usage = rhiFlag(RhiTextureUsage::DepthStencilAttachment) |
                          rhiFlag(RhiTextureUsage::Sampled);
        data.textures.emplace(handleKey(depthHandle),
                              VkRhiDeviceData::Texture{depthImage, depthAllocation,
                                                       depthDesc, false, true});
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
    if (!vkSucceeded(vkDeviceWaitIdle(data.device), "vkDeviceWaitIdle(swapchain)")) {
        return false;
    }
    destroySwapchainResources(data);
    return createSwapchain(data);
}

[[nodiscard]] bool recreateSurfaceAndSwapchain(VkRhiDeviceData& data) {
    if (!vkSucceeded(vkDeviceWaitIdle(data.device), "vkDeviceWaitIdle(surface)")) {
        return false;
    }
    destroySwapchainResources(data);
    if (data.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(data.instance, data.surface, nullptr);
        data.surface = VK_NULL_HANDLE;
    }
    if (!vkSucceeded(glfwCreateWindowSurface(data.instance, data.window, nullptr, &data.surface),
                     "glfwCreateWindowSurface")) {
        return false;
    }
    VkBool32 presentSupported = VK_FALSE;
    if (!vkSucceeded(vkGetPhysicalDeviceSurfaceSupportKHR(
                         data.physicalDevice, data.queueFamilies.present,
                         data.surface, &presentSupported),
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

[[nodiscard]] bool uploadTextureInitialData(VkRhiDeviceData& data, VkImage image,
                                            const RhiTextureDesc& desc,
                                            const RhiTextureInitialData& initialData) {
    if (initialData.finalState == RhiResourceState::Undefined ||
        initialData.mipLevel >= desc.mipLevels || initialData.layerCount == 0u) return false;
    const bool texture3D = desc.dimension == RhiTextureDimension::Texture3D;
    const uint32_t arrayLayers = texture3D ? 1u : desc.depthOrLayers;
    const uint32_t mipDepth = texture3D
        ? std::max(desc.depthOrLayers >> initialData.mipLevel, 1u) : 1u;
    if (texture3D) {
        if (initialData.arrayLayer >= mipDepth ||
            initialData.layerCount > mipDepth - initialData.arrayLayer) {
            return false;
        }
    } else if (initialData.arrayLayer >= arrayLayers ||
               initialData.layerCount > arrayLayers - initialData.arrayLayer) {
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
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(vmaCreateBuffer(data.allocator, &stagingInfo, &allocationInfo, &staging,
                                     &allocation, &nativeAllocationInfo),
                     "vmaCreateBuffer(texture upload)")) return false;
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
    if (!vkSucceeded(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                     "vkBeginCommandBuffer(texture upload)")) {
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
    vkCmdCopyBufferToImage(commandBuffer, staging, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
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
    if (!vkSucceeded(vkEndCommandBuffer(commandBuffer),
                     "vkEndCommandBuffer(texture upload)")) {
        vkDestroyCommandPool(data.device, pool, nullptr);
        vmaDestroyBuffer(data.allocator, staging, allocation);
        return false;
    }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = 1u;
    submitInfo.pCommandBufferInfos = &commandInfo;
    const VkResult submitResult = vkQueueSubmit2(data.graphicsQueue, 1u, &submitInfo,
                                                  VK_NULL_HANDLE);
    const VkResult waitResult = submitResult == VK_SUCCESS
        ? vkQueueWaitIdle(data.graphicsQueue) : submitResult;
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
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount,
                                           availableInstanceExtensions.data());
    std::string missingExtension;
    if (!requirements.validateInstanceExtensions(availableInstanceExtensions, missingExtension)) {
        std::cerr << "VkRhiDevice: required Vulkan instance extension is unavailable: "
                  << missingExtension << '\n';
        shutdown();
        return false;
    }
    const std::vector<const char*> instanceExtensions =
        requirements.instanceExtensionNames();
    std::vector<const char*> layers;
    if (desc.enableDebugOutput) {
        uint32_t layerCount = 0u;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        const bool validationAvailable = std::any_of(
            availableLayers.begin(), availableLayers.end(), [](const auto& layer) {
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
    if (!vkSucceeded(vkCreateInstance(&instanceInfo, nullptr, &m_data->instance),
                     "vkCreateInstance")) {
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
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;
        if (!vkSucceeded(createMessenger(m_data->instance, &debugInfo, nullptr,
                                         &m_data->debugMessenger),
                         "vkCreateDebugUtilsMessengerEXT")) {
            shutdown();
            return false;
        }
    }
    if (!vkSucceeded(glfwCreateWindowSurface(m_data->instance, m_data->window, nullptr,
                                             &m_data->surface),
                     "glfwCreateWindowSurface")) {
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
    const bool requireOpticalFlow = requirements.opticalFlowQueueCount() > 0u;
    const std::vector<const char*> requiredFeatures12 =
        requirements.vulkan12FeatureNames();
    const std::vector<const char*> requiredFeatures13 =
        requirements.vulkan13FeatureNames();
    for (const VkPhysicalDevice candidate : physicalDevices) {
        uint32_t extensionCount = 0u;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
        if (!requirements.validateDeviceExtensions(extensions, missingExtension)) {
            continue;
        }
        const QueueFamilies families = queryQueueFamilies(candidate, m_data->surface,
                                                          requireOpticalFlow);
        if (!families.complete(requireOpticalFlow) ||
            !queueRequirementsSupported(candidate, families, requirements)) {
            continue;
        }
        VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
        VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
            requireOpticalFlow ? &opticalFlow : nullptr};
        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &depthClip};
        VkPhysicalDeviceVulkan12Features features12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13};
        VkPhysicalDeviceVulkan11Features features11{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12};
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                            &features11};
        vkGetPhysicalDeviceFeatures2(candidate, &features2);
#if defined(MECRAFT_ENABLE_FSR31)
        if (features2.features.shaderInt16 != VK_TRUE) {
            continue;
        }
#endif
        if (features2.features.samplerAnisotropy != VK_TRUE ||
            features11.shaderDrawParameters != VK_TRUE ||
            features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE ||
            features12.timelineSemaphore != VK_TRUE ||
            features12.bufferDeviceAddress != VK_TRUE ||
            depthClip.depthClipControl != VK_TRUE) {
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
        }
    }
    if (m_data->physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "VkRhiDevice: no device satisfies the Vulkan 1.3 RHI feature contract\n";
        shutdown();
        return false;
    }

    std::set<uint32_t> uniqueFamilies{
        m_data->queueFamilies.graphics, m_data->queueFamilies.compute,
        m_data->queueFamilies.transfer, m_data->queueFamilies.present};
    if (m_data->queueFamilies.opticalFlow != UINT32_MAX) {
        uniqueFamilies.insert(m_data->queueFamilies.opticalFlow);
    }
    uint32_t maxQueueCount = 1u;
    for (const uint32_t family : uniqueFamilies) {
        maxQueueCount = std::max(
            maxQueueCount,
            requiredQueuesForFamily(m_data->queueFamilies, requirements, family));
    }
    const std::vector<float> queuePriorities(maxQueueCount, 1.0f);
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (const uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = requiredQueuesForFamily(
            m_data->queueFamilies, requirements, family);
        queueInfo.pQueuePriorities = queuePriorities.data();
        queueInfos.push_back(queueInfo);
    }
    m_data->streamlineComputeQueueIndex = 1u;
    m_data->streamlineGraphicsQueueIndex =
        m_data->queueFamilies.graphics == m_data->queueFamilies.compute
            ? 1u + requirements.additionalQueueCount(RhiQueueType::Compute)
            : 1u;
    m_data->streamlineOpticalFlowQueueIndex = 0u;
    VkPhysicalDeviceOpticalFlowFeaturesNV opticalFlow{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV};
    opticalFlow.opticalFlow = requireOpticalFlow ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
        requireOpticalFlow ? &opticalFlow : nullptr};
    depthClip.depthClipControl = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &depthClip};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.privateData = std::any_of(
        requiredFeatures13.begin(), requiredFeatures13.end(), [](const char* name) {
            return std::strcmp(name, "privateData") == 0;
        }) ? VK_TRUE : VK_FALSE;
    features13.subgroupSizeControl = selected13.subgroupSizeControl;
    features13.computeFullSubgroups = selected13.computeFullSubgroups;
    VkPhysicalDeviceVulkan12Features features12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13};
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.shaderFloat16 = selected12.shaderFloat16;
    features12.shaderInt8 = selected12.shaderInt8;
    features12.descriptorIndexing = selected12.descriptorIndexing;
    features12.shaderSampledImageArrayNonUniformIndexing =
        selected12.shaderSampledImageArrayNonUniformIndexing;
    features12.shaderStorageImageArrayNonUniformIndexing =
        selected12.shaderStorageImageArrayNonUniformIndexing;
    features12.descriptorBindingSampledImageUpdateAfterBind =
        selected12.descriptorBindingSampledImageUpdateAfterBind;
    features12.descriptorBindingStorageImageUpdateAfterBind =
        selected12.descriptorBindingStorageImageUpdateAfterBind;
    features12.descriptorBindingPartiallyBound = selected12.descriptorBindingPartiallyBound;
    features12.descriptorBindingVariableDescriptorCount =
        selected12.descriptorBindingVariableDescriptorCount;
    features12.runtimeDescriptorArray = selected12.runtimeDescriptorArray;
    VkPhysicalDeviceVulkan11Features features11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12};
    features11.shaderDrawParameters = VK_TRUE;
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                        &features11};
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.multiDrawIndirect = selectedCoreFeatures.multiDrawIndirect;
#if defined(MECRAFT_ENABLE_FSR31)
    features2.features.shaderInt16 = VK_TRUE;
#endif
    const std::vector<const char*> deviceExtensions = requirements.deviceExtensionNames();
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features2};
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (!vkSucceeded(vkCreateDevice(m_data->physicalDevice, &deviceInfo, nullptr,
                                   &m_data->device),
                     "vkCreateDevice")) {
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

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.physicalDevice = m_data->physicalDevice;
    allocatorInfo.device = m_data->device;
    allocatorInfo.instance = m_data->instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (!vkSucceeded(vmaCreateAllocator(&allocatorInfo, &m_data->allocator),
                     "vmaCreateAllocator")) {
        shutdown();
        return false;
    }
    const std::array<VkDescriptorPoolSize, 6u> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096u},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096u},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096u},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096u},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 4096u},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096u}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 4096u;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (!vkSucceeded(vkCreateDescriptorPool(m_data->device, &poolInfo, nullptr,
                                           &m_data->descriptorPool),
                     "vkCreateDescriptorPool")) {
        shutdown();
        return false;
    }
    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (!vkSucceeded(vkCreatePipelineCache(m_data->device, &cacheInfo, nullptr,
                                          &m_data->pipelineCache),
                     "vkCreatePipelineCache")) {
        shutdown();
        return false;
    }
    VkSemaphoreTypeCreateInfo timelineType{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineType};
    if (!vkSucceeded(vkCreateSemaphore(m_data->device, &semaphoreInfo, nullptr,
                                      &m_data->timeline),
                     "vkCreateSemaphore(timeline)")) {
        shutdown();
        return false;
    }
    for (auto& frame : m_data->frames) {
        VkSemaphoreCreateInfo binaryInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = 0u;
        if (!vkSucceeded(vkCreateSemaphore(m_data->device, &binaryInfo, nullptr,
                                          &frame.imageAvailable),
                         "vkCreateSemaphore(image available)") ||
            !vkSucceeded(vkCreateFence(m_data->device, &fenceInfo, nullptr, &frame.fence),
                         "vkCreateFence(frame)")) {
            shutdown();
            return false;
        }
    }
    if (!createSwapchain(*m_data)) {
        shutdown();
        return false;
    }

    VkPhysicalDeviceVulkan12Properties properties12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                             &properties12};
    vkGetPhysicalDeviceProperties2(m_data->physicalDevice, &properties2);
    m_capabilities.vulkanApiVersion = m_data->properties.apiVersion;
    m_capabilities.dynamicRendering = true;
    m_capabilities.synchronization2 = true;
    m_capabilities.timelineSemaphore = true;
    m_capabilities.bufferDeviceAddress = true;
    m_capabilities.multiDrawIndirect = selectedCoreFeatures.multiDrawIndirect == VK_TRUE;
    m_capabilities.timestampQuery = m_data->properties.limits.timestampComputeAndGraphics == VK_TRUE;
    m_capabilities.textureView = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.storageImage = true;
    m_capabilities.descriptorIndexing = selected12.descriptorIndexing == VK_TRUE;
    m_capabilities.maxColorAttachments = m_data->properties.limits.maxColorAttachments;
    m_capabilities.maxSampledTexturesPerStage =
        m_data->properties.limits.maxPerStageDescriptorSampledImages;
    m_capabilities.textureBufferCopyRowPitchAlignment =
        static_cast<uint32_t>(m_data->properties.limits.optimalBufferCopyRowPitchAlignment);
    m_capabilities.maxSamplerAnisotropy = m_data->properties.limits.maxSamplerAnisotropy;
    m_capabilities.maxDescriptorSetUpdateAfterBindSampledImages =
        properties12.maxDescriptorSetUpdateAfterBindSampledImages;
    m_capabilities.maxDescriptorSetUpdateAfterBindStorageImages =
        properties12.maxDescriptorSetUpdateAfterBindStorageImages;
    m_capabilities.maxDescriptorSetUpdateAfterBindUniformBuffers =
        properties12.maxDescriptorSetUpdateAfterBindUniformBuffers;
    m_capabilities.maxDescriptorSetUpdateAfterBindStorageBuffers =
        properties12.maxDescriptorSetUpdateAfterBindStorageBuffers;
    m_capabilities.graphicsQueueFamilyIndex = m_data->queueFamilies.graphics;
    m_capabilities.computeQueueFamilyIndex = m_data->queueFamilies.compute;
    m_capabilities.transferQueueFamilyIndex = m_data->queueFamilies.transfer;
    m_capabilities.presentQueueFamilyIndex = m_data->queueFamilies.present;
    m_capabilities.dedicatedComputeQueue =
        m_data->queueFamilies.compute != m_data->queueFamilies.graphics;
    m_capabilities.dedicatedTransferQueue =
        m_data->queueFamilies.transfer != m_data->queueFamilies.graphics &&
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
        if (vkDeviceWaitIdle(m_data->device) == VK_SUCCESS && m_initialized) {
            reclaimCompletedWorkUnlocked();
        }
#if defined(MECRAFT_ENABLE_STREAMLINE)
        StreamlineRuntime& streamline = StreamlineRuntime::instance();
        if (streamline.initialized() && !streamline.shutdown()) {
            std::cerr << streamline.lastError() << '\n';
        }
#endif
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
                    vkDestroyCommandPool(m_data->device,
                                         static_cast<VkCommandPool>(commandPool), nullptr);
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
        for (auto& frame : m_data->frames) {
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_data->device, frame.imageAvailable, nullptr);
            }
            if (frame.fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_data->device, frame.fence, nullptr);
            }
        }
        if (m_data->timeline != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_data->device, m_data->timeline, nullptr);
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
        vkDestroyDevice(m_data->device, nullptr);
    }
#if defined(MECRAFT_ENABLE_STREAMLINE)
    StreamlineRuntime& streamline = StreamlineRuntime::instance();
    if (streamline.initialized() && !streamline.shutdown()) {
        std::cerr << streamline.lastError() << '\n';
    }
#endif
    if (m_data->surface != VK_NULL_HANDLE && m_data->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_data->instance, m_data->surface, nullptr);
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

RhiBufferHandle VkRhiDevice::createBuffer(const RhiBufferDesc& desc,
                                          const void* initialData,
                                          const size_t initialDataSize) {
    if (!m_initialized || desc.size == 0u ||
        (initialData == nullptr && initialDataSize != 0u) || initialDataSize > desc.size) {
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
    allocationInfo.usage = desc.memoryUsage == RhiMemoryUsage::GpuOnly
        ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    if (desc.memoryUsage != RhiMemoryUsage::GpuOnly) {
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                               VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeAllocationInfo{};
    if (!vkSucceeded(vmaCreateBuffer(m_data->allocator, &bufferInfo, &allocationInfo,
                                     &buffer, &allocation, &nativeAllocationInfo),
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
            stagingAllocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VkBuffer staging = VK_NULL_HANDLE;
            VmaAllocation stagingAllocation = VK_NULL_HANDLE;
            VmaAllocationInfo stagingNativeInfo{};
            if (!vkSucceeded(vmaCreateBuffer(m_data->allocator, &stagingInfo,
                                             &stagingAllocationInfo, &staging,
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
            const VkCommandBufferSubmitInfo commandInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, commandBuffer, 0u};
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
    const RhiBufferHandle handle = m_data->bufferHandles.allocate();
    m_data->buffers.emplace(handleKey(handle),
                            VkRhiDeviceData::Buffer{buffer, allocation, desc,
                                                    nativeAllocationInfo.pMappedData});
    nameObject(*m_data, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer), desc.debugName);
    return handle;
}

RhiTextureHandle VkRhiDevice::createTexture(const RhiTextureDesc& desc,
                                            const RhiTextureInitialData* initialData) {
    if (!m_initialized || desc.width == 0u || desc.height == 0u ||
        desc.depthOrLayers == 0u || desc.mipLevels == 0u ||
        toVkFormat(desc.format) == VK_FORMAT_UNDEFINED || toVkSampleCount(desc.sampleCount) == 0u) {
        return {};
    }
    if (initialData != nullptr && (initialData->pixels == nullptr || initialData->sizeBytes == 0u)) {
        return {};
    }
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags = desc.dimension == RhiTextureDimension::Cube
        ? VkImageCreateFlags{VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT} : VkImageCreateFlags{};
    imageInfo.imageType = toVkImageType(desc.dimension);
    imageInfo.format = toVkFormat(desc.format);
    imageInfo.extent = {desc.width, desc.height,
                        desc.dimension == RhiTextureDimension::Texture3D
                            ? desc.depthOrLayers : 1u};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.dimension == RhiTextureDimension::Texture3D
        ? 1u : desc.depthOrLayers;
    imageInfo.samples = toVkSampleCount(desc.sampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    if (initialData != nullptr) imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (!vkSucceeded(vmaCreateImage(m_data->allocator, &imageInfo, &allocationInfo,
                                    &image, &allocation, nullptr),
                     "vmaCreateImage")) {
        return {};
    }
    const RhiTextureHandle handle = m_data->textureHandles.allocate();
    m_data->textures.emplace(handleKey(handle),
                             VkRhiDeviceData::Texture{image, allocation, desc, false, false});
    nameObject(*m_data, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image), desc.debugName);
    if (initialData != nullptr && !uploadTextureInitialData(*m_data, image, desc, *initialData)) {
        std::cerr << "VkRhiDevice: texture initial data upload failed ["
                  << (desc.debugName != nullptr ? desc.debugName : "unnamed") << "]\n";
        destroyTexture(handle);
        return {};
    }
    return handle;
}

RhiTextureViewHandle VkRhiDevice::createTextureView(const RhiTextureViewDesc& desc) {
    const auto* texture = m_data != nullptr ? findRecord(m_data->textures, desc.texture) : nullptr;
    if (texture == nullptr) return {};
    const bool texture3D = texture->desc.dimension == RhiTextureDimension::Texture3D;
    const uint32_t textureLayers = texture->desc.depthOrLayers;
    const uint32_t mipCount = desc.mipCount == kRhiRemainingMipLevels
        ? texture->desc.mipLevels - std::min(desc.baseMip, texture->desc.mipLevels)
        : desc.mipCount;
    const uint32_t layerCount = desc.layerCount == kRhiRemainingArrayLayers
        ? textureLayers - std::min(desc.baseLayer, textureLayers) : desc.layerCount;
    if (desc.baseMip >= texture->desc.mipLevels || mipCount == 0u ||
        mipCount > texture->desc.mipLevels - desc.baseMip ||
        desc.baseLayer >= textureLayers || layerCount == 0u ||
        layerCount > textureLayers - desc.baseLayer) {
        return {};
    }
    if (texture3D && (desc.viewType != RhiTextureViewType::Texture3D || desc.baseLayer != 0u ||
                      layerCount != texture->desc.depthOrLayers)) {
        return {};
    }
    const RhiTextureFormat format = desc.format == RhiTextureFormat::Undefined
        ? texture->desc.format : desc.format;
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
        !vkSucceeded(vkCreateImageView(m_data->device, &info, nullptr, &view),
                     "vkCreateImageView")) {
        return {};
    }
    const RhiTextureViewHandle handle = m_data->textureViewHandles.allocate();
    m_data->textureViews.emplace(handleKey(handle),
                                 VkRhiDeviceData::TextureView{view, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(view),
               texture->desc.debugName);
    return handle;
}

RhiSamplerHandle VkRhiDevice::createSampler(const RhiSamplerDesc& desc) {
    if (!m_initialized || desc.maxAnisotropy < 1.0f ||
        desc.maxAnisotropy > m_capabilities.maxSamplerAnisotropy) return {};
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
    if (!vkSucceeded(vkCreateSampler(m_data->device, &info, nullptr, &sampler),
                     "vkCreateSampler")) return {};
    const RhiSamplerHandle handle = m_data->samplerHandles.allocate();
    m_data->samplers.emplace(handleKey(handle), VkRhiDeviceData::Sampler{sampler});
    return handle;
}

RhiShaderHandle VkRhiDevice::createShader(const RhiShaderDesc& desc) {
    if (!m_initialized) return {};
    std::string error;
    auto compiled = renderer::rhi::compileShaderToSpirv(
        desc, renderer::rhi::RhiShaderBackend::Vulkan, error);
    if (!compiled.has_value()) {
        std::cerr << "VkRhiDevice: shader compilation failed: " << error << '\n';
        return {};
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = compiled->spirv.size() * sizeof(uint32_t);
    info.pCode = compiled->spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateShaderModule(m_data->device, &info, nullptr, &module),
                     "vkCreateShaderModule")) return {};
    const RhiShaderHandle handle = m_data->shaderHandles.allocate();
    m_data->shaders.emplace(handleKey(handle),
                            VkRhiDeviceData::Shader{module, compiled->stage,
                                                   compiled->entryPoint,
                                                   std::move(compiled->reflection)});
    nameObject(*m_data, VK_OBJECT_TYPE_SHADER_MODULE,
               reinterpret_cast<uint64_t>(module), desc.debugName);
    return handle;
}

RhiBindGroupLayoutHandle VkRhiDevice::createBindGroupLayout(
    const RhiBindGroupLayoutDesc& desc) {
    if (!m_initialized) return {};
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    std::set<uint32_t> usedBindings;
    for (const auto& entry : desc.entries) {
        const VkDescriptorType type = toVkDescriptorType(entry.type);
        if (entry.arrayCount != 1u || entry.stages == 0u ||
            type == VK_DESCRIPTOR_TYPE_MAX_ENUM ||
            !usedBindings.insert(entry.binding).second) return {};
        bindings.push_back({entry.binding, type, entry.arrayCount,
                            toVkShaderStageFlags(entry.stages), nullptr});
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateDescriptorSetLayout(m_data->device, &info, nullptr, &layout),
                     "vkCreateDescriptorSetLayout")) return {};
    const RhiBindGroupLayoutHandle handle = m_data->bindGroupLayoutHandles.allocate();
    m_data->bindGroupLayouts.emplace(handleKey(handle),
                                     VkRhiDeviceData::BindGroupLayout{layout, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
               reinterpret_cast<uint64_t>(layout), desc.debugName);
    return handle;
}

RhiPipelineLayoutHandle VkRhiDevice::createPipelineLayout(const RhiPipelineLayoutDesc& desc) {
    if (!m_initialized) return {};
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(desc.bindGroupLayouts.size());
    for (const auto handle : desc.bindGroupLayouts) {
        const auto* record = findRecord(m_data->bindGroupLayouts, handle);
        if (record == nullptr) return {};
        layouts.push_back(record->layout);
    }
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = toVkShaderStageFlags(desc.pushConstantStages);
    pushRange.size = desc.pushConstantBytes;
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    if (desc.pushConstantBytes != 0u) {
        if (pushRange.stageFlags == 0u ||
            desc.pushConstantBytes > m_data->properties.limits.maxPushConstantsSize) return {};
        info.pushConstantRangeCount = 1u;
        info.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreatePipelineLayout(m_data->device, &info, nullptr, &layout),
                     "vkCreatePipelineLayout")) return {};
    const RhiPipelineLayoutHandle handle = m_data->pipelineLayoutHandles.allocate();
    m_data->pipelineLayouts.emplace(handleKey(handle),
                                    VkRhiDeviceData::PipelineLayout{layout, desc});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
               reinterpret_cast<uint64_t>(layout), desc.debugName);
    return handle;
}

RhiPipelineHandle VkRhiDevice::createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) {
    const auto* vertex = m_data != nullptr ? findRecord(m_data->shaders, desc.vertexShader) : nullptr;
    const auto* fragment = m_data != nullptr ? findRecord(m_data->shaders, desc.fragmentShader) : nullptr;
    const auto* layout = m_data != nullptr ? findRecord(m_data->pipelineLayouts, desc.layout) : nullptr;
    if (vertex == nullptr || fragment == nullptr || layout == nullptr ||
        vertex->stage != RhiShaderStage::Vertex || fragment->stage != RhiShaderStage::Fragment ||
        desc.colorFormats.size() > m_capabilities.maxColorAttachments) return {};
    const std::array<VkPipelineShaderStageCreateInfo, 2u> stages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
         VK_SHADER_STAGE_VERTEX_BIT, vertex->module, vertex->entryPoint.c_str(), nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment->module, fragment->entryPoint.c_str(), nullptr}}};
    std::vector<VkVertexInputBindingDescription> bindings;
    bindings.reserve(desc.vertexInput.bindings.size());
    for (const auto& binding : desc.vertexInput.bindings) {
        bindings.push_back({binding.binding, binding.stride,
                            binding.inputRate == RhiVertexInputRate::Vertex
                                ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.vertexInput.attributes.size());
    for (const auto& attribute : desc.vertexInput.attributes) {
        const VkFormat format = toVkVertexFormat(attribute.format);
        if (format == VK_FORMAT_UNDEFINED) return {};
        attributes.push_back({attribute.location, attribute.binding, format, attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = toVkPrimitiveTopology(desc.topology);
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineViewportDepthClipControlCreateInfoEXT depthClipControl{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT};
    depthClipControl.negativeOneToOne = VK_TRUE;
    viewportState.pNext = &depthClipControl;
    viewportState.viewportCount = 1u;
    viewportState.scissorCount = 1u;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = desc.raster.depthClampEnabled ? VK_TRUE : VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = toVkCullMode(desc.raster.cullMode);
    raster.frontFace = toVkFrontFace(desc.raster.frontFace);
    raster.depthBiasEnable = desc.raster.depthBiasEnabled ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
    raster.depthBiasSlopeFactor = desc.raster.depthBiasSlopeFactor;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompare);
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    blendAttachments.reserve(desc.colorFormats.size());
    for (size_t i = 0u; i < desc.colorFormats.size(); ++i) {
        RhiBlendAttachmentState source{};
        if (i < desc.blend.attachments.size()) source = desc.blend.attachments[i];
        VkPipelineColorBlendAttachmentState target{};
        target.blendEnable = source.blendEnabled ? VK_TRUE : VK_FALSE;
        target.srcColorBlendFactor = toVkBlendFactor(source.srcColor);
        target.dstColorBlendFactor = toVkBlendFactor(source.dstColor);
        target.colorBlendOp = toVkBlendOp(source.colorOp);
        target.srcAlphaBlendFactor = toVkBlendFactor(source.srcAlpha);
        target.dstAlphaBlendFactor = toVkBlendFactor(source.dstAlpha);
        target.alphaBlendOp = toVkBlendOp(source.alphaOp);
        target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(target);
    }
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    const std::array<VkDynamicState, 2u> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (const auto format : desc.colorFormats) {
        const VkFormat native = toVkFormat(format);
        if (native == VK_FORMAT_UNDEFINED) return {};
        colorFormats.push_back(native);
    }
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = toVkFormat(desc.depthFormat);
    rendering.stencilAttachmentFormat = desc.depthFormat == RhiTextureFormat::Depth24Stencil8
        ? rendering.depthAttachmentFormat : VK_FORMAT_UNDEFINED;
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
    if (!vkSucceeded(vkCreateGraphicsPipelines(m_data->device, m_data->pipelineCache,
                                              1u, &info, nullptr, &pipeline),
                     "vkCreateGraphicsPipelines")) return {};
    const RhiPipelineHandle handle = m_data->pipelineHandles.allocate();
    m_data->pipelines.emplace(handleKey(handle),
                              VkRhiDeviceData::Pipeline{pipeline,
                                                       VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                       desc.layout});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline),
               desc.debugName);
    return handle;
}

RhiPipelineHandle VkRhiDevice::createComputePipeline(const RhiComputePipelineDesc& desc) {
    const auto* shader = m_data != nullptr ? findRecord(m_data->shaders, desc.computeShader) : nullptr;
    const auto* layout = m_data != nullptr ? findRecord(m_data->pipelineLayouts, desc.layout) : nullptr;
    if (shader == nullptr || layout == nullptr || shader->stage != RhiShaderStage::Compute) return {};
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
                  VK_SHADER_STAGE_COMPUTE_BIT, shader->module,
                  shader->entryPoint.c_str(), nullptr};
    info.layout = layout->layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateComputePipelines(m_data->device, m_data->pipelineCache,
                                             1u, &info, nullptr, &pipeline),
                     "vkCreateComputePipelines")) return {};
    const RhiPipelineHandle handle = m_data->pipelineHandles.allocate();
    m_data->pipelines.emplace(handleKey(handle),
                              VkRhiDeviceData::Pipeline{pipeline,
                                                       VK_PIPELINE_BIND_POINT_COMPUTE,
                                                       desc.layout});
    nameObject(*m_data, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline),
               desc.debugName);
    return handle;
}

RhiBindGroupHandle VkRhiDevice::createBindGroup(const RhiBindGroupDesc& desc) {
    const auto* layout = m_data != nullptr ? findRecord(m_data->bindGroupLayouts, desc.layout) : nullptr;
    if (layout == nullptr || desc.entries.size() != layout->desc.entries.size()) return {};
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = m_data->descriptorPool;
    allocateInfo.descriptorSetCount = 1u;
    allocateInfo.pSetLayouts = &layout->layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!vkSucceeded(vkAllocateDescriptorSets(m_data->device, &allocateInfo, &set),
                     "vkAllocateDescriptorSets")) return {};
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    writes.reserve(desc.entries.size());
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    std::set<uint32_t> writtenBindings;
    for (const auto& entry : desc.entries) {
        const auto layoutEntryIt = std::find_if(layout->desc.entries.begin(), layout->desc.entries.end(),
                                                [&](const auto& item) {
                                                    return item.binding == entry.binding;
                                                });
        if (layoutEntryIt == layout->desc.entries.end() || layoutEntryIt->arrayCount != 1u ||
            !writtenBindings.insert(entry.binding).second) {
            vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
            return {};
        }
        const VkDescriptorType type = toVkDescriptorType(layoutEntryIt->type);
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1u;
        write.descriptorType = type;
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            const auto* buffer = findRecord(m_data->buffers, entry.resource.buffer.buffer);
            if (buffer == nullptr) {
                vkFreeDescriptorSets(m_data->device, m_data->descriptorPool, 1u, &set);
                return {};
            }
            bufferInfos.push_back({buffer->buffer, entry.resource.buffer.offset,
                                   entry.resource.buffer.range == 0u
                                       ? VK_WHOLE_SIZE : entry.resource.buffer.range});
            write.pBufferInfo = &bufferInfos.back();
        } else {
            RhiTextureViewHandle viewHandle{};
            RhiSamplerHandle samplerHandle{};
            if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                viewHandle = entry.resource.combinedTextureSampler.textureView;
                samplerHandle = entry.resource.combinedTextureSampler.sampler;
            } else if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                       type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                viewHandle = entry.resource.textureView;
            } else {
                samplerHandle = entry.resource.sampler;
            }
            const auto* view = viewHandle.isValid()
                ? findRecord(m_data->textureViews, viewHandle) : nullptr;
            const auto* sampler = samplerHandle.isValid()
                ? findRecord(m_data->samplers, samplerHandle) : nullptr;
            if ((viewHandle.isValid() && view == nullptr) ||
                (samplerHandle.isValid() && sampler == nullptr)) {
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
                const RhiTextureFormat viewFormat = view->desc.format == RhiTextureFormat::Undefined
                    ? texture->desc.format
                    : view->desc.format;
                imageLayout = (defaultAspectForFormat(viewFormat) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            imageInfos.push_back({sampler != nullptr ? sampler->sampler : VK_NULL_HANDLE,
                                  view != nullptr ? view->view : VK_NULL_HANDLE,
                                  imageLayout});
            write.pImageInfo = &imageInfos.back();
        }
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(m_data->device, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0u, nullptr);
    const RhiBindGroupHandle handle = m_data->bindGroupHandles.allocate();
    m_data->bindGroups.emplace(handleKey(handle),
                               VkRhiDeviceData::BindGroup{set, desc.layout, desc});
    return handle;
}

RhiQueryPoolHandle VkRhiDevice::createQueryPool(const RhiQueryPoolDesc& desc) {
    if (!m_initialized || desc.type != RhiQueryType::Timestamp || desc.queryCount == 0u) return {};
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = desc.queryCount;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (!vkSucceeded(vkCreateQueryPool(m_data->device, &info, nullptr, &pool),
                     "vkCreateQueryPool")) return {};
    const RhiQueryPoolHandle handle = m_data->queryPoolHandles.allocate();
    m_data->queryPools.emplace(handleKey(handle),
                               VkRhiDeviceData::QueryPool{pool, desc.queryCount});
    nameObject(*m_data, VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<uint64_t>(pool),
               desc.debugName);
    return handle;
}

void* VkRhiDevice::mapBuffer(const RhiBufferHandle buffer, const uint64_t offset,
                             const uint64_t size) {
    auto* record = m_data != nullptr ? findRecord(m_data->buffers, buffer) : nullptr;
    if (record == nullptr || record->desc.memoryUsage == RhiMemoryUsage::GpuOnly ||
        offset > record->desc.size || size > record->desc.size - offset) return nullptr;
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
    auto* record = m_data != nullptr ? findRecord(m_data->buffers, buffer) : nullptr;
    if (record == nullptr || record->mapped == nullptr) return;
    if (record->desc.memoryUsage == RhiMemoryUsage::CpuToGpu) {
        vmaFlushAllocation(m_data->allocator, record->allocation, 0u, VK_WHOLE_SIZE);
    }
}

bool VkRhiDevice::areQueryResultsAvailable(const RhiQueryPoolHandle pool,
                                           const uint32_t firstQuery,
                                           const uint32_t queryCount) const {
    const auto* record = m_data != nullptr ? findRecord(m_data->queryPools, pool) : nullptr;
    if (record == nullptr || queryCount == 0u || firstQuery > record->count ||
        queryCount > record->count - firstQuery) return false;
    std::vector<uint64_t> values(queryCount * 2u);
    if (vkGetQueryPoolResults(m_data->device, record->pool, firstQuery, queryCount,
                              values.size() * sizeof(uint64_t), values.data(),
                              2u * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT |
                              VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != VK_SUCCESS) return false;
    for (uint32_t i = 0u; i < queryCount; ++i) {
        if (values[i * 2u + 1u] == 0u) return false;
    }
    return true;
}

bool VkRhiDevice::getQueryResults(const RhiQueryPoolHandle pool, const uint32_t firstQuery,
                                  const uint32_t queryCount, uint64_t* results) const {
    const auto* record = m_data != nullptr ? findRecord(m_data->queryPools, pool) : nullptr;
    if (record == nullptr || results == nullptr || queryCount == 0u ||
        firstQuery > record->count || queryCount > record->count - firstQuery) return false;
    std::vector<uint64_t> ticks(queryCount);
    if (vkGetQueryPoolResults(m_data->device, record->pool, firstQuery, queryCount,
                              ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return false;
    for (uint32_t i = 0u; i < queryCount; ++i) {
        results[i] = static_cast<uint64_t>(static_cast<double>(ticks[i]) *
                                           m_data->properties.limits.timestampPeriod);
    }
    return true;
}

RhiTextureViewHandle VkRhiDevice::currentSwapchainColorView() const {
    if (m_data == nullptr || !m_data->frameAcquired ||
        m_data->acquiredImage >= m_data->swapchainViews.size()) return {};
    return m_data->swapchainViews[m_data->acquiredImage];
}

RhiTextureViewHandle VkRhiDevice::currentSwapchainDepthStencilView() const {
    if (m_data == nullptr || !m_data->frameAcquired ||
        m_data->acquiredImage >= m_data->depthViews.size()) return {};
    return m_data->depthViews[m_data->acquiredImage];
}

RhiTextureHandle VkRhiDevice::currentSwapchainColorTexture() const {
    if (m_data == nullptr || !m_data->frameAcquired ||
        m_data->acquiredImage >= m_data->swapchainTextures.size()) return {};
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
    if (!m_initialized || m_data == nullptr || m_data->frameAcquired ||
        std::this_thread::get_id() != m_deviceThread) {
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
    m_capabilities.swapchainPresentMode = m_data->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR
        ? RhiPresentMode::Immediate : RhiPresentMode::Fifo;
    m_capabilities.vsyncControl = m_data->immediatePresentSupported;
}

bool VkRhiDevice::resizeSwapchain(const uint32_t width, const uint32_t height) {
    if (!m_initialized || width == 0u || height == 0u || m_data->frameAcquired) return false;
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
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread ||
        m_data->frameAcquired) {
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
        const bool recreated = m_data->surfaceLost
            ? recreateSurfaceAndSwapchain(*m_data) : recreateSwapchain(*m_data);
        if (!recreated) {
            result.status = m_data->surfaceLost
                ? RhiFrameStatus::SurfaceLost : RhiFrameStatus::Error;
            return result;
        }
        refreshSwapchainCapabilities();
    }
    auto& frame = m_data->frames[m_data->frameSlot];
    if (frame.fencePending) {
        const VkResult waitResult = vkWaitForFences(m_data->device, 1u, &frame.fence,
                                                    VK_TRUE, UINT64_MAX);
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
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_data->device, m_data->swapchain, UINT64_MAX, frame.imageAvailable,
        VK_NULL_HANDLE, &m_data->acquiredImage);
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
    m_data->frameLastGraphicsSequence = 0u;
    ++m_data->acquiredFrameIndex;
    result.status = acquireResult == VK_SUBOPTIMAL_KHR
        ? RhiFrameStatus::Suboptimal : RhiFrameStatus::Success;
    if (acquireResult == VK_SUBOPTIMAL_KHR) m_data->swapchainDirty = true;
    result.frameIndex = m_data->acquiredFrameIndex;
    result.imageIndex = m_data->acquiredImage;
    result.width = m_data->swapchainExtent.width;
    result.height = m_data->swapchainExtent.height;
    result.colorTexture = m_data->swapchainTextures[m_data->acquiredImage];
    result.colorView = m_data->swapchainViews[m_data->acquiredImage];
    result.depthStencilView = m_data->depthViews[m_data->acquiredImage];
    return result;
}

RhiFrameStatus VkRhiDevice::presentFrame(const RhiPresentInfo& info) {
    if (!m_initialized || std::this_thread::get_id() != m_deviceThread ||
        !m_data->frameAcquired ||
        info.frameIndex != m_data->acquiredFrameIndex || info.imageIndex != m_data->acquiredImage) {
        return RhiFrameStatus::Error;
    }
    auto& frame = m_data->frames[m_data->frameSlot];
    if (!m_data->frameSubmitted) {
        const VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                         frame.imageAvailable, 0u,
                                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u};
        VkSubmitInfo2 releaseSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        releaseSubmit.waitSemaphoreInfoCount = 1u;
        releaseSubmit.pWaitSemaphoreInfos = &wait;
        const VkResult submitResult = vkQueueSubmit2(
            m_data->graphicsQueue, 1u, &releaseSubmit, VK_NULL_HANDLE);
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            logVkError("vkQueueSubmit2(frame cancel)", submitResult);
            return RhiFrameStatus::DeviceLost;
        }
        if (!vkSucceeded(submitResult, "vkQueueSubmit2(frame cancel)") ||
            !vkSucceeded(vkDeviceWaitIdle(m_data->device), "vkDeviceWaitIdle(frame cancel)")) {
            return RhiFrameStatus::Error;
        }
        if (!recreateSwapchain(*m_data)) {
            m_data->swapchainDirty = true;
            return RhiFrameStatus::Error;
        }
        refreshSwapchainCapabilities();
        return RhiFrameStatus::OutOfDate;
    }
    std::array<VkSemaphoreSubmitInfo, 2u> waits{};
    uint32_t waitCount = 0u;
    if (!m_data->frameImageAvailableWaited) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                              frame.imageAvailable, 0u,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u};
    }
    if (m_data->frameLastGraphicsSequence != 0u) {
        waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                              m_data->timeline, m_data->frameLastGraphicsSequence,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u};
    }
    const VkSemaphore presentReady = m_data->presentReadySemaphores[m_data->acquiredImage];
    const VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                       presentReady, 0u,
                                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u};
    VkSubmitInfo2 tailSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    tailSubmit.waitSemaphoreInfoCount = waitCount;
    tailSubmit.pWaitSemaphoreInfos = waits.data();
    tailSubmit.signalSemaphoreInfoCount = 1u;
    tailSubmit.pSignalSemaphoreInfos = &signal;
    const VkResult tailResult = vkQueueSubmit2(m_data->graphicsQueue, 1u, &tailSubmit,
                                               frame.fence);
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
        !StreamlineRuntime::instance().setPclMarker(
            info.trackingFrameIndex,
            StreamlinePclMarker::RenderSubmitEnd)) {
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
        !StreamlineRuntime::instance().setPclMarker(
            info.trackingFrameIndex,
            StreamlinePclMarker::PresentStart)) {
        std::cerr << StreamlineRuntime::instance().lastError() << '\n';
        return RhiFrameStatus::Error;
    }
    const VkResult proxyResult = StreamlineRuntime::instance().presentVulkanFrame(
        m_data->presentQueue, presentInfo);
    const VkResult nativeResult = swapchainResult != VK_SUCCESS
        ? swapchainResult : proxyResult;
    if (info.trackingFrameIndex != 0u &&
        !StreamlineRuntime::instance().setPclMarker(
            info.trackingFrameIndex,
            StreamlinePclMarker::PresentEnd)) {
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
    if (nativeResult == VK_SUCCESS) return RhiFrameStatus::Success;
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
    if (m_data == nullptr) return;
    const auto it = m_data->buffers.find(handleKey(handle));
    if (it == m_data->buffers.end() || !m_data->bufferHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredBuffers,
                    VkRhiDeviceData::DeferredBuffer{it->second.lifetime.lastUseSequence,
                                                    it->second.buffer,
                                                    it->second.allocation});
    m_data->buffers.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyTexture(const RhiTextureHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->textures.find(handleKey(handle));
    if (it == m_data->textures.end() || it->second.swapchainOwned) return;
    if (!m_data->textureHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredImages,
                    VkRhiDeviceData::DeferredImage{it->second.lifetime.lastUseSequence,
                                                   handleKey(handle), it->second.image,
                                                   it->second.allocation});
    m_data->textures.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyTextureView(const RhiTextureViewHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->textureViews.find(handleKey(handle));
    if (it == m_data->textureViews.end() || !m_data->textureViewHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_IMAGE_VIEW,
                        reinterpret_cast<uint64_t>(it->second.view)});
    m_data->textureViews.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroySampler(const RhiSamplerHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->samplers.find(handleKey(handle));
    if (it == m_data->samplers.end() || !m_data->samplerHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_SAMPLER,
                        reinterpret_cast<uint64_t>(it->second.sampler)});
    m_data->samplers.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyShader(const RhiShaderHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->shaders.find(handleKey(handle));
    if (it == m_data->shaders.end() || !m_data->shaderHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_SHADER_MODULE,
                        reinterpret_cast<uint64_t>(it->second.module)});
    m_data->shaders.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyBindGroupLayout(const RhiBindGroupLayoutHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->bindGroupLayouts.find(handleKey(handle));
    if (it == m_data->bindGroupLayouts.end() ||
        !m_data->bindGroupLayoutHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence,
                        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                        reinterpret_cast<uint64_t>(it->second.layout)});
    m_data->bindGroupLayouts.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyPipelineLayout(const RhiPipelineLayoutHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->pipelineLayouts.find(handleKey(handle));
    if (it == m_data->pipelineLayouts.end() || !m_data->pipelineLayoutHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        reinterpret_cast<uint64_t>(it->second.layout)});
    m_data->pipelineLayouts.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyPipeline(const RhiPipelineHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->pipelines.find(handleKey(handle));
    if (it == m_data->pipelines.end() || !m_data->pipelineHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_PIPELINE,
                        reinterpret_cast<uint64_t>(it->second.pipeline)});
    m_data->pipelines.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyBindGroup(const RhiBindGroupHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->bindGroups.find(handleKey(handle));
    if (it == m_data->bindGroups.end() || !m_data->bindGroupHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                        reinterpret_cast<uint64_t>(it->second.set)});
    m_data->bindGroups.erase(it);
    reclaimCompletedWork();
}

void VkRhiDevice::destroyQueryPool(const RhiQueryPoolHandle handle) {
    if (m_data == nullptr) return;
    const auto it = m_data->queryPools.find(handleKey(handle));
    if (it == m_data->queryPools.end() || !m_data->queryPoolHandles.release(handle)) return;
    enqueueDeferred(m_data->deferredObjects,
                    VkRhiDeviceData::DeferredObject{
                        it->second.lifetime.lastUseSequence, VK_OBJECT_TYPE_QUERY_POOL,
                        reinterpret_cast<uint64_t>(it->second.pool)});
    m_data->queryPools.erase(it);
    reclaimCompletedWork();
}

std::unique_ptr<RhiCommandListPool> VkRhiDevice::createCommandListPool(
    const RhiCommandListPoolDesc& desc) {
    if (!m_initialized || desc.initialCommandListCapacity == 0u) return nullptr;
    return std::make_unique<VkRhiCommandListPool>(*this, desc);
}

bool VkRhiDevice::submit(const RhiSubmitInfo& info, RhiSubmissionToken* completionToken) {
    if (!m_initialized || info.commandLists == nullptr || info.commandListCount == 0u ||
        info.queue != RhiQueueType::Graphics || std::this_thread::get_id() != m_deviceThread ||
        (info.waitCount == 0u) != (info.waits == nullptr)) return false;
    const std::lock_guard<std::mutex> registryLock(m_data->commandRegistryMutex);
    std::vector<VkCommandBufferSubmitInfo> commandInfos;
    std::vector<VkRhiCommandList*> lists;
    std::vector<VkRhiCommandResourceReferences> resourceReferences;
    commandInfos.reserve(info.commandListCount);
    lists.reserve(info.commandListCount);
    resourceReferences.reserve(info.commandListCount);
    for (uint32_t i = 0u; i < info.commandListCount; ++i) {
        if (m_data->commandLists.find(info.commandLists[i]) == m_data->commandLists.end()) return false;
        auto* list = static_cast<VkRhiCommandList*>(info.commandLists[i]);
        if (list == nullptr || list->m_device != this || !list->isExecutable()) return false;
        if (std::find(lists.begin(), lists.end(), list) != lists.end()) return false;
        const bool queueMatches =
            (info.queue == RhiQueueType::Graphics && list->m_data->type == RhiCommandListType::Graphics) ||
            (info.queue == RhiQueueType::Compute && list->m_data->type == RhiCommandListType::Compute) ||
            (info.queue == RhiQueueType::Transfer && list->m_data->type == RhiCommandListType::Transfer);
        if (!queueMatches) return false;
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = static_cast<VkCommandBuffer>(list->nativeCommandBuffer());
        VkRhiCommandResourceReferences resolved = list->m_data->resourceReferences;
        if (!resolveResourceReferences(*m_data, resolved)) return false;
        commandInfos.push_back(commandInfo);
        lists.push_back(list);
        resourceReferences.push_back(std::move(resolved));
    }
    const uint64_t sequence = m_lastSubmittedSequence + 1u;
    std::vector<VkSemaphoreSubmitInfo> waits;
    waits.reserve(info.waitCount + 1u);
    for (uint32_t i = 0u; i < info.waitCount; ++i) {
        const auto& wait = info.waits[i];
        if (!validateSubmissionToken(wait.token) || wait.token.sequence > m_lastSubmittedSequence) return false;
        const uint64_t value = wait.value != 0u ? wait.value : wait.token.sequence;
        if (value > wait.token.sequence) return false;
        waits.push_back({VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, m_data->timeline,
                         value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u});
    }
    const bool consumeAcquire = m_data->frameAcquired && !m_data->frameImageAvailableWaited;
    VkRhiDeviceData::FrameContext* frame = nullptr;
    if (consumeAcquire) {
        frame = &m_data->frames[m_data->frameSlot];
        waits.push_back({VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                         frame->imageAvailable, 0u,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u});
    }
    const VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr,
                                       m_data->timeline, sequence,
                                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u};
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submitInfo.pWaitSemaphoreInfos = waits.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
    submitInfo.pCommandBufferInfos = commandInfos.data();
    submitInfo.signalSemaphoreInfoCount = 1u;
    submitInfo.pSignalSemaphoreInfos = &signal;
    const VkResult nativeResult = vkQueueSubmit2(queueForType(*m_data, info.queue),
                                                 1u, &submitInfo, VK_NULL_HANDLE);
    if (nativeResult != VK_SUCCESS) {
        logVkError("vkQueueSubmit2", nativeResult);
        return false;
    }
    m_lastSubmittedSequence = sequence;
    for (size_t index = 0u; index < lists.size(); ++index) {
        auto* list = lists[index];
        markResourceReferencesUsed(*m_data, resourceReferences[index], sequence);
        for (const uint32_t imageIndex : list->m_data->initializedSwapchainImages) {
            if (imageIndex < m_data->swapchainImageInitialized.size()) {
                m_data->swapchainImageInitialized[imageIndex] = true;
            }
        }
        list->markPending(sequence);
        m_data->pendingLists.push_back({sequence, list});
        for (const auto& transient : list->m_data->transientBuffers) {
            enqueueDeferred(m_data->deferredBuffers,
                            VkRhiDeviceData::DeferredBuffer{
                                sequence, transient.first, transient.second});
        }
        list->m_data->transientBuffers.clear();
    }
    if (m_data->frameAcquired) {
        m_data->frameSubmitted = true;
        m_data->frameImageAvailableWaited = true;
        m_data->frameLastGraphicsSequence = sequence;
    }
    if (completionToken != nullptr) *completionToken = {m_deviceId, sequence};
    reclaimCompletedWorkUnlocked();
    return true;
}

bool VkRhiDevice::validateSubmissionToken(const RhiSubmissionToken token) const {
    return token.isValid() && token.deviceId == m_deviceId &&
           token.sequence <= m_lastSubmittedSequence;
}

bool VkRhiDevice::isSubmissionComplete(const RhiSubmissionToken token, bool& complete) {
    complete = false;
    if (!validateSubmissionToken(token)) return false;
    uint64_t counter = 0u;
    if (vkGetSemaphoreCounterValue(m_data->device, m_data->timeline, &counter) != VK_SUCCESS) return false;
    complete = counter >= token.sequence;
    if (complete) reclaimCompletedWork();
    return true;
}

bool VkRhiDevice::waitForSubmission(const RhiSubmissionToken token) {
    if (!validateSubmissionToken(token)) return false;
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1u;
    waitInfo.pSemaphores = &m_data->timeline;
    waitInfo.pValues = &token.sequence;
    if (vkWaitSemaphores(m_data->device, &waitInfo, UINT64_MAX) != VK_SUCCESS) return false;
    reclaimCompletedWork();
    return true;
}

void VkRhiDevice::waitIdle() {
    if (m_initialized && vkDeviceWaitIdle(m_data->device) == VK_SUCCESS) reclaimCompletedWork();
}

void VkRhiDevice::reclaimCompletedWork() {
    if (!m_initialized) return;
    const std::lock_guard<std::mutex> lock(m_data->commandRegistryMutex);
    reclaimCompletedWorkUnlocked();
}

void VkRhiDevice::reclaimCompletedWorkUnlocked() {
    if (!m_initialized) return;
    uint64_t completed = 0u;
    if (vkGetSemaphoreCounterValue(m_data->device, m_data->timeline, &completed) != VK_SUCCESS) return;
    while (!m_data->pendingLists.empty() && m_data->pendingLists.front().sequence <= completed) {
        m_data->pendingLists.front().list->markComplete();
        m_data->pendingLists.pop_front();
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
        const bool hasLiveView = std::any_of(
            m_data->textureViews.begin(), m_data->textureViews.end(), [&item](const auto& view) {
                return handleKey(view.second.desc.texture) == item.textureKey;
            });
        if (hasLiveView) break;
        vmaDestroyImage(m_data->allocator, item.image, item.allocation);
        m_data->deferredImages.pop_front();
    }
}

VkRhiCommandListPool::VkRhiCommandListPool(VkRhiDevice& device,
                                           const RhiCommandListPoolDesc& desc)
    : m_device(&device), m_ownerThread(std::this_thread::get_id()) {
    const std::array<uint32_t, 3u> families{
        device.m_data->queueFamilies.graphics, device.m_data->queueFamilies.compute,
        device.m_data->queueFamilies.transfer};
    for (size_t i = 0u; i < families.size(); ++i) {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = families[i];
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(device.m_data->device, &info, nullptr, &pool) != VK_SUCCESS) {
            for (void* created : m_commandPools) {
                if (created != nullptr) {
                    vkDestroyCommandPool(device.m_data->device,
                                         static_cast<VkCommandPool>(created), nullptr);
                }
            }
            m_commandPools = {};
            m_device = nullptr;
            return;
        }
        m_commandPools[i] = pool;
        nameObject(*device.m_data, VK_OBJECT_TYPE_COMMAND_POOL,
                   reinterpret_cast<uint64_t>(pool), desc.debugName);
    }
    m_commandLists.reserve(desc.initialCommandListCapacity);
    const std::lock_guard<std::mutex> registryLock(device.m_data->commandRegistryMutex);
    device.m_data->commandListPools.insert(this);
}

VkRhiCommandListPool::~VkRhiCommandListPool() {
    if (m_device != nullptr) {
        if (std::this_thread::get_id() != m_ownerThread) {
            std::cerr << "VkRhiDevice: command-list pool destruction requires its owner thread\n";
        }
        const std::lock_guard<std::mutex> registryLock(
            m_device->m_data->commandRegistryMutex);
        if (vkDeviceWaitIdle(m_device->m_data->device) == VK_SUCCESS) {
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
                vkDestroyCommandPool(m_device->m_data->device,
                                     static_cast<VkCommandPool>(pool), nullptr);
            }
        }
        m_device->m_data->commandListPools.erase(this);
    }
}

RhiCommandList* VkRhiCommandListPool::acquire(const RhiCommandListType type) {
    if (m_device == nullptr || std::this_thread::get_id() != m_ownerThread) return nullptr;
    for (auto& list : m_commandLists) {
        if (list->state() == RhiCommandListState::Initial && list->m_data->type == type) {
            return list.get();
        }
    }
    const size_t poolIndex = static_cast<size_t>(type);
    if (poolIndex >= m_commandPools.size() || m_commandPools[poolIndex] == nullptr) return nullptr;
    auto list = std::unique_ptr<VkRhiCommandList>(
        new VkRhiCommandList(*m_device, m_commandPools[poolIndex], type));
    if (list->nativeCommandBuffer() == nullptr) return nullptr;
    auto* result = list.get();
    {
        const std::lock_guard<std::mutex> registryLock(
            m_device->m_data->commandRegistryMutex);
        m_device->m_data->commandLists.insert(result);
    }
    m_commandLists.push_back(std::move(list));
    return result;
}

bool VkRhiCommandListPool::reset() {
    if (m_device == nullptr || std::this_thread::get_id() != m_ownerThread) return false;
    for (const auto& list : m_commandLists) {
        if (list->state() == RhiCommandListState::Recording ||
            list->state() == RhiCommandListState::Pending) return false;
    }
    for (void* pool : m_commandPools) {
        if (vkResetCommandPool(m_device->m_data->device,
                               static_cast<VkCommandPool>(pool), 0u) != VK_SUCCESS) {
            for (auto& list : m_commandLists) list->m_data->valid = false;
            return false;
        }
    }
    for (auto& list : m_commandLists) {
        for (const auto& transient : list->m_data->transientBuffers) {
            vmaDestroyBuffer(m_device->m_data->allocator, transient.first, transient.second);
        }
        list->m_data->transientBuffers.clear();
    }
    for (auto& list : m_commandLists) list->resetForPoolReuse();
    return true;
}

VkRhiCommandList::VkRhiCommandList(VkRhiDevice& device, void* nativeCommandPool,
                                   const RhiCommandListType type)
    : m_device(&device), m_data(std::make_unique<VkRhiCommandListData>()) {
    m_data->pool = static_cast<VkCommandPool>(nativeCommandPool);
    m_data->type = type;
    m_data->ownerThread = std::this_thread::get_id();
    VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = m_data->pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(device.m_data->device, &info,
                                 &m_data->commandBuffer) != VK_SUCCESS) {
        m_data->commandBuffer = VK_NULL_HANDLE;
    }
}

VkRhiCommandList::~VkRhiCommandList() = default;

bool VkRhiCommandList::begin(const RhiCommandListDesc& desc) {
    if (m_device == nullptr || m_data->commandBuffer == VK_NULL_HANDLE ||
        m_data->state != RhiCommandListState::Initial || desc.type != m_data->type ||
        std::this_thread::get_id() != m_data->ownerThread) return false;
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_data->commandBuffer, &info) != VK_SUCCESS) return false;
    m_data->state = RhiCommandListState::Recording;
    m_data->valid = true;
    m_data->resourceReferences.clear();
    nameObject(*m_device->m_data, VK_OBJECT_TYPE_COMMAND_BUFFER,
               reinterpret_cast<uint64_t>(m_data->commandBuffer), desc.debugName);
    return true;
}

bool VkRhiCommandList::end() {
    if (m_data->state != RhiCommandListState::Recording ||
        std::this_thread::get_id() != m_data->ownerThread) return false;
    if (m_data->rendering || !m_data->valid ||
        vkEndCommandBuffer(m_data->commandBuffer) != VK_SUCCESS) {
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

RhiCommandListState VkRhiCommandList::state() const { return m_data->state; }

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
    if (m_data->state == RhiCommandListState::Pending) {
        if (vkResetCommandBuffer(m_data->commandBuffer, 0u) == VK_SUCCESS) {
            resetForPoolReuse();
        } else {
            m_data->valid = false;
        }
    }
}

bool VkRhiCommandList::isExecutable() const {
    return m_data->state == RhiCommandListState::Executable && m_data->valid;
}

void* VkRhiCommandList::nativeCommandBuffer() const {
    return m_data->commandBuffer;
}

void VkRhiCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    if (m_data->state != RhiCommandListState::Recording || name == nullptr ||
        m_device->m_data->beginLabel == nullptr) return;
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
    if (m_data->state != RhiCommandListState::Recording || name == nullptr ||
        m_device->m_data->insertLabel == nullptr) return;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name;
    std::memcpy(label.color, &color[0], sizeof(label.color));
    m_device->m_data->insertLabel(m_data->commandBuffer, &label);
}

void VkRhiCommandList::textureBarrier(const RhiTextureBarrier& barrier) {
    auto* texture = findRecord(m_device->m_data->textures, barrier.texture);
    const uint32_t layers = texture != nullptr &&
        texture->desc.dimension != RhiTextureDimension::Texture3D
        ? texture->desc.depthOrLayers : 1u;
    const uint32_t mipCount = texture != nullptr && barrier.mipCount == kRhiRemainingMipLevels
        ? texture->desc.mipLevels - std::min(barrier.baseMip, texture->desc.mipLevels)
        : barrier.mipCount;
    const uint32_t layerCount = barrier.layerCount == kRhiRemainingArrayLayers
        ? layers - std::min(barrier.baseLayer, layers) : barrier.layerCount;
    if (m_data->state != RhiCommandListState::Recording || texture == nullptr ||
        barrier.baseMip >= texture->desc.mipLevels || mipCount == 0u ||
        mipCount > texture->desc.mipLevels - barrier.baseMip ||
        barrier.baseLayer >= layers || layerCount == 0u ||
        layerCount > layers - barrier.baseLayer) {
        m_data->valid = false;
        return;
    }
    const auto oldState = toVkResourceState(barrier.oldState);
    const auto newState = toVkResourceState(barrier.newState);
    VkImageMemoryBarrier2 native{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    native.srcStageMask = oldState.stages;
    native.srcAccessMask = oldState.access;
    native.dstStageMask = newState.stages;
    native.dstAccessMask = newState.access;
    native.oldLayout = oldState.layout;
    if (texture->swapchainOwned && barrier.oldState == RhiResourceState::Present) {
        const auto swapchainIt = std::find_if(
            m_device->m_data->swapchainTextures.begin(),
            m_device->m_data->swapchainTextures.end(),
            [&barrier](const RhiTextureHandle handle) {
                return handle.index == barrier.texture.index &&
                       handle.generation == barrier.texture.generation;
            });
        if (swapchainIt != m_device->m_data->swapchainTextures.end()) {
            const size_t imageIndex = static_cast<size_t>(std::distance(
                m_device->m_data->swapchainTextures.begin(), swapchainIt));
            if (!m_device->m_data->swapchainImageInitialized[imageIndex]) {
                native.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                native.srcAccessMask = VK_ACCESS_2_NONE;
                native.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (std::find(m_data->initializedSwapchainImages.begin(),
                              m_data->initializedSwapchainImages.end(),
                              static_cast<uint32_t>(imageIndex)) ==
                    m_data->initializedSwapchainImages.end()) {
                    m_data->initializedSwapchainImages.push_back(
                        static_cast<uint32_t>(imageIndex));
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
    auto* buffer = findRecord(m_device->m_data->buffers, barrier.buffer);
    const uint64_t size = buffer != nullptr && barrier.size == kRhiWholeSize
        ? buffer->desc.size - std::min(barrier.offset, buffer->desc.size) : barrier.size;
    if (m_data->state != RhiCommandListState::Recording || buffer == nullptr ||
        barrier.offset >= buffer->desc.size || size == 0u ||
        size > buffer->desc.size - barrier.offset) {
        m_data->valid = false;
        return;
    }
    const auto oldState = toVkResourceState(barrier.oldState);
    const auto newState = toVkResourceState(barrier.newState);
    VkBufferMemoryBarrier2 native{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    native.srcStageMask = oldState.stages;
    native.srcAccessMask = oldState.access;
    native.dstStageMask = newState.stages;
    native.dstAccessMask = newState.access;
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

void VkRhiCommandList::beginRendering(const RhiRenderingInfo& info) {
    if (m_data->state != RhiCommandListState::Recording || m_data->rendering ||
        info.colorAttachmentCount > m_device->m_capabilities.maxColorAttachments) {
        m_data->valid = false;
        return;
    }
    uint32_t targetWidth = 0u;
    uint32_t targetHeight = 0u;
    const auto registerAttachmentExtent = [&](const RhiTextureViewHandle viewHandle) {
        const auto* view = findRecord(m_device->m_data->textureViews, viewHandle);
        const auto* texture = view != nullptr
            ? findRecord(m_device->m_data->textures, view->desc.texture)
            : nullptr;
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
        if (view == nullptr || !registerAttachmentExtent(info.colorAttachments[i].view)) {
            m_data->valid = false;
            return;
        }
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = view->view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = toVkLoadOp(info.colorAttachments[i].loadOp);
        attachment.storeOp = toVkStoreOp(info.colorAttachments[i].storeOp);
        std::memcpy(attachment.clearValue.color.float32,
                    info.colorAttachments[i].clearColor, sizeof(float) * 4u);
        colors.push_back(attachment);
        m_data->resourceReferences.reference(info.colorAttachments[i].view);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (info.depthStencilAttachment != nullptr) {
        const auto* view = findRecord(m_device->m_data->textureViews,
                                      info.depthStencilAttachment->view);
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
    if (targetWidth == 0u || targetHeight == 0u || info.renderArea.x < 0 ||
        info.renderArea.y < 0 ||
        static_cast<uint64_t>(info.renderArea.x) + info.renderArea.width > targetWidth ||
        static_cast<uint64_t>(info.renderArea.y) + info.renderArea.height > targetHeight) {
        m_data->valid = false;
        return;
    }
    const int32_t nativeRenderAreaY = static_cast<int32_t>(targetHeight) -
        info.renderArea.y - static_cast<int32_t>(info.renderArea.height);
    VkRenderingInfo native{VK_STRUCTURE_TYPE_RENDERING_INFO};
    native.renderArea = {{info.renderArea.x, nativeRenderAreaY},
                         {info.renderArea.width, info.renderArea.height}};
    native.layerCount = 1u;
    native.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    native.pColorAttachments = colors.data();
    native.pDepthAttachment = info.depthStencilAttachment != nullptr ? &depth : nullptr;
    native.pStencilAttachment = nullptr;
    const float nativeViewportY = static_cast<float>(
        static_cast<int32_t>(targetHeight) - info.renderArea.y);
    const float nativeViewportHeight = -static_cast<float>(info.renderArea.height);
    const VkViewport viewport{static_cast<float>(info.renderArea.x),
                              nativeViewportY,
                              static_cast<float>(info.renderArea.width),
                              nativeViewportHeight, 0.0f, 1.0f};
    const VkRect2D scissor{{info.renderArea.x, nativeRenderAreaY},
                           {info.renderArea.width, info.renderArea.height}};
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
    const VkRect2D nativeRect = toVkClippedScissor(
        rect, m_data->renderingTargetWidth, m_data->renderingTargetHeight);
    VkClearRect clearRect{nativeRect, 0u, 1u};
    vkCmdClearAttachments(m_data->commandBuffer, 1u, &attachment, 1u, &clearRect);
}

void VkRhiCommandList::setViewport(const RhiViewport& viewport) {
    if (m_data->state != RhiCommandListState::Recording || !m_data->rendering ||
        viewport.x < 0.0f || viewport.y < 0.0f || viewport.width <= 0.0f ||
        viewport.height <= 0.0f ||
        viewport.x + viewport.width > static_cast<float>(m_data->renderingTargetWidth) ||
        viewport.y + viewport.height > static_cast<float>(m_data->renderingTargetHeight)) {
        m_data->valid = false;
        return;
    }
    const float nativeY = static_cast<float>(m_data->renderingTargetHeight) - viewport.y;
    const float nativeHeight = -viewport.height;
    const VkViewport native{viewport.x, nativeY,
                            viewport.width, nativeHeight,
                            viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(m_data->commandBuffer, 0u, 1u, &native);
}

void VkRhiCommandList::setScissor(const RhiRect2D& rect) {
    if (!m_data->rendering) {
        m_data->valid = false;
        return;
    }
    const VkRect2D native = toVkClippedScissor(
        rect, m_data->renderingTargetWidth, m_data->renderingTargetHeight);
    vkCmdSetScissor(m_data->commandBuffer, 0u, 1u, &native);
}

void VkRhiCommandList::setGraphicsPipeline(const RhiPipelineHandle pipeline) {
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
    const auto* record = findRecord(m_device->m_data->pipelines, pipeline);
    if (record == nullptr || record->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE ||
        m_data->rendering) {
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

void VkRhiCommandList::setBindGroup(const uint32_t setIndex,
                                    const RhiBindGroupHandle bindGroup) {
    const auto* record = findRecord(m_device->m_data->bindGroups, bindGroup);
    const auto* pipelineLayout = findRecord(m_device->m_data->pipelineLayouts,
                                            m_data->pipelineLayoutHandle);
    if (record == nullptr || pipelineLayout == nullptr ||
        setIndex >= pipelineLayout->desc.bindGroupLayouts.size() ||
        pipelineLayout->desc.bindGroupLayouts[setIndex].index != record->layoutHandle.index ||
        pipelineLayout->desc.bindGroupLayouts[setIndex].generation != record->layoutHandle.generation) {
        m_data->valid = false;
        return;
    }
    vkCmdBindDescriptorSets(m_data->commandBuffer, m_data->bindPoint,
                            m_data->pipelineLayout, setIndex, 1u, &record->set,
                            0u, nullptr);
    m_data->resourceReferences.reference(bindGroup);
}

void VkRhiCommandList::setVertexBuffer(const uint32_t slot, const RhiBufferHandle buffer,
                                       const uint64_t offset) {
    const auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || offset >= record->desc.size) {
        m_data->valid = false;
        return;
    }
    vkCmdBindVertexBuffers(m_data->commandBuffer, slot, 1u, &record->buffer, &offset);
    m_data->resourceReferences.reference(buffer);
}

void VkRhiCommandList::setIndexBuffer(const RhiBufferHandle buffer,
                                      const RhiIndexFormat format, const uint64_t offset) {
    const auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || offset >= record->desc.size) {
        m_data->valid = false;
        return;
    }
    vkCmdBindIndexBuffer(m_data->commandBuffer, record->buffer, offset,
                         format == RhiIndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16
                                                          : VK_INDEX_TYPE_UINT32);
    m_data->resourceReferences.reference(buffer);
}

void VkRhiCommandList::pushConstants(const void* data, const size_t size,
                                     const RhiShaderStageFlags stages) {
    if (data == nullptr || size == 0u || m_data->pipelineLayout == VK_NULL_HANDLE ||
        size > m_device->m_data->properties.limits.maxPushConstantsSize) {
        m_data->valid = false;
        return;
    }
    vkCmdPushConstants(m_data->commandBuffer, m_data->pipelineLayout,
                       toVkShaderStageFlags(stages), 0u,
                       static_cast<uint32_t>(size), data);
}

void VkRhiCommandList::draw(const uint32_t vertexCount, const uint32_t instanceCount,
                            const uint32_t firstVertex, const uint32_t firstInstance) {
    if (!m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        m_data->valid = false;
        return;
    }
    vkCmdDraw(m_data->commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VkRhiCommandList::drawIndexed(const uint32_t indexCount, const uint32_t instanceCount,
                                   const uint32_t firstIndex, const int32_t vertexOffset,
                                   const uint32_t firstInstance) {
    if (!m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        m_data->valid = false;
        return;
    }
    vkCmdDrawIndexed(m_data->commandBuffer, indexCount, instanceCount, firstIndex,
                     vertexOffset, firstInstance);
}

void VkRhiCommandList::drawIndirect(const RhiBufferHandle indirectBuffer,
                                    const uint64_t offset, const uint32_t drawCount,
                                    const uint32_t stride) {
    const auto* record = findRecord(m_device->m_data->buffers, indirectBuffer);
    if (record == nullptr || !m_data->rendering) {
        m_data->valid = false;
        return;
    }
    vkCmdDrawIndirect(m_data->commandBuffer, record->buffer, offset, drawCount, stride);
    m_data->resourceReferences.reference(indirectBuffer);
}

void VkRhiCommandList::dispatch(const uint32_t groupCountX, const uint32_t groupCountY,
                                const uint32_t groupCountZ) {
    if (m_data->rendering || m_data->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        m_data->valid = false;
        return;
    }
    vkCmdDispatch(m_data->commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VkRhiCommandList::updateBuffer(const RhiBufferHandle buffer, const uint64_t offset,
                                    const void* data, const size_t size) {
    auto* record = findRecord(m_device->m_data->buffers, buffer);
    if (record == nullptr || data == nullptr || size == 0u ||
        offset > record->desc.size || size > record->desc.size - offset ||
        m_data->state != RhiCommandListState::Recording) {
        m_data->valid = false;
        return;
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo nativeInfo{};
    if (vmaCreateBuffer(m_device->m_data->allocator, &bufferInfo, &allocationInfo,
                        &staging, &allocation, &nativeInfo) != VK_SUCCESS) {
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
    const auto* source = findRecord(m_device->m_data->buffers, copy.srcBuffer);
    const auto* destination = findRecord(m_device->m_data->textures, copy.dstTexture);
    if (source == nullptr || destination == nullptr) {
        m_data->valid = false;
        return;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = copy.bufferOffset;
    region.imageSubresource = {defaultAspectForFormat(destination->desc.format), copy.mipLevel,
                               copy.arrayLayer, 1u};
    region.imageOffset = {static_cast<int32_t>(copy.dstX), static_cast<int32_t>(copy.dstY),
                          static_cast<int32_t>(copy.dstZ)};
    region.imageExtent = {copy.width, copy.height, copy.depth};
    vkCmdCopyBufferToImage(m_data->commandBuffer, source->buffer, destination->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    m_data->resourceReferences.reference(copy.srcBuffer);
    m_data->resourceReferences.reference(copy.dstTexture);
}

void VkRhiCommandList::copyTextureToBuffer(const RhiTextureBufferCopy& copy) {
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
    region.imageSubresource = {defaultAspectForFormat(source->desc.format), copy.mipLevel,
                               copy.arrayLayer, 1u};
    region.imageOffset = {static_cast<int32_t>(copy.srcX), static_cast<int32_t>(copy.srcY),
                          static_cast<int32_t>(copy.srcZ)};
    region.imageExtent = {copy.width, copy.height, copy.depth};
    vkCmdCopyImageToBuffer(m_data->commandBuffer, source->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->buffer,
                           1u, &region);
    m_data->resourceReferences.reference(copy.srcTexture);
    m_data->resourceReferences.reference(copy.dstBuffer);
}

void VkRhiCommandList::copyTexture(const RhiTextureCopy& copy) {
    const auto* source = findRecord(m_device->m_data->textures, copy.src);
    const auto* destination = findRecord(m_device->m_data->textures, copy.dst);
    if (source == nullptr || destination == nullptr) {
        m_data->valid = false;
        return;
    }
    VkImageCopy region{};
    region.srcSubresource = {defaultAspectForFormat(source->desc.format),
                             copy.srcSubresource.mipLevel,
                             copy.srcSubresource.baseArrayLayer,
                             copy.srcSubresource.layerCount};
    region.srcOffset = {static_cast<int32_t>(copy.srcOffset.x),
                        static_cast<int32_t>(copy.srcOffset.y),
                        static_cast<int32_t>(copy.srcOffset.z)};
    region.dstSubresource = {defaultAspectForFormat(destination->desc.format),
                             copy.dstSubresource.mipLevel,
                             copy.dstSubresource.baseArrayLayer,
                             copy.dstSubresource.layerCount};
    region.dstOffset = {static_cast<int32_t>(copy.dstOffset.x),
                        static_cast<int32_t>(copy.dstOffset.y),
                        static_cast<int32_t>(copy.dstOffset.z)};
    region.extent = {copy.extent.width, copy.extent.height, copy.extent.depth};
    vkCmdCopyImage(m_data->commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    m_data->resourceReferences.reference(copy.src);
    m_data->resourceReferences.reference(copy.dst);
}

void VkRhiCommandList::blitTexture(const RhiTextureBlit& blit) {
    RhiTextureHandle sourceHandle = blit.src;
    RhiTextureHandle destinationHandle = blit.dst;
    const auto* sourceView = blit.srcView.isValid()
        ? findRecord(m_device->m_data->textureViews, blit.srcView) : nullptr;
    const auto* destinationView = blit.dstView.isValid()
        ? findRecord(m_device->m_data->textureViews, blit.dstView) : nullptr;
    if (!sourceHandle.isValid() && sourceView != nullptr) {
        sourceHandle = sourceView->desc.texture;
    }
    if (!destinationHandle.isValid() && destinationView != nullptr) {
        destinationHandle = destinationView->desc.texture;
    }
    const auto* source = findRecord(m_device->m_data->textures, sourceHandle);
    const auto* destination = findRecord(m_device->m_data->textures, destinationHandle);
    const VkFormat sourceFormat = source != nullptr ? toVkFormat(source->desc.format)
                                                     : VK_FORMAT_UNDEFINED;
    const VkFormat destinationFormat = destination != nullptr
        ? toVkFormat(destination->desc.format) : VK_FORMAT_UNDEFINED;
    const VkImageAspectFlags sourceAspect = source != nullptr
        ? defaultAspectForFormat(source->desc.format) : 0u;
    const VkImageAspectFlags destinationAspect = destination != nullptr
        ? defaultAspectForFormat(destination->desc.format) : 0u;
    const bool colorBlit = (sourceAspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0u;
    const VkFormatFeatureFlags2 sourceFeatures = VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
        (colorBlit ? VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT : 0u);
    if (source == nullptr || destination == nullptr ||
        sourceAspect != destinationAspect ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice, sourceFormat,
                                sourceFeatures) ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice, destinationFormat,
                                VK_FORMAT_FEATURE_2_BLIT_DST_BIT)) {
        m_data->valid = false;
        return;
    }
    VkImageBlit region{};
    region.srcSubresource = {defaultAspectForFormat(source->desc.format), blit.srcMipLevel, 0u, 1u};
    const int32_t sourceWidth = static_cast<int32_t>(
        std::max(source->desc.width >> blit.srcMipLevel, 1u));
    const int32_t sourceHeight = static_cast<int32_t>(
        std::max(source->desc.height >> blit.srcMipLevel, 1u));
    region.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
    region.dstSubresource = {defaultAspectForFormat(destination->desc.format), blit.dstMipLevel, 0u, 1u};
    const int32_t destinationWidth = static_cast<int32_t>(
        std::max(destination->desc.width >> blit.dstMipLevel, 1u));
    const int32_t destinationHeight = static_cast<int32_t>(
        std::max(destination->desc.height >> blit.dstMipLevel, 1u));
    region.dstOffsets[1] = {destinationWidth, destinationHeight, 1};
    vkCmdBlitImage(m_data->commandBuffer, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &region, colorBlit ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    m_data->resourceReferences.reference(sourceHandle);
    m_data->resourceReferences.reference(destinationHandle);
    m_data->resourceReferences.reference(blit.srcView);
    m_data->resourceReferences.reference(blit.dstView);
}

void VkRhiCommandList::generateMipmaps(const RhiTextureHandle textureHandle) {
    const auto* texture = findRecord(m_device->m_data->textures, textureHandle);
    if (texture == nullptr || texture->desc.mipLevels < 2u ||
        (defaultAspectForFormat(texture->desc.format) & VK_IMAGE_ASPECT_COLOR_BIT) == 0u ||
        !supportsFormatFeatures(m_device->m_data->physicalDevice,
                                toVkFormat(texture->desc.format),
                                VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
                                VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                                VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        m_data->valid = false;
        return;
    }
    int32_t width = static_cast<int32_t>(texture->desc.width);
    int32_t height = static_cast<int32_t>(texture->desc.height);
    int32_t depth = texture->desc.dimension == RhiTextureDimension::Texture3D
        ? static_cast<int32_t>(texture->desc.depthOrLayers) : 1;
    const uint32_t layers = texture->desc.dimension == RhiTextureDimension::Texture3D
        ? 1u : texture->desc.depthOrLayers;
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
        vkCmdBlitImage(m_data->commandBuffer, texture->image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image,
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

void VkRhiCommandList::resetQueryPool(const RhiQueryPoolHandle pool,
                                      const uint32_t firstQuery,
                                      const uint32_t queryCount) {
    const auto* record = findRecord(m_device->m_data->queryPools, pool);
    if (record == nullptr || firstQuery > record->count || queryCount > record->count - firstQuery) {
        m_data->valid = false;
        return;
    }
    vkCmdResetQueryPool(m_data->commandBuffer, record->pool, firstQuery, queryCount);
    m_data->resourceReferences.reference(pool);
}

void VkRhiCommandList::writeTimestamp(const RhiQueryPoolHandle pool,
                                      const uint32_t queryIndex) {
    const auto* record = findRecord(m_device->m_data->queryPools, pool);
    if (record == nullptr || queryIndex >= record->count) {
        m_data->valid = false;
        return;
    }
    vkCmdWriteTimestamp2(m_data->commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         record->pool, queryIndex);
    m_data->resourceReferences.reference(pool);
}
