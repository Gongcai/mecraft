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
    /// Captures all live public RHI resources and their backing allocations.
    /// @return Backend snapshot with exact or explicitly estimated byte totals.
    [[nodiscard]] virtual RhiMemoryStats memoryStats() const = 0;

    virtual RhiBufferHandle createBuffer(const RhiBufferDesc& desc, const void* initialData,
                                         size_t initialDataSize) = 0;
    virtual RhiTextureHandle createTexture(const RhiTextureDesc& desc, const RhiTextureInitialData* initialData) = 0;

    /// Returns the immutable creation description for a live buffer.
    /// @param buffer Buffer handle owned by this device.
    /// @param desc Receives the exact description used to create the buffer.
    /// @return True when the handle identifies a live buffer on this device.
    [[nodiscard]] virtual bool getBufferDesc(RhiBufferHandle buffer, RhiBufferDesc& desc) const = 0;

    /// Returns the immutable creation description for a live texture.
    /// @param texture Texture handle owned by this device.
    /// @param desc Receives the exact description used to create the texture.
    /// @return True when the handle identifies a live texture on this device.
    [[nodiscard]] virtual bool getTextureDesc(RhiTextureHandle texture, RhiTextureDesc& desc) const = 0;

    /// Returns the immutable creation description for a live texture view.
    /// @param textureView Texture-view handle owned by this device.
    /// @param desc Receives the exact description used to create the view.
    /// @return True when the handle identifies a live texture view on this device.
    [[nodiscard]] virtual bool getTextureViewDesc(RhiTextureViewHandle textureView, RhiTextureViewDesc& desc) const = 0;

    /// Returns the immutable creation description for a live sampler.
    /// @param sampler Sampler handle owned by this device.
    /// @param desc Receives the exact description used to create the sampler.
    /// @return True when the handle identifies a live sampler on this device.
    [[nodiscard]] virtual bool getSamplerDesc(RhiSamplerHandle sampler, RhiSamplerDesc& desc) const = 0;

    // --- Placed textures on shared memory (memory aliasing) ---
    // Optional feature gated by capabilities().textureAliasing. Backends that
    // do not expose placement report the feature as unsupported, and callers
    // select the non-aliasing resource path before invoking these methods.

    /// Queries the memory footprint a texture with this description would
    /// occupy when placed on a shared memory block.
    /// @param desc Texture description to measure; never creates the texture.
    /// @param requirements Receives size, alignment, and the type mask.
    /// @return False when the backend cannot place textures on shared memory.
    [[nodiscard]] virtual bool getTextureMemoryRequirements(const RhiTextureDesc& desc,
                                                            RhiTextureMemoryRequirements& requirements) {
        (void)desc;
        (void)requirements;
        return false;
    }

    /// Allocates a device-local memory block placed textures can share.
    /// @param requirements Size, alignment, and type mask the block satisfies;
    ///        callers pass the combined (max size/alignment, intersected mask)
    ///        requirements of every texture that will alias the block.
    /// @param category Explicit ownership category for the backing allocation.
    /// @param debugName Optional label for tooling.
    /// @return Invalid handle when aliasing is unsupported or allocation fails.
    virtual RhiMemoryHandle allocateTextureMemory(const RhiTextureMemoryRequirements& requirements,
                                                  RhiMemoryCategory category, const char* debugName) {
        (void)requirements;
        (void)category;
        (void)debugName;
        return {};
    }

    /// Creates a texture bound at offset zero of a shared memory block. The
    /// texture's contents are undefined whenever another texture aliasing the
    /// block was written since this texture's last write; callers must treat
    /// each reuse as an uninitialized resource.
    /// @param desc Texture description whose requirements fit the block.
    /// @param memory Block returned by allocateTextureMemory on this device.
    /// @return Invalid handle when the block cannot host the description.
    virtual RhiTextureHandle createPlacedTexture(const RhiTextureDesc& desc, RhiMemoryHandle memory) {
        (void)desc;
        (void)memory;
        return {};
    }

    /// Destroys a shared memory block once submitted GPU work completes.
    /// Placed textures created on the block must be destroyed beforehand.
    virtual void destroyTextureMemory(RhiMemoryHandle handle) { (void)handle; }

    virtual RhiTextureViewHandle createTextureView(const RhiTextureViewDesc& desc) = 0;
    virtual RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) = 0;
    virtual RhiShaderHandle createShader(const RhiShaderDesc& desc) = 0;
    virtual RhiBindGroupLayoutHandle createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) = 0;
    virtual RhiPipelineLayoutHandle createPipelineLayout(const RhiPipelineLayoutDesc& desc) = 0;
    virtual RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) = 0;
    virtual RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc& desc) = 0;
    virtual RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) = 0;

    /// Creates an acceleration structure inside a caller-owned storage buffer.
    /// @param desc Type, backing-buffer range, and diagnostic label.
    /// @return Live handle, or an invalid handle when the complete description is rejected.
    virtual RhiAccelerationStructureHandle createAccelerationStructure(const RhiAccelerationStructureDesc& desc) = 0;

    /// Returns the immutable creation description for a live acceleration structure.
    /// @param accelerationStructure Handle owned by this device.
    /// @param desc Receives the exact description used to create the object.
    /// @return True when the handle identifies a live acceleration structure.
    [[nodiscard]] virtual bool getAccelerationStructureDesc(RhiAccelerationStructureHandle accelerationStructure,
                                                            RhiAccelerationStructureDesc& desc) const = 0;

    /// Queries exact storage, build-scratch, and update-scratch byte requirements.
    /// @param input Geometry and primitive ranges that will be built.
    /// @param sizes Receives native byte requirements when the complete input is valid.
    /// @return True when every geometry and range satisfies the backend contract.
    [[nodiscard]] virtual bool
    queryAccelerationStructureBuildSizes(const RhiAccelerationStructureBuildInput& input,
                                         RhiAccelerationStructureBuildSizes& sizes) const = 0;

    /// Returns the shader-visible address of a buffer created with DeviceAddress usage.
    /// @param buffer Live buffer owned by this device.
    /// @return Non-zero device address, or zero when the handle or usage is invalid.
    [[nodiscard]] virtual uint64_t bufferDeviceAddress(RhiBufferHandle buffer) const = 0;

    /// Returns the shader-visible address of a built acceleration structure.
    /// @param accelerationStructure Live acceleration structure owned by this device.
    /// @return Non-zero device address, or zero when the handle is invalid.
    [[nodiscard]] virtual uint64_t
    accelerationStructureDeviceAddress(RhiAccelerationStructureHandle accelerationStructure) const = 0;

    /// Atomically updates contiguous descriptor-array ranges across one or more bind groups.
    /// @param updates Array of destination ranges and replacement resources to validate.
    /// @param updateCount Number of range updates in the batch.
    /// @return True when every range and lifecycle constraint was accepted and applied.
    virtual bool updateBindGroups(const RhiBindGroupUpdate* updates, uint32_t updateCount) = 0;
    virtual RhiQueryPoolHandle createQueryPool(const RhiQueryPoolDesc& desc) = 0;

    /// Resets a contiguous query range before commands write new results.
    /// @param pool Query pool owned by this device.
    /// @param firstQuery Index of the first query to reset.
    /// @param queryCount Number of queries in the reset range.
    /// @return True when the complete range was reset on the device thread.
    virtual bool resetQueryPool(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount) = 0;
    virtual void* mapBuffer(RhiBufferHandle buffer, uint64_t offset, uint64_t size) = 0;
    virtual void unmapBuffer(RhiBufferHandle buffer) = 0;
    [[nodiscard]] virtual bool areQueryResultsAvailable(RhiQueryPoolHandle pool, uint32_t firstQuery,
                                                        uint32_t queryCount) const = 0;
    // Timestamp query results are returned in nanoseconds on every backend.
    virtual bool getQueryResults(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount,
                                 uint64_t* results) const = 0;

    /// Returns the color view for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureViewHandle currentSwapchainColorView() const = 0;
    /// Returns the depth-stencil view for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureViewHandle currentSwapchainDepthStencilView() const = 0;
    /// Returns the color texture for the acquired frame until that frame is presented.
    [[nodiscard]] virtual RhiTextureHandle currentSwapchainColorTexture() const = 0;
    [[nodiscard]] virtual RhiTextureFormat swapchainColorFormat() const = 0;
    [[nodiscard]] virtual RhiTextureFormat swapchainDepthStencilFormat() const = 0;
    [[nodiscard]] virtual bool vsyncEnabled() const = 0;
    virtual bool setVsyncEnabled(bool enabled) = 0;
    virtual bool resizeSwapchain(uint32_t width, uint32_t height) = 0;

    /// Acquires exactly one presentation image and opens its frame lifetime.
    /// @return Frame status, stable frame identity, extent, and acquired image handles.
    [[nodiscard]] virtual RhiFrameAcquireResult acquireFrame() = 0;

    /// Presents the currently acquired image and closes its frame lifetime.
    /// @param info Frame and image indices returned by the matching acquireFrame call.
    /// @return Presentation status reported by the backend or window system.
    virtual RhiFrameStatus presentFrame(const RhiPresentInfo& info) = 0;

    /// Cancels the currently acquired image without displaying it and closes its frame lifetime.
    /// @param info Frame and image indices returned by the matching acquireFrame call.
    /// @return True when submitted work completed and the presentation image was released.
    virtual bool cancelFrame(const RhiPresentInfo& info) = 0;

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
    virtual void destroyAccelerationStructure(RhiAccelerationStructureHandle handle) = 0;

    [[nodiscard]] virtual std::unique_ptr<RhiCommandListPool>
    createCommandListPool(const RhiCommandListPoolDesc& desc) = 0;

    /// Submits executable command lists to the device queue in array order.
    /// @param info Command lists and diagnostic name for this queue submission.
    /// @param completionToken Optional destination for the submission completion identity.
    /// @return True when the complete submission was accepted atomically.
    virtual bool submit(const RhiSubmitInfo& info, RhiSubmissionToken* completionToken = nullptr) = 0;

    /// Queries one submission without blocking the calling thread.
    /// @param token Token returned by this device instance.
    /// @param complete Receives true only after every command in the submission has completed.
    /// @return False when the token or calling thread violates the device contract.
    [[nodiscard]] virtual bool isSubmissionComplete(RhiSubmissionToken token, bool& complete) = 0;

    /// Blocks until one submission and every earlier queue submission have completed.
    /// @param token Token returned by this device instance.
    /// @return False when the token is invalid, foreign, or cannot be waited successfully.
    virtual bool waitForSubmission(RhiSubmissionToken token) = 0;
    virtual void waitIdle() = 0;
};

#endif // MECRAFT_RHI_DEVICE_H
