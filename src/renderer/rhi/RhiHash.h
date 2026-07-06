#ifndef MECRAFT_RHI_HASH_H
#define MECRAFT_RHI_HASH_H

#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiPipeline.h"
#include "renderer/rhi/RhiResources.h"

#include <cstddef>
#include <cstdint>

[[nodiscard]] uint64_t rhiHashTextureDesc(const RhiTextureDesc& desc);
[[nodiscard]] uint64_t rhiHashBufferDesc(const RhiBufferDesc& desc);
[[nodiscard]] uint64_t rhiHashSamplerDesc(const RhiSamplerDesc& desc);
[[nodiscard]] uint64_t rhiHashBindGroupLayoutDesc(const RhiBindGroupLayoutDesc& desc);
[[nodiscard]] uint64_t rhiHashGraphicsPipelineDesc(const RhiGraphicsPipelineDesc& desc);

#endif // MECRAFT_RHI_HASH_H
