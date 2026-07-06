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
    CombinedTextureSampler
};

struct RhiBindGroupLayoutEntry {
    uint32_t binding = 0;
    RhiBindingType type = RhiBindingType::UniformBuffer;
    RhiShaderStageFlags stages = 0;
    uint32_t arrayCount = 1;
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
};

struct RhiBindGroupEntry {
    uint32_t binding = 0;
    RhiBindingResource resource;
};

struct RhiBindGroupDesc {
    RhiBindGroupLayoutHandle layout;
    std::vector<RhiBindGroupEntry> entries;
};

#endif // MECRAFT_RHI_DESCRIPTOR_H
