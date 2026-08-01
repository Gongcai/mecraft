#include "renderer/rhi/RhiHash.h"

#include <cstring>

namespace {
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void rhiHashCombine(uint64_t& hash, uint64_t value) {
    for (uint32_t byteIndex = 0; byteIndex < 8; ++byteIndex) {
        hash ^= (value >> (byteIndex * 8u)) & 0xffu;
        hash *= kFnvPrime;
    }
}

template <typename Enum> void rhiHashEnum(uint64_t& hash, Enum value) {
    rhiHashCombine(hash, static_cast<uint64_t>(value));
}

uint32_t floatBits(const float value) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
} // namespace

uint64_t rhiHashTextureDesc(const RhiTextureDesc& desc) {
    uint64_t hash = kFnvOffset;
    rhiHashEnum(hash, desc.dimension);
    rhiHashEnum(hash, desc.format);
    rhiHashCombine(hash, desc.width);
    rhiHashCombine(hash, desc.height);
    rhiHashCombine(hash, desc.depthOrLayers);
    rhiHashCombine(hash, desc.mipLevels);
    rhiHashCombine(hash, desc.sampleCount);
    rhiHashCombine(hash, desc.usage);
    rhiHashEnum(hash, desc.memoryCategory);
    return hash;
}

uint64_t rhiHashBufferDesc(const RhiBufferDesc& desc) {
    uint64_t hash = kFnvOffset;
    rhiHashCombine(hash, desc.size);
    rhiHashCombine(hash, desc.usage);
    rhiHashEnum(hash, desc.memoryUsage);
    rhiHashEnum(hash, desc.initialState);
    rhiHashEnum(hash, desc.memoryCategory);
    return hash;
}

uint64_t rhiHashSamplerDesc(const RhiSamplerDesc& desc) {
    uint64_t hash = kFnvOffset;
    rhiHashEnum(hash, desc.minFilter);
    rhiHashEnum(hash, desc.magFilter);
    rhiHashEnum(hash, desc.mipmapMode);
    rhiHashEnum(hash, desc.addressU);
    rhiHashEnum(hash, desc.addressV);
    rhiHashEnum(hash, desc.addressW);
    rhiHashCombine(hash, static_cast<uint64_t>(desc.maxAnisotropy * 1024.0f));
    rhiHashCombine(hash, desc.compareEnabled ? 1u : 0u);
    rhiHashEnum(hash, desc.compareOp);
    return hash;
}

uint64_t rhiHashBindGroupLayoutDesc(const RhiBindGroupLayoutDesc& desc) {
    uint64_t hash = kFnvOffset;
    rhiHashCombine(hash, desc.entries.size());
    for (const RhiBindGroupLayoutEntry& entry : desc.entries) {
        rhiHashCombine(hash, entry.binding);
        rhiHashEnum(hash, entry.type);
        rhiHashCombine(hash, entry.stages);
        rhiHashCombine(hash, entry.arrayCount);
        rhiHashCombine(hash, entry.flags);
    }
    return hash;
}

uint64_t rhiHashGraphicsPipelineDesc(const RhiGraphicsPipelineDesc& desc) {
    uint64_t hash = kFnvOffset;
    rhiHashCombine(hash, desc.vertexShader.index);
    rhiHashCombine(hash, desc.vertexShader.generation);
    rhiHashCombine(hash, desc.fragmentShader.index);
    rhiHashCombine(hash, desc.fragmentShader.generation);
    rhiHashCombine(hash, desc.layout.index);
    rhiHashCombine(hash, desc.layout.generation);
    rhiHashEnum(hash, desc.topology);
    rhiHashEnum(hash, desc.raster.cullMode);
    rhiHashEnum(hash, desc.raster.frontFace);
    rhiHashCombine(hash, floatBits(desc.raster.depthBiasConstantFactor));
    rhiHashCombine(hash, floatBits(desc.raster.depthBiasSlopeFactor));
    rhiHashCombine(hash, desc.raster.depthClampEnabled ? 1u : 0u);
    rhiHashCombine(hash, desc.raster.depthBiasEnabled ? 1u : 0u);
    rhiHashCombine(hash, desc.raster.scissorEnabled ? 1u : 0u);
    rhiHashCombine(hash, desc.depthStencil.depthTestEnabled ? 1u : 0u);
    rhiHashCombine(hash, desc.depthStencil.depthWriteEnabled ? 1u : 0u);
    rhiHashEnum(hash, desc.depthStencil.depthCompare);
    rhiHashCombine(hash, desc.colorFormats.size());
    for (const RhiTextureFormat format : desc.colorFormats) {
        rhiHashEnum(hash, format);
    }
    rhiHashEnum(hash, desc.depthFormat);
    return hash;
}
