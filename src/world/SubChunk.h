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

// GPU buffer range handle used by the global vertex pool (MDI path).
struct GpuMeshRange {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint64_t generation = 0;
};

// Logical handle for a sub-chunk mesh in the global GPU buffer pool.
struct WorldGpuMesh {
    GpuMeshRange opaque;
    GpuMeshRange cutout;
    GpuMeshRange cutoutDistance;
    GpuMeshRange transparent;
    GpuMeshRange water;
    bool hasBounds = false;
    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
};

class WorldRenderBuffer;

struct BlockVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    int8_t normal;
    uint8_t sunlight;
    uint8_t blockLight;
    uint8_t ao;
    uint16_t layer;
    uint16_t animationFrameCount;
    uint8_t animationFps;
    uint8_t animated;
    uint16_t tintPacked;
};

namespace BlockTintKinds {
constexpr uint8_t NONE = 0;
constexpr uint8_t GRASS = 1;
constexpr uint8_t FOLIAGE = 2;
}

inline uint8_t blockTintKindFromBiomeTint(const BiomeTintKind tintKind) {
    switch (tintKind) {
        case BiomeTintKind::Grass:
            return BlockTintKinds::GRASS;
        case BiomeTintKind::Foliage:
            return BlockTintKinds::FOLIAGE;
        case BiomeTintKind::None:
        default:
            return BlockTintKinds::NONE;
    }
}

inline void computeDefaultBlockTintMapPosition(uint8_t& outU, uint8_t& outV) {
    constexpr double temperature = 0.70;
    constexpr double moisture = 0.65;
    outU = static_cast<uint8_t>((1.0 - temperature) * 255.0 + 0.5);
    outV = static_cast<uint8_t>((1.0 - moisture * temperature) * 255.0 + 0.5);
}

inline uint8_t packBlockVertexNormalizedByte(float value) {
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

inline uint8_t packBlockVertexByte(float value) {
    value = value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value);
    return static_cast<uint8_t>(value + 0.5f);
}

inline uint16_t packBlockVertexU16(float value) {
    value = value < 0.0f ? 0.0f : (value > 65535.0f ? 65535.0f : value);
    return static_cast<uint16_t>(value + 0.5f);
}

inline uint16_t packBlockVertexTint(uint8_t kind, uint8_t u, uint8_t v, uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT) {
    const uint16_t packedKind = static_cast<uint16_t>(kind & 0x03U);
    const uint16_t packedMaterial = static_cast<uint16_t>(derivativeMaterialId & 0x3FU);
    const uint16_t packedU = static_cast<uint16_t>((u >> 4U) & 0x0FU);
    const uint16_t packedV = static_cast<uint16_t>((v >> 4U) & 0x0FU);
    return static_cast<uint16_t>((packedKind << 14U) | (packedMaterial << 8U) | (packedU << 4U) | packedV);
}

inline BlockVertex makeBlockVertex(float x,
                                   float y,
                                   float z,
                                   float u,
                                   float v,
                                   float normal,
                                   float sunlight,
                                   float blockLight,
                                   float ao,
                                   float layer,
                                   float animationFrameCount = 1.0f,
                                   float animationFps = 0.0f,
                                   float animated = 0.0f,
                                   uint8_t tintKind = BlockTintKinds::NONE,
                                   uint8_t tintU = 0,
                                   uint8_t tintV = 0,
                                   uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT) {
    return {
        x,
        y,
        z,
        u,
        v,
        static_cast<int8_t>(normal),
        packBlockVertexNormalizedByte(sunlight),
        packBlockVertexNormalizedByte(blockLight),
        packBlockVertexByte(ao),
        packBlockVertexU16(layer),
        packBlockVertexU16(animationFrameCount),
        packBlockVertexByte(animationFps),
        animated > 0.5f ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0),
        packBlockVertexTint(tintKind, tintU, tintV, derivativeMaterialId)
    };
}

static_assert(sizeof(BlockVertex) <= 32, "BlockVertex should stay bandwidth-friendly");

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

    GLuint cutoutDistanceVao = 0;
    GLuint cutoutDistanceVbo = 0;
    uint32_t cutoutDistanceVertexCount = 0;
    GLsizeiptr cutoutDistanceVboCapacity = 0;

    bool hasBounds = false;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);

    // MDI path: GPU ranges in the global buffer pool
    GpuMeshRange opaqueRange;
    GpuMeshRange cutoutRange;
    GpuMeshRange cutoutDistanceRange;
    GpuMeshRange transparentRange;
    GpuMeshRange waterRange;
    uint32_t waterVertexCount = 0;
    bool inGlobalPool = false;

    void upload(const std::vector<BlockVertex>& vertices);
    void uploadCutout(const std::vector<BlockVertex>& cutoutVerts);
    void uploadCutoutDistance(const std::vector<BlockVertex>& cutoutDistanceVerts);
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
    void initializeFromBlocks(const std::array<BlockID, BLOCK_COUNT>& blocks);
    void copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const;

    // --- Fluid layer access (for waterlogged blocks) ---
    [[nodiscard]] BlockID getFluidLayer(int x, int y, int z) const;
    void setFluidLayer(int x, int y, int z, BlockID id);

    void optimizePalette();

    [[nodiscard]] static std::size_t toIndex(int x, int y, int z);

    [[nodiscard]] bool isDirty() const { return m_dirty; }
    void markDirty();
    [[nodiscard]] uint64_t getMeshRevision() const;
    void markMeshClean();

    // Light access — same packing as before: high nibble = sun, low nibble = block
    [[nodiscard]] uint8_t getSunlight(int x, int y, int z) const;
    void setSunlight(int x, int y, int z, uint8_t level);
    [[nodiscard]] uint8_t getBlockLight(int x, int y, int z) const;
    void setBlockLight(int x, int y, int z, uint8_t level);

    // Sub-chunk type — for semantic culling
    [[nodiscard]] SubChunkType getType() const { return m_type; }
    void setType(SubChunkType type) { m_type = type; }
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

    Palette m_fluidPalette;
    BitPackedArray m_fluidData;
    std::unordered_map<BlockID, uint16_t> m_fluidCounts;

    SubChunkType m_type = SubChunkType::Air;
    bool m_dirty = true;
    uint64_t m_meshRevision = 1;
    SubChunkMesh m_mesh;
};

#endif // MECRAFT_SUBCHUNK_H
