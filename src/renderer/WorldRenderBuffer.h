#ifndef MECRAFT_WORLDRENDERBUFFER_H
#define MECRAFT_WORLDRENDERBUFFER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../world/SubChunk.h"

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    uint32_t baseInstance;
};

class VertexPoolAllocator {
public:
    VertexPoolAllocator();
    ~VertexPoolAllocator();

    void init(size_t initialCapacityVertices);
    void shutdown();

    bool allocate(uint32_t vertexCount, GpuMeshRange& outRange);
    void free(const GpuMeshRange& range);
    void upload(const GpuMeshRange& range, const std::vector<BlockVertex>& vertices);

    GLuint vbo() const { return m_vbo; }
    uint64_t generation() const { return m_generationCounter; }
    size_t capacityVertices() const { return m_capacityVertices; }
    size_t usedVertices() const { return m_usedVertices; }
    float fragmentationRatio() const;

    // Register/unregister active ranges for defrag remapping
    void registerRange(GpuMeshRange* range);
    void unregisterRange(GpuMeshRange* range);

private:
    struct FreeBlock {
        uint32_t offset = 0;
        uint32_t size = 0;
        int next = -1;
    };

    void expand(size_t newCapacityVertices);
    void defragment();

    static constexpr size_t kFreeBlockPoolSize = 256;
    std::vector<FreeBlock> m_freeBlocks;
    int m_freeHead = -1;
    std::vector<int> m_freeBlockFreeList;

    GLuint m_vbo = 0;
    size_t m_capacityVertices = 0;
    size_t m_usedVertices = 0;
    uint64_t m_generationCounter = 1;
    std::unordered_set<uint64_t> m_liveAllocations;

    std::vector<GpuMeshRange*> m_activeRanges;

    int allocFreeBlockNode();
    void returnFreeBlockNode(int nodeIdx);
    void coalesceAt(int nodeIdx);
};

class WorldRenderBuffer {
public:
    static constexpr size_t kInitialPoolVertices = 1 << 20;   // 1M vertices ≈ 44 MB
    static constexpr size_t kInitialIndirectCapacity = 4096;
    static constexpr float kDefragmentThreshold = 0.35f;

    WorldRenderBuffer();
    ~WorldRenderBuffer();

    void init();
    void shutdown();

    WorldGpuMesh uploadSubChunk(const std::vector<BlockVertex>& opaque,
                                const std::vector<BlockVertex>& cutout,
                                const std::vector<BlockVertex>& transparent,
                                bool hasBounds,
                                const glm::vec3& boundsMin,
                                const glm::vec3& boundsMax);
    void free(const WorldGpuMesh& mesh);

    void beginFrame();

    void addOpaque(const GpuMeshRange& range);
    void addCutout(const GpuMeshRange& range);
    void addTransparent(const GpuMeshRange& range);

    void flushOpaque();
    void flushCutout();
    void flushTransparent();

    int glSubmitCount() const { return m_glSubmitCount; }
    size_t opaqueCommandCount() const { return m_opaqueCommands.size(); }
    size_t cutoutCommandCount() const { return m_cutoutCommands.size(); }
    size_t transparentCommandCount() const { return m_transparentCommands.size(); }
    uint64_t opaqueVertexCount() const { return m_opaqueVertexCount; }
    uint64_t cutoutVertexCount() const { return m_cutoutVertexCount; }
    uint64_t transparentVertexCount() const { return m_transparentVertexCount; }

private:
    static void setupVertexLayout();

    VertexPoolAllocator m_opaquePool;
    VertexPoolAllocator m_cutoutPool;
    VertexPoolAllocator m_transparentPool;

    GLuint m_opaqueVao = 0;
    GLuint m_cutoutVao = 0;
    GLuint m_transparentVao = 0;

    GLuint m_opaqueIndirectBuf = 0;
    GLuint m_cutoutIndirectBuf = 0;
    GLuint m_transparentIndirectBuf = 0;

    size_t m_opaqueIndirectCapacity = 0;
    size_t m_cutoutIndirectCapacity = 0;
    size_t m_transparentIndirectCapacity = 0;

    std::vector<DrawArraysIndirectCommand> m_opaqueCommands;
    std::vector<DrawArraysIndirectCommand> m_cutoutCommands;
    std::vector<DrawArraysIndirectCommand> m_transparentCommands;

    int m_glSubmitCount = 0;
    uint64_t m_opaqueVertexCount = 0;
    uint64_t m_cutoutVertexCount = 0;
    uint64_t m_transparentVertexCount = 0;

    void ensureIndirectCapacity(std::vector<DrawArraysIndirectCommand>& commands,
                                GLuint& buf, size_t& capacity, size_t needed);
    void flushPass(std::vector<DrawArraysIndirectCommand>& commands,
                   GLuint indirectBuf, GLuint vao, GLuint vbo);
};

#endif // MECRAFT_WORLDRENDERBUFFER_H
