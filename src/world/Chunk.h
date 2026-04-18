#ifndef MECRAFT_CHUNK_H
#define MECRAFT_CHUNK_H

#include <array>
#include <cstdint>
#include <vector>
#include <memory>

#include <glm/vec3.hpp>

#include "Block.h"
#include "SubChunk.h"

struct ChunkMeshData;

// Chunk = ChunkColumn. Internally composed of NUM_SUB_CHUNKS SubChunks along Y axis.
// Phase 2: Each SubChunk owns its own mesh. Chunk provides column-level aggregation.
class Chunk {
public:
    static constexpr int SIZE_X = 16;
    static constexpr int SIZE_Y = 256;
    static constexpr int SIZE_Z = 16;
    static constexpr int SUB_CHUNK_SIZE = SubChunk::SIZE;  // 16
    static constexpr int NUM_SUB_CHUNKS = SIZE_Y / SUB_CHUNK_SIZE;  // 16
    static constexpr std::size_t BLOCK_COUNT = static_cast<std::size_t>(SIZE_X) * SIZE_Y * SIZE_Z;

    Chunk(int chunkX, int chunkZ);
    ~Chunk();

    // --- Block access (column-local coordinates, y in [0, 256)) ---
    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);
    void setBlockWithoutMeshDirty(int x, int y, int z, BlockID id);
    void setBlockFast(int x, int y, int z, BlockID id);

    void optimizePalette();
    void seedInitialLightMap();

    static glm::ivec3 worldToLocal(int wx, int wy, int wz);
    [[nodiscard]] glm::ivec3 getWorldOffset() const;

    // --- Dirty tracking ---
    // A chunk is "dirty" if any of its sub-chunks is dirty.
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isSubChunkDirty(int scy) const {
        if (scy < 0 || scy >= NUM_SUB_CHUNKS) return false;
        return (m_dirtySubChunkMask & (1u << scy)) != 0u;
    }
    void markSubChunkDirty(int scy);
    [[nodiscard]] uint64_t getSubChunkMeshRevision(int scy) const;
    void markMeshClean();  // Clears all sub-chunk dirty flags

    // --- Per sub-chunk mesh access ---
    [[nodiscard]] const SubChunkMesh& getSubChunkMesh(int scy) const;
    [[nodiscard]] SubChunkMesh& getSubChunkMesh(int scy);
    void setSubChunkMesh(int scy, const SubChunkMesh& mesh);

    // --- Column-level aggregated mesh access (opaque + cutout) ---
    [[nodiscard]] const SubChunkMesh& getColumnMesh() const;
    [[nodiscard]] SubChunkMesh& getColumnMesh();
    void updateColumnAggregateData(int scy, const ChunkMeshData& meshData);
    void ensureColumnMeshBuilt();

    // --- Light access (column-local coordinates) ---
    [[nodiscard]] uint8_t getSunlight(int x, int y, int z) const;
    void setSunlight(int x, int y, int z, uint8_t level);
    [[nodiscard]] uint8_t getBlockLight(int x, int y, int z) const;
    void setBlockLight(int x, int y, int z, uint8_t level);

    // --- Height map ---
    [[nodiscard]] int getHeightMap(int x, int z) const;
    void setHeightMap(int x, int z, int height);
    void recalcHeightMap(int x, int z);

    // --- Sub-chunk access ---
    [[nodiscard]] SubChunk* getSubChunk(int scy) {
        if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
        return m_subChunks[scy].get();
    }
    [[nodiscard]] const SubChunk* getSubChunk(int scy) const {
        if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
        return m_subChunks[scy].get();
    }
    [[nodiscard]] SubChunk* getOrCreateSubChunk(int scy);

    [[nodiscard]] uint8_t getPackedLight(int x, int y, int z) const;
    bool replacePackedLight(const uint8_t* data, size_t size, uint32_t* outDirtySubChunkMask = nullptr);

    [[nodiscard]] uint64_t getLightRevision() const { return m_lightRevision; }
    uint64_t bumpLightRevision() { return ++m_lightRevision; }
    void setLightQueued(bool queued) { m_lightQueued = queued; }
    [[nodiscard]] bool isLightQueued() const { return m_lightQueued; }
    void setLightInFlight(bool inFlight) { m_lightInFlight = inFlight; }
    [[nodiscard]] bool isLightInFlight() const { return m_lightInFlight; }

    // Convert column-local Y to sub-chunk index and sub-chunk-local Y
    [[nodiscard]] static int toSubChunkIndex(int y) { return y / SUB_CHUNK_SIZE; }
    [[nodiscard]] static int toSubChunkLocalY(int y) { return y % SUB_CHUNK_SIZE; }

    void markExistingSubChunksDirty();
    void linkExistingSubChunksWithNeighbor(int direction);
    void unlinkExistingSubChunksFromNeighbor(int direction);

    // Column-level neighbor pointers (4 horizontal directions)
    // [0]=+X, [1]=-X, [2]=+Z, [3]=-Z
    Chunk* neighbors[4] = {nullptr, nullptr, nullptr, nullptr};

    int m_chunkX;
    int m_chunkZ;

    [[nodiscard]] static std::size_t toIndex(int x, int y, int z);

private:
    struct ColumnAggregateSlice {
        std::vector<BlockVertex> opaqueVertices;
        std::vector<BlockVertex> cutoutVertices;
        bool hasBounds = false;
        glm::vec3 boundsMin = glm::vec3(0.0f);
        glm::vec3 boundsMax = glm::vec3(0.0f);
    };

    [[nodiscard]] static bool isInBounds(int x, int y, int z);
    void setBlockImpl(int x, int y, int z, BlockID id, bool markMeshDirty);
    [[nodiscard]] uint8_t getImplicitSunlight(int x, int y, int z) const;
    [[nodiscard]] uint8_t getImplicitPackedLight(int x, int y, int z) const;
    void initializeSubChunkLightDefaults(SubChunk& subChunk) const;
    [[nodiscard]] bool canRecycleSubChunk(const SubChunk& subChunk) const;
    void recycleSubChunk(int scy);
    void tryRecycleSubChunk(int scy);
    void rebuildColumnMesh();

    // Sub-chunks along Y axis. nullptr = all-air (SubChunkType::Air with no storage)
    std::array<std::unique_ptr<SubChunk>, NUM_SUB_CHUNKS> m_subChunks{};
    std::array<ColumnAggregateSlice, NUM_SUB_CHUNKS> m_columnAggregateSlices{};
    SubChunkMesh m_columnMesh;

    std::array<int, static_cast<std::size_t>(SIZE_X) * SIZE_Z> m_heightMap{};

    bool m_dirty = true;
    uint32_t m_dirtySubChunkMask = 0;
    bool m_columnMeshDirty = false;
    uint64_t m_lightRevision = 1;
    bool m_lightQueued = false;
    bool m_lightInFlight = false;
};

#endif // MECRAFT_CHUNK_H
