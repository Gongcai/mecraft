#ifndef MECRAFT_WORLDRENDERBUFFER_H
#define MECRAFT_WORLDRENDERBUFFER_H

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "renderer/rhi/RhiGrowableBuffer.h"
#include "renderer/rhi/RhiHandles.h"
#include "../../world/chunk/SubChunk.h"

class RhiCommandList;
class RhiDevice;

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    uint32_t baseInstance;
};

class VertexRangeAllocator {
public:
    void init(size_t capacityVertices);
    void shutdown();

    bool allocate(uint32_t vertexCount, GpuMeshRange& outRange);
    void free(const GpuMeshRange& range);
    void grow(size_t newCapacityVertices);

    [[nodiscard]] uint64_t generation() const { return m_generationCounter; }
    [[nodiscard]] size_t capacityVertices() const { return m_capacityVertices; }
    [[nodiscard]] size_t usedVertices() const { return m_usedVertices; }
    [[nodiscard]] float fragmentationRatio() const;

private:
    struct FreeBlock {
        uint32_t offset = 0;
        uint32_t size = 0;
        int next = -1;
    };

    static constexpr size_t kFreeBlockPoolSize = 256;
    std::vector<FreeBlock> m_freeBlocks;
    int m_freeHead = -1;
    std::vector<int> m_freeBlockFreeList;
    size_t m_capacityVertices = 0;
    size_t m_usedVertices = 0;
    uint64_t m_generationCounter = 1;
    std::unordered_set<uint64_t> m_liveAllocations;

    int allocFreeBlockNode();
    void returnFreeBlockNode(int nodeIdx);
    void coalesce();
};

class RhiVertexPoolAllocator {
public:
    RhiVertexPoolAllocator();
    ~RhiVertexPoolAllocator();

    bool init(RhiDevice& rhiDevice, size_t initialCapacityVertices, const char* debugName);
    void shutdown();

    bool allocate(RhiCommandList& commandList, uint32_t vertexCount, GpuMeshRange& outRange);
    void free(const GpuMeshRange& range);
    bool upload(RhiCommandList& commandList,
                const GpuMeshRange& range,
                const std::vector<PackedBlockVertex>& vertices);

    [[nodiscard]] RhiBufferHandle buffer() const { return m_buffer; }
    [[nodiscard]] size_t capacityVertices() const { return m_ranges.capacityVertices(); }
    [[nodiscard]] size_t usedVertices() const { return m_ranges.usedVertices(); }
    [[nodiscard]] float fragmentationRatio() const { return m_ranges.fragmentationRatio(); }

    void beginFrame();
    [[nodiscard]] size_t expandCountThisFrame() const { return m_expandCountThisFrame; }
    [[nodiscard]] size_t uploadedBytesThisFrame() const { return m_uploadedBytesThisFrame; }

private:
    bool expand(RhiCommandList& commandList, size_t newCapacityVertices);

    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_buffer;
    std::vector<RhiBufferHandle> m_retiredBuffers;
    VertexRangeAllocator m_ranges;
    const char* m_debugName = nullptr;
    size_t m_expandCountThisFrame = 0;
    size_t m_uploadedBytesThisFrame = 0;
};

class WorldRenderBuffer {
public:
    // GPU occlusion culling contract: raw handles and counts so the Hi-Z
    // cull pass can patch indirect draw commands in place.
    [[nodiscard]] RhiBufferHandle opaqueIndirectBufferHandle() const { return m_rhiOpaqueIndirectBuffer.buffer(); }
    [[nodiscard]] RhiBufferHandle cutoutIndirectBufferHandle() const { return m_rhiCutoutIndirectBuffer.buffer(); }
    [[nodiscard]] RhiBufferHandle transparentIndirectBufferHandle() const { return m_rhiTransparentIndirectBuffer.buffer(); }
    [[nodiscard]] RhiBufferHandle metadataBufferHandle() const { return m_rhiMetadataBuffer.buffer(); }
    [[nodiscard]] uint64_t opaqueIndirectBufferCapacity() const { return m_rhiOpaqueIndirectBuffer.capacity(); }
    [[nodiscard]] uint64_t cutoutIndirectBufferCapacity() const { return m_rhiCutoutIndirectBuffer.capacity(); }
    [[nodiscard]] uint64_t transparentIndirectBufferCapacity() const { return m_rhiTransparentIndirectBuffer.capacity(); }
    [[nodiscard]] uint64_t metadataBufferCapacity() const { return m_rhiMetadataBuffer.capacity(); }
    struct SubChunkDrawMetadata {
        glm::vec4 originAndFlags = glm::vec4(0.0f);
        glm::uvec4 identity = glm::uvec4(0u);
    };

    struct FrameStatsSnapshot {
        int rhiSubmitCount = 0;
        size_t opaqueCommands = 0;
        size_t cutoutCommands = 0;
        size_t transparentCommands = 0;
        size_t waterCommands = 0;
        size_t opaqueLogicalCommands = 0;
        size_t cutoutLogicalCommands = 0;
        size_t transparentLogicalCommands = 0;
        uint64_t opaqueVertices = 0;
        uint64_t cutoutVertices = 0;
        uint64_t transparentVertices = 0;
        uint64_t waterVertices = 0;
    };

    static constexpr size_t kInitialPoolVertices = 1 << 21;   // ~2M vertices (~64MB), avoids early expand stalls
    static constexpr size_t kInitialCutoutPoolVertices = kInitialPoolVertices / 4;
    static constexpr size_t kInitialTransparentPoolVertices = kInitialPoolVertices / 16;
    static constexpr size_t kInitialIndirectCapacity = 4096;
    static constexpr uint32_t kTerrainMetadataBinding = 0;

    WorldRenderBuffer();
    ~WorldRenderBuffer();

    bool init(RhiDevice& rhiDevice);
    void shutdown();

    WorldGpuMesh uploadSubChunk(RhiCommandList& commandList,
                                const std::vector<BlockVertex>& opaque,
                                const std::vector<BlockVertex>& cutout,
                                const std::vector<BlockVertex>& cutoutDistance,
                                const std::vector<BlockVertex>& transparent,
                                const std::vector<BlockVertex>& water,
                                bool hasBounds,
                                const glm::vec3& boundsMin,
                                const glm::vec3& boundsMax);
    void free(const WorldGpuMesh& mesh);

    void beginFrame();
    /// Clears per-draw command streams without releasing resources referenced by
    /// command lists that are still being recorded.
    void resetDrawCommands();
    void captureSceneFrameStats();
    void mergeSceneWaterFrameStats();

    void addOpaque(const GpuMeshRange& range);
    void addCutout(const GpuMeshRange& range);
    void addTransparent(const GpuMeshRange& range);
    void addWater(const GpuMeshRange& range);

    bool prepareRhiOpaqueAndCutout(RhiCommandList& commandList,
                                   RhiBindGroupLayoutHandle metadataLayout);
    void recordRhiOpaque(RhiCommandList& commandList,
                         RhiPipelineHandle pipeline,
                         RhiBindGroupHandle materialBindGroup);
    void recordRhiCutout(RhiCommandList& commandList,
                         RhiPipelineHandle pipeline,
                         RhiBindGroupHandle materialBindGroup);
    bool prepareRhiTransparent(RhiCommandList& commandList,
                               RhiBindGroupLayoutHandle metadataLayout);
    void recordRhiTransparent(RhiCommandList& commandList,
                              RhiPipelineHandle pipeline,
                              RhiBindGroupHandle materialBindGroup);
    bool prepareRhiWater(RhiCommandList& commandList,
                         RhiBindGroupLayoutHandle metadataLayout);
    void recordRhiWater(RhiCommandList& commandList,
                        RhiPipelineHandle pipeline,
                        RhiBindGroupHandle materialBindGroup);

    void clearWaterCommands();

    const FrameStatsSnapshot& sceneFrameStats() const { return m_sceneFrameStats; }
    int rhiSubmitCount() const { return m_rhiSubmitCount; }
    size_t opaqueCommandCount() const { return m_opaqueCommands.size(); }
    size_t cutoutCommandCount() const { return m_cutoutCommands.size(); }
    size_t transparentCommandCount() const { return m_transparentCommands.size(); }
    size_t waterCommandCount() const { return m_waterCommands.size(); }
    size_t opaqueLogicalCommandCount() const { return m_opaqueLogicalCommandCount; }
    size_t cutoutLogicalCommandCount() const { return m_cutoutLogicalCommandCount; }
    size_t transparentLogicalCommandCount() const { return m_transparentLogicalCommandCount; }
    size_t opaqueCapacityVertices() const { return m_opaquePool.capacityVertices(); }
    size_t cutoutCapacityVertices() const { return m_cutoutPool.capacityVertices(); }
    size_t transparentCapacityVertices() const { return m_transparentPool.capacityVertices(); }
    size_t opaqueUsedVertices() const { return m_opaquePool.usedVertices(); }
    size_t cutoutUsedVertices() const { return m_cutoutPool.usedVertices(); }
    size_t transparentUsedVertices() const { return m_transparentPool.usedVertices(); }
    float opaqueFragmentationRatio() const { return m_opaquePool.fragmentationRatio(); }
    float cutoutFragmentationRatio() const { return m_cutoutPool.fragmentationRatio(); }
    float transparentFragmentationRatio() const { return m_transparentPool.fragmentationRatio(); }
    size_t metadataSlotCount() const { return m_subChunkMetadata.size(); }
    size_t metadataFreeSlotCount() const { return m_freeSubChunkMetadataIndices.size(); }
    size_t metadataBytes() const { return m_subChunkMetadata.size() * sizeof(SubChunkDrawMetadata); }
    uint64_t opaqueVertexCount() const { return m_opaqueVertexCount; }
    uint64_t cutoutVertexCount() const { return m_cutoutVertexCount; }
    uint64_t transparentVertexCount() const { return m_transparentVertexCount; }
    uint64_t waterVertexCount() const { return m_waterVertexCount; }

    // Per-frame pool stats
    size_t opaqueExpandCount() const { return m_opaquePool.expandCountThisFrame(); }
    size_t cutoutExpandCount() const { return m_cutoutPool.expandCountThisFrame(); }
    size_t transparentExpandCount() const { return m_transparentPool.expandCountThisFrame(); }
    size_t opaqueUploadedBytes() const { return m_opaquePool.uploadedBytesThisFrame(); }
    size_t cutoutUploadedBytes() const { return m_cutoutPool.uploadedBytesThisFrame(); }
    size_t transparentUploadedBytes() const { return m_transparentPool.uploadedBytesThisFrame(); }

private:
    uint32_t uploadSubChunkMetadata(RhiCommandList& commandList, const glm::vec3& origin);
    bool ensureRhiMetadataBindGroup(RhiBindGroupLayoutHandle metadataLayout);

    RhiVertexPoolAllocator m_opaquePool;
    RhiVertexPoolAllocator m_cutoutPool;
    RhiVertexPoolAllocator m_transparentPool;

    RhiDevice* m_rhiDevice = nullptr;
    RhiGrowableBuffer m_rhiOpaqueIndirectBuffer;
    RhiGrowableBuffer m_rhiCutoutIndirectBuffer;
    RhiGrowableBuffer m_rhiTransparentIndirectBuffer;
    RhiGrowableBuffer m_rhiWaterIndirectBuffer;
    RhiGrowableBuffer m_rhiMetadataBuffer;
    RhiBindGroupHandle m_rhiMetadataBindGroup;
    std::vector<RhiBindGroupHandle> m_retiredMetadataBindGroups;
    RhiBindGroupLayoutHandle m_rhiMetadataLayout;
    RhiBufferHandle m_rhiMetadataBoundBuffer;

    std::vector<DrawArraysIndirectCommand> m_opaqueCommands;
    std::vector<DrawArraysIndirectCommand> m_cutoutCommands;
    std::vector<DrawArraysIndirectCommand> m_transparentCommands;
    std::vector<DrawArraysIndirectCommand> m_waterCommands;
    std::vector<SubChunkDrawMetadata> m_subChunkMetadata;
    std::vector<uint32_t> m_freeSubChunkMetadataIndices;

    int m_rhiSubmitCount = 0;
    size_t m_opaqueLogicalCommandCount = 0;
    size_t m_cutoutLogicalCommandCount = 0;
    size_t m_transparentLogicalCommandCount = 0;
    uint64_t m_opaqueVertexCount = 0;
    uint64_t m_cutoutVertexCount = 0;
    uint64_t m_transparentVertexCount = 0;
    uint64_t m_waterVertexCount = 0;
    FrameStatsSnapshot m_sceneFrameStats{};

    FrameStatsSnapshot makeCurrentFrameStats() const;
};

#endif // MECRAFT_WORLDRENDERBUFFER_H
