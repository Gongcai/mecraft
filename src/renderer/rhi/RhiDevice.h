#ifndef MECRAFT_RHI_DEVICE_H
#define MECRAFT_RHI_DEVICE_H

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiPipeline.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstddef>

class RhiDevice {
public:
    virtual ~RhiDevice() = default;

    virtual bool init(const RhiDeviceDesc& desc) = 0;
    virtual void shutdown() = 0;

    [[nodiscard]] virtual RhiBackend backend() const = 0;
    [[nodiscard]] virtual const RhiCapabilities& capabilities() const = 0;

    virtual RhiBufferHandle createBuffer(const RhiBufferDesc& desc,
                                         const void* initialData,
                                         size_t initialDataSize) = 0;
    virtual RhiTextureHandle createTexture(const RhiTextureDesc& desc,
                                           const RhiTextureInitialData* initialData) = 0;
    virtual RhiTextureViewHandle createTextureView(const RhiTextureViewDesc& desc) = 0;
    virtual RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) = 0;
    virtual RhiShaderHandle createShader(const RhiShaderDesc& desc) = 0;
    virtual RhiBindGroupLayoutHandle createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) = 0;
    virtual RhiPipelineLayoutHandle createPipelineLayout(const RhiPipelineLayoutDesc& desc) = 0;
    virtual RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) = 0;
    virtual RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc& desc) = 0;
    virtual RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) = 0;

    [[nodiscard]] virtual RhiTextureViewHandle currentSwapchainColorView() const = 0;
    [[nodiscard]] virtual RhiTextureFormat swapchainColorFormat() const = 0;
    virtual bool resizeSwapchain(uint32_t width, uint32_t height) = 0;

    virtual void destroyBuffer(RhiBufferHandle handle) = 0;
    virtual void destroyTexture(RhiTextureHandle handle) = 0;
    virtual void destroyTextureView(RhiTextureViewHandle handle) = 0;
    virtual void destroySampler(RhiSamplerHandle handle) = 0;
    virtual void destroyShader(RhiShaderHandle handle) = 0;
    virtual void destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) = 0;
    virtual void destroyPipelineLayout(RhiPipelineLayoutHandle handle) = 0;
    virtual void destroyPipeline(RhiPipelineHandle handle) = 0;
    virtual void destroyBindGroup(RhiBindGroupHandle handle) = 0;

    virtual RhiCommandList& beginFrame() = 0;
    virtual void submitFrame(RhiCommandList& commandList) = 0;
    virtual void present() = 0;
};

#endif // MECRAFT_RHI_DEVICE_H
