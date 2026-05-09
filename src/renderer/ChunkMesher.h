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
constexpr int SC_HALO_SIZE = SubChunk::SIZE + 2;
constexpr std::size_t SC_HALO_BLOCK_COUNT = static_cast<std::size_t>(SC_HALO_SIZE) *
                                            SC_HALO_SIZE * SC_HALO_SIZE;

// Snapshot of a single SubChunk for meshing.
// Contains the 16x16x16 block/light data plus 6-direction border slices.
struct SubChunkMeshingSnapshot {
    // Core data (16x16x16)
    std::array<BlockID, SC_BLOCK_COUNT> blocks{};
    std::array<BlockID, SC_BLOCK_COUNT> fluidBlocks{};  // Dedicated fluid layer
    std::array<uint8_t, SC_BLOCK_COUNT> lightMap{};
    std::array<BlockID, SC_HALO_BLOCK_COUNT> haloBlocks{};
    std::array<BlockID, SC_HALO_BLOCK_COUNT> haloFluidBlocks{};  // Halo fluid layer
    std::array<uint8_t, SC_HALO_BLOCK_COUNT> haloLightMap{};

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
    int worldOffsetX = 0;
    int worldOffsetZ = 0;
    const World* world = nullptr;
    // Sub-chunk index within the column (0..15)
    int scy = 0;
    // Whether this is the topmost sub-chunk (for sky light at y=256)
    bool isTopSection = false;
    // Whether this is the bottommost sub-chunk
    bool isBottomSection = false;
};

using SubChunkMeshingSnapshotPtr = std::shared_ptr<SubChunkMeshingSnapshot>;

struct ChunkMeshData {

    std::vector<BlockVertex> opaqueVertices;
    std::vector<BlockVertex> cutoutVertices;
    std::vector<BlockVertex> cutoutDistanceVertices;
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

    // Direct mesh generation (for synchronous path)
    static void generateSubChunkMesh(Chunk& chunk, int scy);

    // Check if a sub-chunk should be skipped (air or fully occluded semantic-solid sub-chunk)
    static bool shouldSkipSubChunk(const Chunk& chunk, int scy);
};

namespace ChunkMeshBuilders {
void buildCross(ChunkMeshData& meshData,
                const SubChunkMeshingSnapshot& snapshot,
                BlockID blockId,
                const BlockDef& def,
                int x,
                int y,
                int z);
void buildTorch(ChunkMeshData& meshData,
                const SubChunkMeshingSnapshot& snapshot,
                BlockID blockId,
                const BlockDef& def,
                int x,
                int y,
                int z);
void buildWater(ChunkMeshData& meshData,
                const SubChunkMeshingSnapshot& snapshot,
                BlockID blockId,
                const BlockDef& def,
                int x,
                int y,
                int z);
void buildUnitFaces(ChunkMeshData& meshData,
                    const SubChunkMeshingSnapshot& snapshot,
                    BlockID blockId,
                    const BlockDef& def,
                    int x,
                    int y,
                    int z);
}


#endif // MECRAFT_CHUNKMESHER_H


