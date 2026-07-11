#ifndef MECRAFT_RHI_COMMAND_LIST_H
#define MECRAFT_RHI_COMMAND_LIST_H

#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiPipeline.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

class RhiCommandList {
public:
    virtual ~RhiCommandList() = default;

    virtual void beginDebugLabel(const char* name, const glm::vec4& color) = 0;
    virtual void endDebugLabel() = 0;
    virtual void insertDebugMarker(const char* name, const glm::vec4& color) = 0;

    virtual void textureBarrier(const RhiTextureBarrier& barrier) = 0;
    virtual void bufferBarrier(const RhiBufferBarrier& barrier) = 0;

    virtual void beginRendering(const RhiRenderingInfo& info) = 0;
    virtual void endRendering() = 0;
    virtual void clearDepthAttachment(float depth, const RhiRect2D& rect) = 0;

    virtual void setViewport(const RhiViewport& viewport) = 0;
    virtual void setScissor(const RhiRect2D& rect) = 0;
    virtual void setGraphicsPipeline(RhiPipelineHandle pipeline) = 0;
    virtual void setComputePipeline(RhiPipelineHandle pipeline) = 0;
    virtual void setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) = 0;
    virtual void setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) = 0;
    virtual void setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) = 0;
    virtual void pushConstants(const void* data, size_t size, RhiShaderStageFlags stages) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount,
                      uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                             uint32_t firstIndex, int32_t vertexOffset,
                             uint32_t firstInstance) = 0;
    virtual void drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                              uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    virtual void updateBuffer(RhiBufferHandle buffer, uint64_t offset,
                              const void* data, size_t size) = 0;
    virtual void copyBuffer(const RhiBufferCopy& copy) = 0;
    virtual void copyBufferToTexture(const RhiBufferTextureCopy& copy) = 0;
    virtual void copyTexture(const RhiTextureCopy& copy) = 0;
    virtual void blitTexture(const RhiTextureBlit& blit) = 0;
    virtual void generateMipmaps(RhiTextureHandle texture) = 0;

    virtual void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) = 0;
};

#endif // MECRAFT_RHI_COMMAND_LIST_H
