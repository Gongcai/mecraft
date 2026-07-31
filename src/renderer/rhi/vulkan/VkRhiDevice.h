#ifndef MECRAFT_VK_RHI_DEVICE_H
#define MECRAFT_VK_RHI_DEVICE_H

#include "renderer/rhi/RhiDevice.h"

#include <memory>
#include <thread>

class VkRhiCommandList;
class VkRhiCommandListPool;
class VkRhiInterop;
struct VkRhiDeviceData;

class VkRhiDevice final : public RhiDevice {
public:
    VkRhiDevice();
    ~VkRhiDevice() override;

    bool prepareWindowCreation() override;
    bool init(const RhiDeviceDesc& desc) override;
    void shutdown() override;
    [[nodiscard]] RhiBackend backend() const override;
    [[nodiscard]] const RhiCapabilities& capabilities() const override;
    [[nodiscard]] RhiMemoryStats memoryStats() const override;

    RhiBufferHandle createBuffer(const RhiBufferDesc& desc, const void* initialData, size_t initialDataSize) override;
    RhiTextureHandle createTexture(const RhiTextureDesc& desc, const RhiTextureInitialData* initialData) override;
    [[nodiscard]] bool getBufferDesc(RhiBufferHandle buffer, RhiBufferDesc& desc) const override;
    [[nodiscard]] bool getTextureDesc(RhiTextureHandle texture, RhiTextureDesc& desc) const override;
    [[nodiscard]] bool getTextureMemoryRequirements(const RhiTextureDesc& desc,
                                                    RhiTextureMemoryRequirements& requirements) override;
    RhiMemoryHandle allocateTextureMemory(const RhiTextureMemoryRequirements& requirements, RhiMemoryCategory category,
                                          const char* debugName) override;
    RhiTextureHandle createPlacedTexture(const RhiTextureDesc& desc, RhiMemoryHandle memory) override;
    void destroyTextureMemory(RhiMemoryHandle handle) override;
    RhiTextureViewHandle createTextureView(const RhiTextureViewDesc& desc) override;
    RhiSamplerHandle createSampler(const RhiSamplerDesc& desc) override;
    RhiShaderHandle createShader(const RhiShaderDesc& desc) override;
    RhiBindGroupLayoutHandle createBindGroupLayout(const RhiBindGroupLayoutDesc& desc) override;
    RhiPipelineLayoutHandle createPipelineLayout(const RhiPipelineLayoutDesc& desc) override;
    RhiPipelineHandle createGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override;
    RhiPipelineHandle createComputePipeline(const RhiComputePipelineDesc& desc) override;
    RhiBindGroupHandle createBindGroup(const RhiBindGroupDesc& desc) override;
    RhiQueryPoolHandle createQueryPool(const RhiQueryPoolDesc& desc) override;
    bool resetQueryPool(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount) override;
    void* mapBuffer(RhiBufferHandle buffer, uint64_t offset, uint64_t size) override;
    void unmapBuffer(RhiBufferHandle buffer) override;
    [[nodiscard]] bool areQueryResultsAvailable(RhiQueryPoolHandle pool, uint32_t firstQuery,
                                                uint32_t queryCount) const override;
    bool getQueryResults(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount,
                         uint64_t* results) const override;

    [[nodiscard]] RhiTextureViewHandle currentSwapchainColorView() const override;
    [[nodiscard]] RhiTextureViewHandle currentSwapchainDepthStencilView() const override;
    [[nodiscard]] RhiTextureHandle currentSwapchainColorTexture() const override;
    [[nodiscard]] RhiTextureFormat swapchainColorFormat() const override;
    [[nodiscard]] RhiTextureFormat swapchainDepthStencilFormat() const override;
    [[nodiscard]] bool vsyncEnabled() const override;
    bool setVsyncEnabled(bool enabled) override;
    bool resizeSwapchain(uint32_t width, uint32_t height) override;
    [[nodiscard]] RhiFrameAcquireResult acquireFrame() override;
    RhiFrameStatus presentFrame(const RhiPresentInfo& info) override;
    bool cancelFrame(const RhiPresentInfo& info) override;

    void destroyBuffer(RhiBufferHandle handle) override;
    void destroyTexture(RhiTextureHandle handle) override;
    void destroyTextureView(RhiTextureViewHandle handle) override;
    void destroySampler(RhiSamplerHandle handle) override;
    void destroyShader(RhiShaderHandle handle) override;
    void destroyBindGroupLayout(RhiBindGroupLayoutHandle handle) override;
    void destroyPipelineLayout(RhiPipelineLayoutHandle handle) override;
    void destroyPipeline(RhiPipelineHandle handle) override;
    void destroyBindGroup(RhiBindGroupHandle handle) override;
    void destroyQueryPool(RhiQueryPoolHandle handle) override;

    [[nodiscard]] std::unique_ptr<RhiCommandListPool>
    createCommandListPool(const RhiCommandListPoolDesc& desc) override;
    bool submit(const RhiSubmitInfo& info, RhiSubmissionToken* completionToken = nullptr) override;
    [[nodiscard]] bool isSubmissionComplete(RhiSubmissionToken token, bool& complete) override;
    bool waitForSubmission(RhiSubmissionToken token) override;
    void waitIdle() override;

    /// Returns the number of validation warnings and errors observed by this device.
    /// @return Monotonic diagnostic count accumulated by the Vulkan callback.
    [[nodiscard]] uint64_t validationErrorCount() const;

private:
    friend class VkRhiCommandList;
    friend class VkRhiCommandListPool;
    friend class VkRhiInterop;

    [[nodiscard]] bool validateSubmissionToken(RhiSubmissionToken token) const;
    [[nodiscard]] RhiFrameStatus cancelAcquiredFrame(const RhiPresentInfo& info);
    void refreshSwapchainCapabilities();
    void reclaimCompletedWork();
    void reclaimCompletedWorkUnlocked();

    bool m_initialized = false;
    RhiCapabilities m_capabilities{};
    std::thread::id m_deviceThread;
    uint64_t m_deviceId = 0u;
    uint64_t m_lastSubmittedSequence = 0u;
    std::unique_ptr<VkRhiDeviceData> m_data;
};

#endif // MECRAFT_VK_RHI_DEVICE_H
