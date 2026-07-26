#ifndef MECRAFT_RHI_RESOURCES_H
#define MECRAFT_RHI_RESOURCES_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <limits>

inline constexpr uint32_t kRhiRemainingMipLevels = 0xFFFFFFFFu;
inline constexpr uint32_t kRhiRemainingArrayLayers = 0xFFFFFFFFu;
inline constexpr uint64_t kRhiWholeSize = std::numeric_limits<uint64_t>::max();
inline constexpr uint32_t kRhiQueueFamilyIgnored = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kRhiQueueFamilyExternal = kRhiQueueFamilyIgnored - 1u;

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
};

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

struct RhiColorAttachment {
    RhiTextureViewHandle view;
    RhiLoadOp loadOp = RhiLoadOp::Load;
    RhiStoreOp storeOp = RhiStoreOp::Store;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
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
