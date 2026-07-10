#ifndef MECRAFT_WORLDRENDERBUFFER_H
#define MECRAFT_WORLDRENDERBUFFER_H

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../../world/chunk/SubChunk.h"

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

class VertexPoolAllocator {
public:
    VertexPoolAllocator();
    ~VertexPoolAllocator();

    void init(size_t initialCapacityVertices);
    void shutdown();

    bool allocate(uint32_t vertexCount, GpuMeshRange& outRange);
    void free(const GpuMeshRange& range);
    void upload(const GpuMeshRange& range, const std::vector<PackedBlockVertex>& vertices);

    uint32_t vbo() const { return m_vbo; }
    uint64_t generation() const { return m_ranges.generation(); }
    size_t capacityVertices() const { return m_ranges.capacityVertices(); }
    size_t usedVertices() const { return m_ranges.usedVertices(); }
    float fragmentationRatio() const { return m_ranges.fragmentationRatio(); }

    // Per-frame stats (reset by beginFrame)
    void beginFrame();
    size_t expandCountThisFrame() const { return m_expandCountThisFrame; }
    size_t uploadedBytesThisFrame() const { return m_uploadedBytesThisFrame; }

private:
    void expand(size_t newCapacityVertices);

    VertexRangeAllocator m_ranges;
    uint32_t m_vbo = 0;

    // Per-frame stats
    size_t m_expandCountThisFrame = 0;
    size_t m_uploadedBytesThisFrame = 0;
};

class WorldRenderBuffer {
public:
    struct SubChunkDrawMetadata {
        glm::vec4 originAndFlags = glm::vec4(0.0f);
    };

    struct FrameStatsSnapshot {
        int glSubmitCount = 0;
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

    void init();
    void shutdown();

    WorldGpuMesh uploadSubChunk(const std::vector<BlockVertex>& opaque,
                                const std::vector<BlockVertex>& cutout,
                                const std::vector<BlockVertex>& cutoutDistance,
                                const std::vector<BlockVertex>& transparent,
                                const std::vector<BlockVertex>& water,
                                bool hasBounds,
                                const glm::vec3& boundsMin,
                                const glm::vec3& boundsMax);
    void free(const WorldGpuMesh& mesh);

    void beginFrame();
    void captureSceneFrameStats();
    void mergeSceneWaterFrameStats();

    void addOpaque(const GpuMeshRange& range);
    void addCutout(const GpuMeshRange& range);
    void addTransparent(const GpuMeshRange& range);
    void addWater(const GpuMeshRange& range);

    void flushOpaque();
    void flushCutout();
    void flushTransparent();
    void flushWater();
    void bindTransparentVao();

    void clearWaterCommands();

    uint32_t transparentVao() const { return m_transparentVao; }
    const FrameStatsSnapshot& sceneFrameStats() const { return m_sceneFrameStats; }
    int glSubmitCount() const { return m_glSubmitCount; }
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
    static void setupVertexLayout();
    uint32_t uploadSubChunkMetadata(const glm::vec3& origin);
    void bindMetadataBuffer() const;

    VertexPoolAllocator m_opaquePool;
    VertexPoolAllocator m_cutoutPool;
    VertexPoolAllocator m_transparentPool;

    uint32_t m_opaqueVao = 0;
    uint32_t m_cutoutVao = 0;
    uint32_t m_transparentVao = 0;
    uint32_t m_opaqueVaoBoundVbo = 0;
    uint32_t m_cutoutVaoBoundVbo = 0;
    uint32_t m_transparentVaoBoundVbo = 0;

    uint32_t m_opaqueIndirectBuf = 0;
    uint32_t m_cutoutIndirectBuf = 0;
    uint32_t m_transparentIndirectBuf = 0;
    uint32_t m_waterIndirectBuf = 0;
    uint32_t m_subChunkMetadataBuffer = 0;

    size_t m_opaqueIndirectCapacity = 0;
    size_t m_cutoutIndirectCapacity = 0;
    size_t m_transparentIndirectCapacity = 0;
    size_t m_waterIndirectCapacity = 0;

    std::vector<DrawArraysIndirectCommand> m_opaqueCommands;
    std::vector<DrawArraysIndirectCommand> m_cutoutCommands;
    std::vector<DrawArraysIndirectCommand> m_transparentCommands;
    std::vector<DrawArraysIndirectCommand> m_waterCommands;
    std::vector<SubChunkDrawMetadata> m_subChunkMetadata;
    std::vector<uint32_t> m_freeSubChunkMetadataIndices;

    int m_glSubmitCount = 0;
    size_t m_opaqueLogicalCommandCount = 0;
    size_t m_cutoutLogicalCommandCount = 0;
    size_t m_transparentLogicalCommandCount = 0;
    uint64_t m_opaqueVertexCount = 0;
    uint64_t m_cutoutVertexCount = 0;
    uint64_t m_transparentVertexCount = 0;
    uint64_t m_waterVertexCount = 0;
    FrameStatsSnapshot m_sceneFrameStats{};

    void ensureVaoVertexBuffer(uint32_t vao, uint32_t vbo, uint32_t& cachedVbo);
    FrameStatsSnapshot makeCurrentFrameStats() const;
    void ensureIndirectCapacity(std::vector<DrawArraysIndirectCommand>& commands,
                                uint32_t& buf, size_t& capacity, size_t needed);
    void flushPass(std::vector<DrawArraysIndirectCommand>& commands,
                   uint32_t& indirectBuf, uint32_t vao, uint32_t vbo, uint32_t& cachedVbo);
};

#endif // MECRAFT_WORLDRENDERBUFFER_H
