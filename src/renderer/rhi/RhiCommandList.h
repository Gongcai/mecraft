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

    virtual bool begin(const RhiCommandListDesc& desc) = 0;
    virtual bool end() = 0;
    [[nodiscard]] virtual RhiCommandListState state() const = 0;

    virtual void beginDebugLabel(const char* name, const glm::vec4& color) = 0;
    virtual void endDebugLabel() = 0;
    virtual void insertDebugMarker(const char* name, const glm::vec4& color) = 0;

    virtual void textureBarrier(const RhiTextureBarrier& barrier) = 0;
    virtual void bufferBarrier(const RhiBufferBarrier& barrier) = 0;
    virtual bool accelerationStructureBarrier(const RhiAccelerationStructureBarrier& barrier) = 0;

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

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                             uint32_t firstInstance) = 0;
    virtual void drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    virtual void updateBuffer(RhiBufferHandle buffer, uint64_t offset, const void* data, size_t size) = 0;
    virtual void copyBuffer(const RhiBufferCopy& copy) = 0;
    virtual void copyBufferToTexture(const RhiBufferTextureCopy& copy) = 0;
    virtual void copyTextureToBuffer(const RhiTextureBufferCopy& copy) = 0;
    virtual void copyTexture(const RhiTextureCopy& copy) = 0;
    virtual void blitTexture(const RhiTextureBlit& blit) = 0;
    // Generates every lower mip level while preserving TransferDst as the externally tracked state
    // for all subresources. Explicit backends perform the required per-level transfer transitions
    // inside this command and restore TransferDst before subsequent recorded commands execute.
    virtual void generateMipmaps(RhiTextureHandle texture) = 0;

    virtual void resetQueryPool(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount) = 0;
    virtual void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) = 0;

    /// Atomically records one or more acceleration-structure build or update operations.
    /// @param builds Complete build array validated before any native command is emitted.
    /// @param buildCount Number of builds in the array.
    /// @return True when every build was accepted and recorded.
    virtual bool buildAccelerationStructures(const RhiAccelerationStructureBuildDesc* builds, uint32_t buildCount) = 0;

    /// Atomically records one or more opacity micromap build operations.
    virtual bool buildMicromaps(const RhiMicromapBuildDesc* builds, uint32_t buildCount) = 0;

    /// Records a clone or compact copy between compatible acceleration structures.
    virtual bool copyAccelerationStructure(const RhiAccelerationStructureCopyDesc& copy) = 0;

    /// Writes compacted acceleration-structure sizes into a compatible query pool.
    virtual bool writeAccelerationStructureProperties(const RhiAccelerationStructurePropertyQueryDesc& query) = 0;
};

#endif // MECRAFT_RHI_COMMAND_LIST_H
