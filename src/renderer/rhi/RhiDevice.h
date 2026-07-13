#ifndef MECRAFT_RHI_DEVICE_H
#define MECRAFT_RHI_DEVICE_H

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiPipeline.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <memory>

class RhiDevice {
public:
    virtual ~RhiDevice() = default;

    virtual bool prepareWindowCreation() = 0;
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
    virtual RhiQueryPoolHandle createQueryPool(const RhiQueryPoolDesc& desc) = 0;
    virtual void* mapBuffer(RhiBufferHandle buffer, uint64_t offset, uint64_t size) = 0;
    virtual void unmapBuffer(RhiBufferHandle buffer) = 0;
    [[nodiscard]] virtual bool areQueryResultsAvailable(RhiQueryPoolHandle pool,
                                                        uint32_t firstQuery,
                                                        uint32_t queryCount) const = 0;
    // Timestamp query results are returned in nanoseconds on every backend.
    virtual bool getQueryResults(RhiQueryPoolHandle pool, uint32_t firstQuery,
                                 uint32_t queryCount, uint64_t* results) const = 0;

    /// Returns the color view for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureViewHandle currentSwapchainColorView() const = 0;
    /// Returns the depth-stencil view for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureViewHandle currentSwapchainDepthStencilView() const = 0;
    /// Returns the color texture for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureHandle currentSwapchainColorTexture() const = 0;
    [[nodiscard]] virtual RhiTextureFormat swapchainColorFormat() const = 0;
    [[nodiscard]] virtual RhiTextureFormat swapchainDepthStencilFormat() const = 0;
    virtual bool resizeSwapchain(uint32_t width, uint32_t height) = 0;

    /// Acquires exactly one presentation image and opens its frame lifetime.
    /// @return Frame status, stable frame identity, extent, and acquired image handles.
    [[nodiscard]] virtual RhiFrameAcquireResult acquireFrame() = 0;

    /// Presents the currently acquired image and closes its frame lifetime.
    /// @param info Frame and image indices returned by the matching acquireFrame call.
    /// @return Presentation status reported by the backend or window system.
    virtual RhiFrameStatus presentFrame(const RhiPresentInfo& info) = 0;

    // Destruction invalidates the public handle immediately. Backends with deferred
    // command execution must retain the native resource until recorded work completes.
    virtual void destroyBuffer(RhiBufferHandle handle) = 0;
    virtual void destroyTexture(RhiTextureHandle handle) = 0;
    virtual void destroyTextureView(RhiTextureViewHandle handle) = 0;
    virtual void destroySampler(RhiSamplerHandle handle) = 0;
    virtual void destroyShader(RhiShaderHandle handle) = 0;
    virtual void destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) = 0;
    virtual void destroyPipelineLayout(RhiPipelineLayoutHandle handle) = 0;
    virtual void destroyPipeline(RhiPipelineHandle handle) = 0;
    virtual void destroyBindGroup(RhiBindGroupHandle handle) = 0;
    virtual void destroyQueryPool(RhiQueryPoolHandle handle) = 0;


    [[nodiscard]] virtual std::unique_ptr<RhiCommandListPool> createCommandListPool(
        const RhiCommandListPoolDesc& desc) = 0;

    /// Submits executable command lists to the device queue in array order.
    /// @param info Command lists and diagnostic name for this queue submission.
    /// @param completionToken Optional destination for the submission completion identity.
    /// @return True when the complete submission was accepted atomically.
    virtual bool submit(const RhiSubmitInfo& info,
                        RhiSubmissionToken* completionToken = nullptr) = 0;

    /// Queries one submission without blocking the calling thread.
    /// @param token Token returned by this device instance.
    /// @param complete Receives true only after every command in the submission has completed.
    /// @return False when the token or calling thread violates the device contract.
    [[nodiscard]] virtual bool isSubmissionComplete(RhiSubmissionToken token,
                                                    bool& complete) = 0;

    /// Blocks until one submission and every earlier queue submission have completed.
    /// @param token Token returned by this device instance.
    /// @return False when the token is invalid, foreign, or cannot be waited successfully.
    virtual bool waitForSubmission(RhiSubmissionToken token) = 0;
    virtual void waitIdle() = 0;
};

#endif // MECRAFT_RHI_DEVICE_H
