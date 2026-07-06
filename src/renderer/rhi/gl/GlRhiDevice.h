#ifndef MECRAFT_GL_RHI_DEVICE_H
#define MECRAFT_GL_RHI_DEVICE_H

#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiHandleAllocator.h"

class GlRhiCommandList final : public RhiCommandList {
public:
    void beginDebugLabel(const char* name, const glm::vec4& color) override;
    void endDebugLabel() override;
    void textureBarrier(const RhiTextureBarrier& barrier) override;
    void bufferBarrier(const RhiBufferBarrier& barrier) override;
    void beginRendering(const RhiRenderingInfo& info) override;
    void endRendering() override;
    void setViewport(const RhiViewport& viewport) override;
    void setScissor(const RhiRect2D& rect) override;
    void setGraphicsPipeline(RhiPipelineHandle pipeline) override;
    void setComputePipeline(RhiPipelineHandle pipeline) override;
    void setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) override;
    void setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) override;
    void setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) override;
    void pushConstants(const void* data, size_t size, RhiShaderStageFlags stages) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                      uint32_t drawCount, uint32_t stride) override;
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void copyBuffer(const RhiBufferCopy& copy) override;
    void copyBufferToTexture(const RhiBufferTextureCopy& copy) override;
    void copyTexture(const RhiTextureCopy& copy) override;
    void blitTexture(const RhiTextureBlit& blit) override;
    void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) override;
};

class GlRhiDevice final : public RhiDevice {
public:
    bool init(const RhiDeviceDesc& desc) override;
    void shutdown() override;

    [[nodiscard]] RhiBackend backend() const override;
    [[nodiscard]] const RhiCapabilities& capabilities() const override;

    RhiBufferHandle createBuffer(const RhiBufferDesc& desc,
                                 const void* initialData,
                                 size_t initialDataSize) override;
    RhiTextureHandle createTexture(const RhiTextureDesc& desc,
                                   const RhiTextureInitialData* initialData) override;
    RhiTextureViewHandle createTextureView(const RhiTextureViewDesc& desc) override;
    RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) override;
    RhiShaderHandle createShader(const RhiShaderDesc& desc) override;
    RhiBindGroupLayoutHandle createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) override;
    RhiPipelineLayoutHandle createPipelineLayout(const RhiPipelineLayoutDesc& desc) override;
    RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override;
    RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc& desc) override;
    RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) override;

    void destroyBuffer(RhiBufferHandle handle) override;
    void destroyTexture(RhiTextureHandle handle) override;
    void destroyTextureView(RhiTextureViewHandle handle) override;
    void destroySampler(RhiSamplerHandle handle) override;
    void destroyShader(RhiShaderHandle handle) override;
    void destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) override;
    void destroyPipelineLayout(RhiPipelineLayoutHandle handle) override;
    void destroyPipeline(RhiPipelineHandle handle) override;
    void destroyBindGroup(RhiBindGroupHandle handle) override;

    RhiCommandList& beginFrame() override;
    void submitFrame(RhiCommandList& commandList) override;
    void present() override;

private:
    bool m_initialized = false;
    RhiCapabilities m_capabilities{};
    GlRhiCommandList m_commandList;
    RhiHandleAllocator<RhiBufferHandle> m_buffers;
    RhiHandleAllocator<RhiTextureHandle> m_textures;
    RhiHandleAllocator<RhiTextureViewHandle> m_textureViews;
    RhiHandleAllocator<RhiSamplerHandle> m_samplers;
    RhiHandleAllocator<RhiShaderHandle> m_shaders;
    RhiHandleAllocator<RhiBindGroupLayoutHandle> m_bindGroupLayouts;
    RhiHandleAllocator<RhiPipelineLayoutHandle> m_pipelineLayouts;
    RhiHandleAllocator<RhiPipelineHandle> m_pipelines;
    RhiHandleAllocator<RhiBindGroupHandle> m_bindGroups;
};

#endif // MECRAFT_GL_RHI_DEVICE_H
