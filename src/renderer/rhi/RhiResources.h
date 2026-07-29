#ifndef MECRAFT_RHI_RESOURCES_H
#define MECRAFT_RHI_RESOURCES_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

inline constexpr uint32_t kRhiRemainingMipLevels = 0xFFFFFFFFu;
inline constexpr uint32_t kRhiRemainingArrayLayers = 0xFFFFFFFFu;
inline constexpr uint64_t kRhiWholeSize = std::numeric_limits<uint64_t>::max();
inline constexpr uint32_t kRhiQueueFamilyIgnored = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kRhiQueueFamilyExternal = kRhiQueueFamilyIgnored - 1u;

/// Identifies the owning subsystem for one RHI allocation. Resource creators
/// must set this explicitly; Unclassified remains visible in diagnostics so
/// missing ownership metadata cannot be hidden in another category.
enum class RhiMemoryCategory : uint8_t {
    Unclassified,
    GBufferHistory,
    Nrd,
    AccelerationStructure,
    Texture,
    Geometry,
    SceneData,
    Uniform,
    Readback,
    Transient,
    Presentation,
    Sdk,
    Count
};

inline constexpr size_t kRhiMemoryCategoryCount =
    static_cast<size_t>(RhiMemoryCategory::Count);

/// Reports whether a category can index a memory statistics snapshot.
/// @param category Category value to validate.
/// @return True for every concrete category and false for Count.
[[nodiscard]] constexpr bool rhiMemoryCategoryValid(
    const RhiMemoryCategory category) {
    return static_cast<size_t>(category) < kRhiMemoryCategoryCount;
}

/// Describes whether reported bytes originate from native allocations or
/// from backend-independent resource description estimates.
enum class RhiMemoryStatsAccuracy : uint8_t {
    Unavailable,
    Exact,
    Estimated
};

/// Current allocation and resource totals for one ownership category.
struct RhiMemoryCategoryStats {
    uint64_t bytes = 0u;
    uint64_t allocationCount = 0u;
    uint64_t resourceCount = 0u;
};

/// Snapshot of all live RHI resources and their backing allocations.
struct RhiMemoryStats {
    bool valid = false;
    RhiMemoryStatsAccuracy accuracy = RhiMemoryStatsAccuracy::Unavailable;
    std::array<RhiMemoryCategoryStats, kRhiMemoryCategoryCount> categories{};
    uint64_t totalBytes = 0u;
    uint64_t totalAllocationCount = 0u;
    uint64_t totalResourceCount = 0u;

    /// Adds one category contribution and updates the snapshot totals.
    /// @param category Explicit resource ownership category.
    /// @param bytes Backing allocation bytes attributed to the category.
    /// @param allocationCount Number of backing allocations represented.
    /// @param resourceCount Number of live public RHI resources represented.
    /// @return False when category is Count or otherwise outside the contract.
    [[nodiscard]] bool add(RhiMemoryCategory category,
                           uint64_t bytes,
                           uint64_t allocationCount,
                           uint64_t resourceCount) {
        const size_t index = static_cast<size_t>(category);
        if (index >= categories.size()) {
            return false;
        }
        RhiMemoryCategoryStats& entry = categories[index];
        if (bytes > std::numeric_limits<uint64_t>::max() - entry.bytes ||
            allocationCount >
                std::numeric_limits<uint64_t>::max() - entry.allocationCount ||
            resourceCount >
                std::numeric_limits<uint64_t>::max() - entry.resourceCount ||
            bytes > std::numeric_limits<uint64_t>::max() - totalBytes ||
            allocationCount >
                std::numeric_limits<uint64_t>::max() - totalAllocationCount ||
            resourceCount >
                std::numeric_limits<uint64_t>::max() - totalResourceCount) {
            return false;
        }
        entry.bytes += bytes;
        entry.allocationCount += allocationCount;
        entry.resourceCount += resourceCount;
        totalBytes += bytes;
        totalAllocationCount += allocationCount;
        totalResourceCount += resourceCount;
        return true;
    }
};

/// Returns the stable machine-readable identifier for a memory category.
/// @param category Category to identify.
/// @return Stable lowercase identifier, or "invalid" for Count.
[[nodiscard]] constexpr const char* rhiMemoryCategoryStableId(
    const RhiMemoryCategory category) {
    switch (category) {
        case RhiMemoryCategory::Unclassified: return "unclassified";
        case RhiMemoryCategory::GBufferHistory: return "gbuffer_history";
        case RhiMemoryCategory::Nrd: return "nrd";
        case RhiMemoryCategory::AccelerationStructure:
            return "acceleration_structure";
        case RhiMemoryCategory::Texture: return "texture";
        case RhiMemoryCategory::Geometry: return "geometry";
        case RhiMemoryCategory::SceneData: return "scene_data";
        case RhiMemoryCategory::Uniform: return "uniform";
        case RhiMemoryCategory::Readback: return "readback";
        case RhiMemoryCategory::Transient: return "transient";
        case RhiMemoryCategory::Presentation: return "presentation";
        case RhiMemoryCategory::Sdk: return "sdk";
        case RhiMemoryCategory::Count: return "invalid";
    }
    return "invalid";
}

/// Returns the human-readable English label for a memory category.
/// @param category Category to label.
/// @return Display label, or "Invalid" for Count.
[[nodiscard]] constexpr const char* rhiMemoryCategoryDisplayName(
    const RhiMemoryCategory category) {
    switch (category) {
        case RhiMemoryCategory::Unclassified: return "Unclassified";
        case RhiMemoryCategory::GBufferHistory: return "GBuffer / History";
        case RhiMemoryCategory::Nrd: return "NRD";
        case RhiMemoryCategory::AccelerationStructure:
            return "Acceleration Structure";
        case RhiMemoryCategory::Texture: return "Texture";
        case RhiMemoryCategory::Geometry: return "Geometry";
        case RhiMemoryCategory::SceneData: return "Scene Data";
        case RhiMemoryCategory::Uniform: return "Uniform";
        case RhiMemoryCategory::Readback: return "Readback";
        case RhiMemoryCategory::Transient: return "Transient";
        case RhiMemoryCategory::Presentation: return "Presentation";
        case RhiMemoryCategory::Sdk: return "SDK";
        case RhiMemoryCategory::Count: return "Invalid";
    }
    return "Invalid";
}

/// Returns the stable machine-readable identifier for snapshot accuracy.
/// @param accuracy Accuracy mode reported by the backend.
/// @return Stable lowercase identifier.
[[nodiscard]] constexpr const char* rhiMemoryStatsAccuracyStableId(
    const RhiMemoryStatsAccuracy accuracy) {
    switch (accuracy) {
        case RhiMemoryStatsAccuracy::Unavailable: return "unavailable";
        case RhiMemoryStatsAccuracy::Exact: return "exact";
        case RhiMemoryStatsAccuracy::Estimated: return "estimated";
    }
    return "unavailable";
}

/// Returns the storage bytes per texel for an uncompressed RHI format.
/// @param format Texture format to measure.
/// @return Bytes per texel, or zero for Undefined.
[[nodiscard]] constexpr uint64_t rhiTextureFormatSizeBytes(
    const RhiTextureFormat format) {
    switch (format) {
        case RhiTextureFormat::R8Unorm: return 1u;
        case RhiTextureFormat::Rg8Unorm: return 2u;
        case RhiTextureFormat::Rgba8Unorm:
        case RhiTextureFormat::Rgba8Srgb:
        case RhiTextureFormat::Bgra8Unorm:
        case RhiTextureFormat::Bgra8Srgb:
        case RhiTextureFormat::Rgb10A2Unorm:
        case RhiTextureFormat::R32Float:
        case RhiTextureFormat::R32Uint:
        case RhiTextureFormat::Depth24:
        case RhiTextureFormat::Depth24Stencil8:
        case RhiTextureFormat::Depth32Float:
            return 4u;
        case RhiTextureFormat::R16Float:
        case RhiTextureFormat::Depth16:
            return 2u;
        case RhiTextureFormat::Rg16Float: return 4u;
        case RhiTextureFormat::Rg32Uint:
        case RhiTextureFormat::Rgba16Float: return 8u;
        case RhiTextureFormat::Rgba32Float: return 16u;
        case RhiTextureFormat::Undefined: return 0u;
    }
    return 0u;
}

/// Reports whether a texture format stores unsigned integer color components.
/// @param format Texture format to classify.
/// @return True only for unsigned integer color formats.
[[nodiscard]] constexpr bool rhiTextureFormatIsUnsignedInteger(
    const RhiTextureFormat format) {
    switch (format) {
        case RhiTextureFormat::R32Uint:
        case RhiTextureFormat::Rg32Uint:
            return true;
        default:
            return false;
    }
}

/// Selects whether a resource barrier performs a complete transition or one
/// half of a queue-family ownership transfer.
enum class RhiBarrierPhase { Full, Release, Acquire };

enum class RhiTextureAspect : uint32_t {
    Color = 1u << 0u,
    Depth = 1u << 1u,
    Stencil = 1u << 2u
};

using RhiTextureAspectFlags = uint32_t;

[[nodiscard]] constexpr RhiTextureAspectFlags rhiFlag(const RhiTextureAspect aspect) {
    return static_cast<RhiTextureAspectFlags>(aspect);
}

/// Memory footprint of a texture description, used to place multiple
/// lifetime-disjoint textures on one shared device memory block.
struct RhiTextureMemoryRequirements {
    uint64_t sizeBytes = 0u;
    uint64_t alignment = 0u;
    /// Backend-opaque compatibility mask; a block can only host textures
    /// whose masks all include the block's chosen memory type.
    uint32_t memoryTypeBits = 0u;
};

/// Selects the queue-family sharing contract used when a texture is created.
/// Concurrent graphics/compute textures may be read from both queue families
/// without queue-family ownership transfers; write hazards still require
/// explicit Render Graph dependencies and memory barriers.
enum class RhiTextureQueueSharing {
    Exclusive,
    GraphicsComputeConcurrent
};

struct RhiTextureDesc {
    const char* debugName = nullptr;
    RhiTextureDimension dimension = RhiTextureDimension::Texture2D;
    RhiTextureFormat format = RhiTextureFormat::Rgba8Unorm;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depthOrLayers = 1;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 1;
    RhiTextureUsageFlags usage = 0;
    RhiMemoryCategory memoryCategory = RhiMemoryCategory::Unclassified;
    RhiTextureQueueSharing queueSharing = RhiTextureQueueSharing::Exclusive;
};

/// Estimates the complete texture storage described by every mip and layer.
/// The calculation models uncompressed resource payload bytes and excludes
/// backend-specific alignment and allocator metadata.
/// @param desc Texture description to measure.
/// @return Estimated bytes, or std::nullopt for invalid data or overflow.
[[nodiscard]] inline std::optional<uint64_t> rhiEstimateTextureBytes(
    const RhiTextureDesc& desc) {
    const uint64_t bytesPerTexel = rhiTextureFormatSizeBytes(desc.format);
    if (bytesPerTexel == 0u || desc.width == 0u || desc.height == 0u ||
        desc.depthOrLayers == 0u || desc.mipLevels == 0u ||
        desc.sampleCount == 0u) {
        return std::nullopt;
    }
    const auto checkedMultiply = [](const uint64_t lhs,
                                    const uint64_t rhs,
                                    uint64_t& result) {
        if (lhs != 0u && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
            return false;
        }
        result = lhs * rhs;
        return true;
    };
    uint64_t totalBytes = 0u;
    for (uint32_t mip = 0u; mip < desc.mipLevels; ++mip) {
        const uint64_t width = mip < 32u
            ? std::max<uint32_t>(1u, desc.width >> mip) : 1u;
        const uint64_t height = mip < 32u
            ? std::max<uint32_t>(1u, desc.height >> mip) : 1u;
        const uint64_t depthOrLayers =
            desc.dimension == RhiTextureDimension::Texture3D
                ? (mip < 32u
                       ? std::max<uint32_t>(1u, desc.depthOrLayers >> mip)
                       : 1u)
                : desc.depthOrLayers;
        uint64_t mipBytes = 0u;
        if (!checkedMultiply(width, height, mipBytes) ||
            !checkedMultiply(mipBytes, depthOrLayers, mipBytes) ||
            !checkedMultiply(mipBytes, desc.sampleCount, mipBytes) ||
            !checkedMultiply(mipBytes, bytesPerTexel, mipBytes) ||
            mipBytes > std::numeric_limits<uint64_t>::max() - totalBytes) {
            return std::nullopt;
        }
        totalBytes += mipBytes;
    }
    return totalBytes;
}

struct RhiTextureInitialData {
    const void* pixels = nullptr;
    size_t sizeBytes = 0;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
    uint32_t layerCount = 1;
    RhiResourceState finalState = RhiResourceState::Undefined;
};

struct RhiTextureViewDesc {
    RhiTextureHandle texture;
    RhiTextureViewType viewType = RhiTextureViewType::Texture2D;
    RhiTextureFormat format = RhiTextureFormat::Undefined;
    uint32_t baseMip = 0;
    uint32_t mipCount = 1;
    uint32_t baseLayer = 0;
    uint32_t layerCount = 1;
    bool depthCompare = false;
};

struct RhiBufferDesc {
    const char* debugName = nullptr;
    uint64_t size = 0;
    RhiBufferUsageFlags usage = 0;
    RhiMemoryUsage memoryUsage = RhiMemoryUsage::GpuOnly;
    RhiResourceState initialState = RhiResourceState::Undefined;
    RhiMemoryCategory memoryCategory = RhiMemoryCategory::Unclassified;
};

struct RhiSamplerDesc {
    RhiFilter minFilter = RhiFilter::Linear;
    RhiFilter magFilter = RhiFilter::Linear;
    RhiMipmapMode mipmapMode = RhiMipmapMode::Linear;
    RhiAddressMode addressU = RhiAddressMode::ClampToEdge;
    RhiAddressMode addressV = RhiAddressMode::ClampToEdge;
    RhiAddressMode addressW = RhiAddressMode::ClampToEdge;
    RhiBorderColor borderColor = RhiBorderColor::TransparentBlack;
    float maxAnisotropy = 1.0f;
    bool compareEnabled = false;
    RhiCompareOp compareOp = RhiCompareOp::LessOrEqual;
};

struct RhiQueryPoolDesc {
    const char* debugName = nullptr;
    RhiQueryType type = RhiQueryType::Timestamp;
    uint32_t queryCount = 1;
};

enum class RhiColorClearValueType : uint8_t {
    Float,
    Uint
};

struct RhiColorAttachment {
    RhiTextureViewHandle view;
    RhiLoadOp loadOp = RhiLoadOp::Load;
    RhiStoreOp storeOp = RhiStoreOp::Store;
    RhiColorClearValueType clearValueType = RhiColorClearValueType::Float;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t clearColorUint[4] = {0u, 0u, 0u, 0u};
};

struct RhiDepthStencilAttachment {
    RhiTextureViewHandle view;
    RhiLoadOp depthLoadOp = RhiLoadOp::Load;
    RhiStoreOp depthStoreOp = RhiStoreOp::Store;
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;
};

struct RhiRenderingInfo {
    const char* debugName = nullptr;
    RhiRect2D renderArea;
    const RhiColorAttachment* colorAttachments = nullptr;
    uint32_t colorAttachmentCount = 0;
    const RhiDepthStencilAttachment* depthStencilAttachment = nullptr;
};

struct RhiTextureBarrier {
    RhiTextureHandle texture;
    RhiResourceState oldState = RhiResourceState::Undefined;
    RhiResourceState newState = RhiResourceState::Undefined;
    uint32_t baseMip = 0u;
    uint32_t mipCount = kRhiRemainingMipLevels;
    uint32_t baseLayer = 0u;
    uint32_t layerCount = kRhiRemainingArrayLayers;
    // Zero selects every aspect declared by the texture format.
    RhiTextureAspectFlags aspect = 0u;
    uint32_t srcQueueFamilyIndex = kRhiQueueFamilyIgnored;
    uint32_t dstQueueFamilyIndex = kRhiQueueFamilyIgnored;
    /// Selects full transition, source-queue release, or destination-queue acquire.
    RhiBarrierPhase phase = RhiBarrierPhase::Full;
};

struct RhiBufferBarrier {
    RhiBufferHandle buffer;
    RhiResourceState oldState = RhiResourceState::Undefined;
    RhiResourceState newState = RhiResourceState::Undefined;
    uint64_t offset = 0u;
    uint64_t size = kRhiWholeSize;
    uint32_t srcQueueFamilyIndex = kRhiQueueFamilyIgnored;
    uint32_t dstQueueFamilyIndex = kRhiQueueFamilyIgnored;
    /// Selects full transition, source-queue release, or destination-queue acquire.
    RhiBarrierPhase phase = RhiBarrierPhase::Full;
};

struct RhiBufferCopy {
    RhiBufferHandle src;
    RhiBufferHandle dst;
    uint64_t srcOffset = 0;
    uint64_t dstOffset = 0;
    uint64_t size = 0;
};

struct RhiBufferTextureCopy {
    RhiBufferHandle srcBuffer;
    RhiTextureHandle dstTexture;
    uint64_t bufferOffset = 0;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
    uint32_t dstX = 0;
    uint32_t dstY = 0;
    uint32_t dstZ = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

struct RhiTextureBufferCopy {
    RhiTextureHandle srcTexture;
    RhiBufferHandle dstBuffer;
    uint64_t bufferOffset = 0;
    uint32_t bytesPerRow = 0;
    uint32_t rowsPerImage = 0;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
    uint32_t srcX = 0;
    uint32_t srcY = 0;
    uint32_t srcZ = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

struct RhiTextureSubresourceLayers {
    uint32_t mipLevel = 0;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;
};

struct RhiOffset3D {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
};

struct RhiExtent3D {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

struct RhiTextureCopy {
    RhiTextureHandle src;
    RhiTextureHandle dst;
    RhiTextureSubresourceLayers srcSubresource;
    RhiOffset3D srcOffset;
    RhiTextureSubresourceLayers dstSubresource;
    RhiOffset3D dstOffset;
    RhiExtent3D extent;
};

struct RhiTextureBlit {
    RhiTextureHandle src;
    RhiTextureHandle dst;
    RhiTextureViewHandle srcView;
    RhiTextureViewHandle dstView;
    uint32_t srcMipLevel = 0;
    uint32_t dstMipLevel = 0;
};

#endif // MECRAFT_RHI_RESOURCES_H
