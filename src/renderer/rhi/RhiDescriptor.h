#ifndef MECRAFT_RHI_DESCRIPTOR_H
#define MECRAFT_RHI_DESCRIPTOR_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <vector>

enum class RhiBindingType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    CombinedTextureSampler,
    AccelerationStructure
};

enum class RhiBindingFlag : uint32_t {
    PartiallyBound = 1u << 0u,
    UpdateAfterBind = 1u << 1u,
    UpdateUnusedWhilePending = 1u << 2u,
    VariableDescriptorCount = 1u << 3u
};

using RhiBindingFlags = uint32_t;

[[nodiscard]] constexpr RhiBindingFlags rhiFlag(const RhiBindingFlag flag) {
    return static_cast<RhiBindingFlags>(flag);
}

[[nodiscard]] constexpr bool rhiHasBindingFlag(const RhiBindingFlags flags, const RhiBindingFlag flag) {
    return (flags & rhiFlag(flag)) != 0u;
}

struct RhiBindGroupLayoutEntry {
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    RhiShaderStageFlags stages = 0;
    uint32_t arrayCount = 1;
    RhiBindingFlags flags = 0u;
};

struct RhiBindGroupLayoutDesc {
    const char* debugName = nullptr;
    std::vector<RhiBindGroupLayoutEntry> entries;
};

struct RhiBufferBinding {
    RhiBufferHandle buffer;
    uint64_t offset = 0;
    uint64_t range = 0;
};

struct RhiTextureSamplerBinding {
    RhiTextureViewHandle textureView;
    RhiSamplerHandle sampler;
};

struct RhiBindingResource {
    RhiBufferBinding buffer;
    RhiTextureViewHandle textureView;
    RhiSamplerHandle sampler;
    RhiTextureSamplerBinding combinedTextureSampler;
    RhiAccelerationStructureHandle accelerationStructure;
};

struct RhiBindGroupEntry {
    uint32_t binding = 0;
    uint32_t arrayElement = 0;
    RhiBindingResource resource;
};

struct RhiBindGroupDesc {
    RhiBindGroupLayoutHandle layout;
    uint32_t variableDescriptorCount = 0u;
    std::vector<RhiBindGroupEntry> entries;
};

struct RhiBindGroupUpdate {
    RhiBindGroupHandle bindGroup;
    uint32_t binding = 0u;
    uint32_t firstArrayElement = 0u;
    const RhiBindingResource* resources = nullptr;
    uint32_t resourceCount = 0u;
};

#endif // MECRAFT_RHI_DESCRIPTOR_H
