#ifndef MECRAFT_SUBCHUNK_H
#define MECRAFT_SUBCHUNK_H

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>
#include <glm/vec3.hpp>

#include "Block.h"
#include "Palette.h"
#include "BitPackedArray.h"

struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float normal;
    float sunlight;
    float blockLight;
    float ao;
    float layer;
};

struct SubChunkMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    uint32_t vertexCount = 0;
    GLsizeiptr vboCapacity = 0;

    GLuint transparentVao = 0;
    GLuint transparentVbo = 0;
    uint32_t transparentVertexCount = 0;
    GLsizeiptr transparentVboCapacity = 0;

    GLuint cutoutVao = 0;
    GLuint cutoutVbo = 0;
    uint32_t cutoutVertexCount = 0;
    GLsizeiptr cutoutVboCapacity = 0;

    bool hasBounds = false;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);

    void upload(const std::vector<BlockVertex>& vertices);
    void uploadCutout(const std::vector<BlockVertex>& cutoutVerts);
    void uploadTransparent(const std::vector<BlockVertex>& transparentVerts);
    void destroy();
};

// Semantic type for a sub-chunk — enables zero-cost skipping during rendering/meshing
enum class SubChunkType : uint8_t {
    Air     = 0,  // Entire sub-chunk is air — no storage, no mesh, no render
    Solid   = 1,  // Entire sub-chunk is a single non-air block — minimal storage, rarely rendered
    Normal  = 2   // Mixed content — needs full storage, meshing, and rendering
};

// SubChunk: a 16x16x16 slice of a ChunkColumn.
// Each SubChunk owns its own mesh for per-section draw calls.
class SubChunk {
public:
    static constexpr int SIZE = 16;
    static constexpr std::size_t BLOCK_COUNT = static_cast<std::size_t>(SIZE) * SIZE * SIZE; // 4096

    SubChunk();
    ~SubChunk();

    // Disallow copy
    SubChunk(const SubChunk&) = delete;
    SubChunk& operator=(const SubChunk&) = delete;
    SubChunk(SubChunk&& other) noexcept;
    SubChunk& operator=(SubChunk&& other) noexcept;

    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);
    void setBlockWithoutMeshDirty(int x, int y, int z, BlockID id);
    void setBlockFast(int x, int y, int z, BlockID id);
    void copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const;

    void optimizePalette();

    [[nodiscard]] static std::size_t toIndex(int x, int y, int z);

    [[nodiscard]] bool isDirty() const;
    void markDirty();
    [[nodiscard]] uint64_t getMeshRevision() const;
    void markMeshClean();

    // Light access — same packing as before: high nibble = sun, low nibble = block
    [[nodiscard]] uint8_t getSunlight(int x, int y, int z) const;
    void setSunlight(int x, int y, int z, uint8_t level);
    [[nodiscard]] uint8_t getBlockLight(int x, int y, int z) const;
    void setBlockLight(int x, int y, int z, uint8_t level);

    // Sub-chunk type — for semantic culling
    [[nodiscard]] SubChunkType getType() const;
    void setType(SubChunkType type);
    void inferType();  // Scan contents and set type accordingly

    // Per-sub-chunk mesh
    [[nodiscard]] const SubChunkMesh& getMesh() const;
    [[nodiscard]] SubChunkMesh& getMesh();
    void setMesh(const SubChunkMesh& mesh);

    // 6-direction neighbor pointers: [0]=+X, [1]=-X, [2]=+Y, [3]=-Y, [4]=+Z, [5]=-Z
    SubChunk* neighbors[6] = {};

    // Public for snapshot capture (same pattern as old Chunk)
    std::array<uint8_t, BLOCK_COUNT> m_lightMap{};

    // Which sub-chunk index (0..15) within the column — set by Chunk on creation
    int m_subChunkY = 0;

private:
    [[nodiscard]] static bool isInBounds(int x, int y, int z);
    void setBlockImpl(int x, int y, int z, BlockID id, bool markMeshDirty);

    Palette m_palette;
    BitPackedArray m_blockData;
    std::unordered_map<BlockID, uint16_t> m_blockCounts;

    SubChunkType m_type = SubChunkType::Air;
    bool m_dirty = true;
    uint64_t m_meshRevision = 1;
    SubChunkMesh m_mesh;
};

#endif // MECRAFT_SUBCHUNK_H
