#include "renderer/rhi/gl/GlRhiDevice.h"

void GlRhiCommandList::beginDebugLabel(const char* name, const glm::vec4& color) {
    (void) name;
    (void) color;
}

void GlRhiCommandList::endDebugLabel() {}

void GlRhiCommandList::textureBarrier(const RhiTextureBarrier& barrier) {
    (void) barrier;
}

void GlRhiCommandList::bufferBarrier(const RhiBufferBarrier& barrier) {
    (void) barrier;
}

void GlRhiCommandList::beginRendering(const RhiRenderingInfo& info) {
    (void) info;
}

void GlRhiCommandList::endRendering() {}

void GlRhiCommandList::setViewport(const RhiViewport& viewport) {
    (void) viewport;
}

void GlRhiCommandList::setScissor(const RhiRect2D& rect) {
    (void) rect;
}

void GlRhiCommandList::setGraphicsPipeline(RhiPipelineHandle pipeline) {
    (void) pipeline;
}

void GlRhiCommandList::setComputePipeline(RhiPipelineHandle pipeline) {
    (void) pipeline;
}

void GlRhiCommandList::setBindGroup(uint32_t setIndex, RhiBindGroupHandle bindGroup) {
    (void) setIndex;
    (void) bindGroup;
}

void GlRhiCommandList::setVertexBuffer(uint32_t slot, RhiBufferHandle buffer, uint64_t offset) {
    (void) slot;
    (void) buffer;
    (void) offset;
}

void GlRhiCommandList::setIndexBuffer(RhiBufferHandle buffer, RhiIndexFormat format, uint64_t offset) {
    (void) buffer;
    (void) format;
    (void) offset;
}

void GlRhiCommandList::pushConstants(const void* data, size_t size, RhiShaderStageFlags stages) {
    (void) data;
    (void) size;
    (void) stages;
}

void GlRhiCommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    (void) vertexCount;
    (void) instanceCount;
    (void) firstVertex;
    (void) firstInstance;
}

void GlRhiCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    (void) indexCount;
    (void) instanceCount;
    (void) firstIndex;
    (void) vertexOffset;
    (void) firstInstance;
}

void GlRhiCommandList::drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset,
                                    uint32_t drawCount, uint32_t stride) {
    (void) indirectBuffer;
    (void) offset;
    (void) drawCount;
    (void) stride;
}

void GlRhiCommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    (void) groupCountX;
    (void) groupCountY;
    (void) groupCountZ;
}

void GlRhiCommandList::copyBuffer(const RhiBufferCopy& copy) {
    (void) copy;
}

void GlRhiCommandList::copyBufferToTexture(const RhiBufferTextureCopy& copy) {
    (void) copy;
}

void GlRhiCommandList::copyTexture(const RhiTextureCopy& copy) {
    (void) copy;
}

void GlRhiCommandList::blitTexture(const RhiTextureBlit& blit) {
    (void) blit;
}

void GlRhiCommandList::writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) {
    (void) pool;
    (void) queryIndex;
}

bool GlRhiDevice::init(const RhiDeviceDesc& desc) {
    (void) desc;
    m_initialized = true;
    m_capabilities.multiDrawIndirect = true;
    m_capabilities.timestampQuery = true;
    m_capabilities.textureView = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.storageImage = true;
    m_capabilities.maxColorAttachments = 8;
    m_capabilities.maxSampledTexturesPerStage = 32;
    return true;
}

void GlRhiDevice::shutdown() {
    m_bindGroups.clear();
    m_pipelines.clear();
    m_pipelineLayouts.clear();
    m_bindGroupLayouts.clear();
    m_shaders.clear();
    m_samplers.clear();
    m_textureViews.clear();
    m_textures.clear();
    m_buffers.clear();
    m_initialized = false;
}

RhiBackend GlRhiDevice::backend() const {
    return RhiBackend::OpenGL;
}

const RhiCapabilities& GlRhiDevice::capabilities() const {
    return m_capabilities;
}

RhiBufferHandle GlRhiDevice::createBuffer(const RhiBufferDesc& desc,
                                          const void* initialData,
                                          size_t initialDataSize) {
    (void) initialData;
    (void) initialDataSize;
    if (!m_initialized || desc.size == 0 || desc.usage == 0) {
        return {};
    }
    return m_buffers.allocate();
}

RhiTextureHandle GlRhiDevice::createTexture(const RhiTextureDesc& desc,
                                            const RhiTextureInitialData* initialData) {
    (void) initialData;
    if (!m_initialized || desc.width == 0 || desc.height == 0 ||
        desc.depthOrLayers == 0 || desc.mipLevels == 0 || desc.usage == 0) {
        return {};
    }
    return m_textures.allocate();
}

RhiTextureViewHandle GlRhiDevice::createTextureView(const RhiTextureViewDesc& desc) {
    if (!m_initialized || !desc.texture.isValid() || desc.mipCount == 0 || desc.layerCount == 0) {
        return {};
    }
    return m_textureViews.allocate();
}

RhiSamplerHandle GlRhiDevice::createSampler(const RhiSamplerDesc& desc) {
    (void) desc;
    if (!m_initialized) {
        return {};
    }
    return m_samplers.allocate();
}

RhiShaderHandle GlRhiDevice::createShader(const RhiShaderDesc& desc) {
    if (!m_initialized || ((desc.source == nullptr || desc.sourceSize == 0) &&
                           (desc.bytecode == nullptr || desc.bytecodeSize == 0))) {
        return {};
    }
    return m_shaders.allocate();
}

RhiBindGroupLayoutHandle GlRhiDevice::createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) {
    (void) desc;
    if (!m_initialized) {
        return {};
    }
    return m_bindGroupLayouts.allocate();
}

RhiPipelineLayoutHandle GlRhiDevice::createPipelineLayout(const RhiPipelineLayoutDesc& desc) {
    (void) desc;
    if (!m_initialized) {
        return {};
    }
    return m_pipelineLayouts.allocate();
}

RhiPipelineHandle GlRhiDevice::createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) {
    if (!m_initialized || !desc.vertexShader.isValid() || !desc.fragmentShader.isValid() ||
        !desc.layout.isValid()) {
        return {};
    }
    return m_pipelines.allocate();
}

RhiPipelineHandle GlRhiDevice::createComputePipeline(const RhiComputePipelineDesc& desc) {
    if (!m_initialized || !desc.computeShader.isValid() || !desc.layout.isValid()) {
        return {};
    }
    return m_pipelines.allocate();
}

RhiBindGroupHandle GlRhiDevice::createBindGroup(const RhiBindGroupDesc& desc) {
    if (!m_initialized || !desc.layout.isValid()) {
        return {};
    }
    return m_bindGroups.allocate();
}

void GlRhiDevice::destroyBuffer(RhiBufferHandle handle) {
    (void) m_buffers.release(handle);
}

void GlRhiDevice::destroyTexture(RhiTextureHandle handle) {
    (void) m_textures.release(handle);
}

void GlRhiDevice::destroyTextureView(RhiTextureViewHandle handle) {
    (void) m_textureViews.release(handle);
}

void GlRhiDevice::destroySampler(RhiSamplerHandle handle) {
    (void) m_samplers.release(handle);
}

void GlRhiDevice::destroyShader(RhiShaderHandle handle) {
    (void) m_shaders.release(handle);
}

void GlRhiDevice::destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) {
    (void) m_bindGroupLayouts.release(handle);
}

void GlRhiDevice::destroyPipelineLayout(RhiPipelineLayoutHandle handle) {
    (void) m_pipelineLayouts.release(handle);
}

void GlRhiDevice::destroyPipeline(RhiPipelineHandle handle) {
    (void) m_pipelines.release(handle);
}

void GlRhiDevice::destroyBindGroup(RhiBindGroupHandle handle) {
    (void) m_bindGroups.release(handle);
}

RhiCommandList& GlRhiDevice::beginFrame() {
    return m_commandList;
}

void GlRhiDevice::submitFrame(RhiCommandList& commandList) {
    (void) commandList;
}

void GlRhiDevice::present() {}
