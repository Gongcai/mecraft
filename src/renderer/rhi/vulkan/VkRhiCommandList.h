#ifndef MECRAFT_VK_RHI_COMMAND_LIST_H
#define MECRAFT_VK_RHI_COMMAND_LIST_H

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"

#include <memory>
#include <array>
#include <thread>
#include <vector>

class VkRhiDevice;
class VkRhiInterop;
struct VkRhiCommandListData;

class VkRhiCommandList final : public RhiCommandList {
public:
    ~VkRhiCommandList() override;

    bool begin(const RhiCommandListDesc& desc) override;
    bool end() override;
    [[nodiscard]] RhiCommandListState state() const override;
    void beginDebugLabel(const char* name, const glm::vec4& color) override;
    void endDebugLabel() override;
    void insertDebugMarker(const char* name, const glm::vec4& color) override;
    void textureBarrier(const RhiTextureBarrier& barrier) override;
    void bufferBarrier(const RhiBufferBarrier& barrier) override;
    bool accelerationStructureBarrier(const RhiAccelerationStructureBarrier& barrier) override;
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
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void drawIndirect(RhiBufferHandle indirectBuffer, uint64_t offset, uint32_t drawCount, uint32_t stride) override;
    void drawIndexedIndirectCount(RhiBufferHandle indirectBuffer, uint64_t indirectOffset, uint32_t maxDrawCount,
                                  uint32_t stride, RhiBufferHandle countBuffer, uint64_t countOffset) override;
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void updateBuffer(RhiBufferHandle buffer, uint64_t offset, const void* data, size_t size) override;
    void copyBuffer(const RhiBufferCopy& copy) override;
    void copyBufferToTexture(const RhiBufferTextureCopy& copy) override;
    void copyTextureToBuffer(const RhiTextureBufferCopy& copy) override;
    void copyTexture(const RhiTextureCopy& copy) override;
    void blitTexture(const RhiTextureBlit& blit) override;
    void generateMipmaps(RhiTextureHandle texture) override;
    void resetQueryPool(RhiQueryPoolHandle pool, uint32_t firstQuery, uint32_t queryCount) override;
    void writeTimestamp(RhiQueryPoolHandle pool, uint32_t queryIndex) override;
    bool buildAccelerationStructures(const RhiAccelerationStructureBuildDesc* builds, uint32_t buildCount) override;
    bool buildMicromaps(const RhiMicromapBuildDesc* builds, uint32_t buildCount) override;
    bool copyAccelerationStructure(const RhiAccelerationStructureCopyDesc& copy) override;
    bool writeAccelerationStructureProperties(const RhiAccelerationStructurePropertyQueryDesc& query) override;

private:
    friend class VkRhiCommandListPool;
    friend class VkRhiDevice;
    friend class VkRhiInterop;

    VkRhiCommandList(VkRhiDevice& device, void* nativeCommandPool, RhiCommandListType type);
    void resetForPoolReuse();
    void markPending(uint64_t sequence);
    void markComplete();
    [[nodiscard]] bool isExecutable() const;
    [[nodiscard]] void* nativeCommandBuffer() const;

    VkRhiDevice* m_device = nullptr;
    std::unique_ptr<VkRhiCommandListData> m_data;
};

class VkRhiCommandListPool final : public RhiCommandListPool {
public:
    VkRhiCommandListPool(VkRhiDevice& device, const RhiCommandListPoolDesc& desc);
    ~VkRhiCommandListPool() override;

    [[nodiscard]] RhiCommandList* acquire(RhiCommandListType type) override;
    bool reset() override;

private:
    friend class VkRhiDevice;

    VkRhiDevice* m_device = nullptr;
    std::thread::id m_ownerThread;
    std::array<void*, 3u> m_commandPools{};
    std::vector<std::unique_ptr<VkRhiCommandList>> m_commandLists;
};

#endif // MECRAFT_VK_RHI_COMMAND_LIST_H
