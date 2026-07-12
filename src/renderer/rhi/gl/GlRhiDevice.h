#ifndef MECRAFT_GL_RHI_DEVICE_H
#define MECRAFT_GL_RHI_DEVICE_H

#include "renderer/rhi/RhiDevice.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

struct GlRhiCommandResourceReferences;
struct GlRhiCommandPoolRegistry;
class GlRhiCommandListPool;

class GlRhiCommandList final : public RhiCommandList {
public:
    GlRhiCommandList();
    ~GlRhiCommandList() override;

    bool begin(const RhiCommandListDesc& desc) override;
    bool end() override;
    [[nodiscard]] RhiCommandListState state() const override;

    void beginDebugLabel(const char* name, const glm::vec4& color) override;
    void endDebugLabel() override;
    void insertDebugMarker(const char* name, const glm::vec4& color) override;
    void textureBarrier(const RhiTextureBarrier& barrier) override;
    void bufferBarrier(const RhiBufferBarrier& barrier) override;
    void beginRendering(const RhiRenderingInfo& info) override;
    void endRendering() override;
    void clearDepthAttachment(float depth, const RhiRect2D& rect) override;
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
    void updateBuffer(RhiBufferHandle buffer, uint64_t offset,
                      const void* data, size_t size) override;
    void copyBuffer(const RhiBufferCopy& copy) override;
    void copyBufferToTexture(const RhiBufferTextureCopy& copy) override;
    void copyTextureToBuffer(const RhiTextureBufferCopy& copy) override;
    void copyTexture(const RhiTextureCopy& copy) override;
    void blitTexture(const RhiTextureBlit& blit) override;
    void generateMipmaps(RhiTextureHandle texture) override;
    void resetQueryPool(RhiQueryPoolHandle pool,
                        uint32_t firstQuery,
                        uint32_t queryCount) override;
    void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) override;

private:
    friend class GlRhiDevice;
    friend class GlRhiCommandListPool;

    enum class CommandType : uint8_t {
        BeginDebugLabel,
        EndDebugLabel,
        InsertDebugMarker,
        TextureBarrier,
        BufferBarrier,
        BeginRendering,
        EndRendering,
        ClearDepthAttachment,
        SetViewport,
        SetScissor,
        SetGraphicsPipeline,
        SetComputePipeline,
        SetBindGroup,
        SetVertexBuffer,
        SetIndexBuffer,
        PushConstants,
        Draw,
        DrawIndexed,
        DrawIndirect,
        Dispatch,
        UpdateBuffer,
        CopyBuffer,
        CopyBufferToTexture,
        CopyTextureToBuffer,
        CopyTexture,
        BlitTexture,
        GenerateMipmaps,
        ResetQueryPool,
        WriteTimestamp
    };

    struct VertexBufferBindingState {
        RhiBufferHandle buffer;
        uint64_t offset = 0u;
        bool valid = false;
    };

    struct IndexBufferBindingState {
        RhiBufferHandle buffer;
        RhiIndexFormat format = RhiIndexFormat::Uint32;
        uint64_t offset = 0u;
        bool valid = false;
    };

    void attachDevice(class GlRhiDevice* device);
    void attachPool(GlRhiCommandListPool* pool);
    void resetForPoolReuse();
    [[nodiscard]] bool isRecordingThread() const;
    void resetFrameState();
    [[nodiscard]] bool validateForSubmit() const;
    [[nodiscard]] bool replay(bool validationOnly);
    [[nodiscard]] bool rejectReplayCommand(const char* reason);
    [[nodiscard]] bool beginRecordedCommand(CommandType type);
    [[nodiscard]] bool rejectRecordedCommand(const char* reason);
    [[nodiscard]] bool commandTypeSupports(CommandType type) const;
    [[nodiscard]] bool renderingScopeSupports(CommandType type) const;
    void appendBytes(const void* data, size_t size);
    [[nodiscard]] bool readBytes(size_t& offset, void* destination, size_t size) const;
    void recordString(const char* value);
    [[nodiscard]] bool readString(size_t& offset, const char*& value) const;
    void referenceResource(RhiBufferHandle handle);
    void referenceResource(RhiTextureHandle handle);
    void referenceResource(RhiTextureViewHandle handle);
    void referenceResource(RhiSamplerHandle handle);
    void referenceResource(RhiBindGroupLayoutHandle handle);
    void referenceResource(RhiPipelineLayoutHandle handle);
    void referenceResource(RhiPipelineHandle handle);
    void referenceResource(RhiBindGroupHandle handle);
    void referenceResource(RhiQueryPoolHandle handle);

    template <typename T>
    void appendValue(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Command stream values must be trivially copyable");
        appendBytes(&value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] bool readValue(size_t& offset, T& value) const {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Command stream values must be trivially copyable");
        return readBytes(offset, &value, sizeof(T));
    }

    [[nodiscard]] bool validateGraphicsDrawState(bool indexed) const;
    [[nodiscard]] bool validateComputeDispatchState() const;

    class GlRhiDevice* m_device = nullptr;
    GlRhiCommandListPool* m_pool = nullptr;
    RhiPipelineHandle m_graphicsPipeline;
    RhiPipelineHandle m_computePipeline;
    RhiPipelineLayoutHandle m_boundPipelineLayout;
    std::vector<RhiBindGroupHandle> m_bindGroups;
    std::vector<VertexBufferBindingState> m_vertexBuffers;
    IndexBufferBindingState m_indexBuffer;
    RhiPipelineLayoutHandle m_pushConstantLayout;
    uint32_t m_pushConstantSize = 0u;
    RhiShaderStageFlags m_pushConstantStages = 0u;
    std::vector<RhiTextureFormat> m_renderingColorFormats;
    RhiTextureFormat m_renderingDepthFormat = RhiTextureFormat::Undefined;
    bool m_rendering = false;
    bool m_recordingRendering = false;
    bool m_recordingValid = true;
    uint32_t m_recordingDebugLabelDepth = 0u;
    bool m_replaying = false;
    bool m_validationOnly = false;
    bool m_replayValid = true;
    std::vector<uint8_t> m_commandStream;
    std::unique_ptr<GlRhiCommandResourceReferences> m_resourceReferences;
    RhiCommandListState m_state = RhiCommandListState::Initial;
    bool m_acquired = false;
    RhiCommandListType m_acquiredType = RhiCommandListType::Graphics;
};

class GlRhiCommandListPool final : public RhiCommandListPool {
public:
    GlRhiCommandListPool(class GlRhiDevice& device, const RhiCommandListPoolDesc& desc);
    ~GlRhiCommandListPool() override;

    [[nodiscard]] RhiCommandList* acquire(RhiCommandListType type) override;
    bool reset() override;

private:
    friend class GlRhiCommandList;
    friend class GlRhiDevice;

    [[nodiscard]] bool isOwnerThread() const;
    void detachDevice();

    class GlRhiDevice* m_device = nullptr;
    std::shared_ptr<GlRhiCommandPoolRegistry> m_registry;
    std::thread::id m_ownerThread;
    size_t m_initialArenaCapacity = 0u;
    std::vector<std::shared_ptr<GlRhiCommandList>> m_commandLists;
};

struct GlRhiDeviceData;

class GlRhiDevice final : public RhiDevice {
public:
    GlRhiDevice();
    ~GlRhiDevice() override;

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
    RhiQueryPoolHandle createQueryPool(const RhiQueryPoolDesc& desc) override;
    void* mapBuffer(RhiBufferHandle buffer, uint64_t offset, uint64_t size) override;
    void unmapBuffer(RhiBufferHandle buffer) override;
    [[nodiscard]] bool areQueryResultsAvailable(RhiQueryPoolHandle pool,
                                                uint32_t firstQuery,
                                                uint32_t queryCount) const override;
    bool getQueryResults(RhiQueryPoolHandle pool, uint32_t firstQuery,
                         uint32_t queryCount, uint64_t* results) const override;

    [[nodiscard]] RhiTextureViewHandle currentSwapchainColorView() const override;
    [[nodiscard]] RhiTextureViewHandle currentSwapchainDepthStencilView() const override;
    [[nodiscard]] RhiTextureHandle currentSwapchainColorTexture() const override;
    [[nodiscard]] RhiTextureFormat swapchainColorFormat() const override;
    [[nodiscard]] RhiTextureFormat swapchainDepthStencilFormat() const override;
    bool resizeSwapchain(uint32_t width, uint32_t height) override;

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

    [[nodiscard]] std::unique_ptr<RhiCommandListPool> createCommandListPool(
        const RhiCommandListPoolDesc& desc) override;
    bool submit(const RhiSubmitInfo& info,
                RhiSubmissionToken* completionToken = nullptr) override;
    [[nodiscard]] bool isSubmissionComplete(RhiSubmissionToken token,
                                            bool& complete) override;
    bool waitForSubmission(RhiSubmissionToken token) override;
    void waitIdle() override;
    void present() override;

private:
    friend class GlRhiCommandList;
    friend class GlRhiCommandListPool;

    void reclaimCompletedCommandLists();
    [[nodiscard]] bool validateSubmissionToken(RhiSubmissionToken token) const;

    bool m_initialized = false;
    RhiCapabilities m_capabilities{};
    std::thread::id m_deviceThread;
    uint64_t m_deviceId = 0u;
    uint64_t m_lastSubmittedSequence = 0u;
    std::unique_ptr<GlRhiDeviceData> m_data;
    std::shared_ptr<GlRhiCommandPoolRegistry> m_commandPoolRegistry;
};

#endif // MECRAFT_GL_RHI_DEVICE_H
