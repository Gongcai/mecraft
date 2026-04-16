#ifndef MECRAFT_CHUNKMESHER_H
#define MECRAFT_CHUNKMESHER_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/vec3.hpp>

#include "../resource/ResourceMgr.h"
#include "../world/Chunk.h"

class World;

// Per-sub-chunk snapshot constants
constexpr std::size_t SC_BLOCK_COUNT = static_cast<std::size_t>(SubChunk::SIZE) *
                                        SubChunk::SIZE * SubChunk::SIZE;  // 4096
constexpr std::size_t SC_BORDER_SIZE = static_cast<std::size_t>(SubChunk::SIZE) * SubChunk::SIZE;  // 256

// Snapshot of a single SubChunk for meshing.
// Contains the 16x16x16 block/light data plus 6-direction border slices.
struct SubChunkMeshingSnapshot {
    // Core data (16x16x16)
    std::array<BlockID, SC_BLOCK_COUNT> blocks{};
    std::array<uint8_t, SC_BLOCK_COUNT> lightMap{};

    // Horizontal borders (same as before, but now per-sub-chunk height slice)
    std::array<BlockID, SC_BORDER_SIZE> posXBorder{};
    std::array<BlockID, SC_BORDER_SIZE> negXBorder{};
    std::array<BlockID, SC_BORDER_SIZE> posZBorder{};
    std::array<BlockID, SC_BORDER_SIZE> negZBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> posXLightBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> negXLightBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> posZLightBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> negZLightBorder{};

    // Vertical borders (+Y and -Y) — new in Phase 2
    std::array<BlockID, SC_BORDER_SIZE> posYBorder{};
    std::array<BlockID, SC_BORDER_SIZE> negYBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> posYLightBorder{};
    std::array<uint8_t, SC_BORDER_SIZE> negYLightBorder{};

    // Column-relative Y base (scy * 16)
    int yBase = 0;
    // Sub-chunk index within the column (0..15)
    int scy = 0;
    // Whether this is the topmost sub-chunk (for sky light at y=256)
    bool isTopSection = false;
    // Whether this is the bottommost sub-chunk
    bool isBottomSection = false;
};

using SubChunkMeshingSnapshotPtr = std::shared_ptr<SubChunkMeshingSnapshot>;

// Legacy full-column snapshot — kept for backward compatibility during transition
constexpr std::size_t CHUNK_BLOCK_COUNT = static_cast<std::size_t>(Chunk::SIZE_X) * Chunk::SIZE_Y * Chunk::SIZE_Z;
constexpr std::size_t BORDER_YZ_COUNT = static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z;
constexpr std::size_t BORDER_YX_COUNT = static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X;

struct ChunkMeshingSnapshot {
    std::array<BlockID, CHUNK_BLOCK_COUNT> blocks{};
    std::array<uint8_t, CHUNK_BLOCK_COUNT> lightMap{};

    std::array<BlockID, BORDER_YZ_COUNT> posXBorder{};
    std::array<BlockID, BORDER_YZ_COUNT> negXBorder{};
    std::array<BlockID, BORDER_YX_COUNT> posZBorder{};
    std::array<BlockID, BORDER_YX_COUNT> negZBorder{};

    std::array<uint8_t, BORDER_YZ_COUNT> posXLightBorder{};
    std::array<uint8_t, BORDER_YZ_COUNT> negXLightBorder{};
    std::array<uint8_t, BORDER_YX_COUNT> posZLightBorder{};
    std::array<uint8_t, BORDER_YX_COUNT> negZLightBorder{};
};

using ChunkMeshingSnapshotPtr = std::shared_ptr<ChunkMeshingSnapshot>;

struct ChunkMeshData {
    std::vector<BlockVertex> opaqueVertices;
    std::vector<BlockVertex> cutoutVertices;
    std::vector<BlockVertex> transparentVertices;
    uint32_t opaqueFaceCountBeforeGreedy = 0;
    uint32_t opaqueFaceCountAfterGreedy = 0;
    uint32_t transparentFaceCountBeforeGreedy = 0;
    uint32_t transparentFaceCountAfterGreedy = 0;
    uint32_t opaqueVertexCount = 0;
    double buildTimeMs = 0.0;
    bool hasBounds = false;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

class ChunkMesher {
public:
    // --- Per-sub-chunk snapshot capture ---
    static SubChunkMeshingSnapshotPtr captureSubChunkSnapshot(
        const Chunk& chunk,
        int scy,
        const Chunk* neighborPosX,
        const Chunk* neighborNegX,
        const Chunk* neighborPosZ,
        const Chunk* neighborNegZ,
        const World* world = nullptr);

    static SubChunkMeshingSnapshotPtr captureSubChunkSnapshot(
        const Chunk& chunk,
        int scy,
        const World* world = nullptr);

    // --- Per-sub-chunk mesh building ---
    static ChunkMeshData buildSubChunkMeshData(const SubChunkMeshingSnapshot& snapshot);

    // --- Legacy full-column snapshot capture (deprecated) ---
    static ChunkMeshingSnapshotPtr captureSnapshot(
        const Chunk& chunk,
        const Chunk* neighborPosX,
        const Chunk* neighborNegX,
        const Chunk* neighborPosZ,
        const Chunk* neighborNegZ,
        const World* world = nullptr);

    static ChunkMeshingSnapshotPtr captureSnapshot(const Chunk& chunk, const World* world = nullptr);

    // Legacy full-column mesh building (deprecated)
    static ChunkMeshData buildMeshData(const ChunkMeshingSnapshot& snapshot);

    // Direct mesh generation (for synchronous path)
    static void generateSubChunkMesh(Chunk& chunk, int scy);
    static void generateMesh(Chunk& chunk);

    // Check if a sub-chunk should be skipped (Air type)
    static bool shouldSkipSubChunk(const Chunk& chunk, int scy);

private:
    static bool shouldRenderFace(const SubChunkMeshingSnapshot& snapshot,
                                 int nx, int ny, int nz,
                                 BlockID currentId);

    static void addFace(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        int face,
                        const BlockDef& def,
                        int x, int y, int z,
                        const SubChunkMeshingSnapshot& snapshot);

    static void addCrossedQuads(std::vector<BlockVertex>& vertices,
                                const glm::vec3& pos,
                                const BlockDef& def,
                                int x, int y, int z,
                                const SubChunkMeshingSnapshot& snapshot);

    // Legacy helpers (kept for transition)
    static bool shouldRenderFaceLegacy(const ChunkMeshingSnapshot& snapshot,
                                       int nx, int ny, int nz,
                                       BlockID currentId);
};

#endif // MECRAFT_CHUNKMESHER_H


