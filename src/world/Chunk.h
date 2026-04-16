#ifndef MECRAFT_CHUNK_H
#define MECRAFT_CHUNK_H

#include <array>
#include <cstdint>
#include <vector>
#include <memory>

#include <glm/vec3.hpp>

#include "Block.h"
#include "SubChunk.h"

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
    void copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const;

    void optimizePalette();

    static glm::ivec3 worldToLocal(int wx, int wy, int wz);
    [[nodiscard]] glm::ivec3 getWorldOffset() const;

    // --- Dirty tracking ---
    // A chunk is "dirty" if any of its sub-chunks is dirty.
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool isSubChunkDirty(int scy) const;
    void markDirty();  // Marks ALL sub-chunks dirty
    void markSubChunkDirty(int scy);
    [[nodiscard]] uint64_t getMeshRevision() const;
    [[nodiscard]] uint64_t getSubChunkMeshRevision(int scy) const;
    void markMeshClean();  // Clears all sub-chunk dirty flags
    void markSubChunkMeshClean(int scy);

    // --- Per sub-chunk mesh access ---
    [[nodiscard]] const SubChunkMesh& getSubChunkMesh(int scy) const;
    [[nodiscard]] SubChunkMesh& getSubChunkMesh(int scy);
    void setSubChunkMesh(int scy, const SubChunkMesh& mesh);

    // --- Column-level mesh (DEPRECATED — for transition only) ---
    // Returns the first non-null sub-chunk mesh, or a static empty mesh.
    // Prefer using getSubChunkMesh(scy) directly.
    [[nodiscard]] const SubChunkMesh& getMesh() const;

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
    [[nodiscard]] SubChunk* getSubChunk(int scy);
    [[nodiscard]] const SubChunk* getSubChunk(int scy) const;
    [[nodiscard]] SubChunk* getOrCreateSubChunk(int scy);

    // Copy entire column light map into a flat array (for mesher snapshot compatibility)
    void copyLightMapTo(std::array<uint8_t, BLOCK_COUNT>& out) const;

    // Read a single light value by column-local flat index (for border capture)
    [[nodiscard]] uint8_t getLightByFlatIndex(std::size_t flatIndex) const;

    // Convert column-local Y to sub-chunk index and sub-chunk-local Y
    [[nodiscard]] static int toSubChunkIndex(int y) { return y / SUB_CHUNK_SIZE; }
    [[nodiscard]] static int toSubChunkLocalY(int y) { return y % SUB_CHUNK_SIZE; }

    // Column-level neighbor pointers (4 horizontal directions)
    // [0]=+X, [1]=-X, [2]=+Z, [3]=-Z
    Chunk* neighbors[4] = {nullptr, nullptr, nullptr, nullptr};

    int m_chunkX;
    int m_chunkZ;

    [[nodiscard]] static std::size_t toIndex(int x, int y, int z);

private:
    [[nodiscard]] static bool isInBounds(int x, int y, int z);

    // Sub-chunks along Y axis. nullptr = all-air (SubChunkType::Air with no storage)
    std::array<std::unique_ptr<SubChunk>, NUM_SUB_CHUNKS> m_subChunks{};

    std::array<int, static_cast<std::size_t>(SIZE_X) * SIZE_Z> m_heightMap{};

    bool m_dirty = true;
    uint64_t m_meshRevision = 1;
};

#endif // MECRAFT_CHUNK_H
