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

constexpr std::size_t CHUNK_BLOCK_COUNT = static_cast<std::size_t>(Chunk::SIZE_X) * Chunk::SIZE_Y * Chunk::SIZE_Z;

constexpr std::size_t BORDER_YZ_COUNT = static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z;
constexpr std::size_t BORDER_YX_COUNT = static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X;

struct ChunkMeshingSnapshot {
    std::array<BlockID, CHUNK_BLOCK_COUNT> blocks{};
    std::array<uint8_t, CHUNK_BLOCK_COUNT> lightMap{}; // same packing as Chunk::m_lightMap

    // Border block data for cross-chunk AO and face culling (fixed arrays, no heap alloc)
    std::array<BlockID, BORDER_YZ_COUNT> posXBorder{};
    std::array<BlockID, BORDER_YZ_COUNT> negXBorder{};
    std::array<BlockID, BORDER_YX_COUNT> posZBorder{};
    std::array<BlockID, BORDER_YX_COUNT> negZBorder{};

    // Border light data for cross-chunk AO and light lookups
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
    /// Capture a snapshot of chunk data for async meshing.
    /// When called from a worker thread, pass explicit neighbor pointers instead of
    /// relying on chunk.neighbors[] (which may be mutated by the main thread).
    static ChunkMeshingSnapshotPtr captureSnapshot(
        const Chunk& chunk,
        const Chunk* neighborPosX,
        const Chunk* neighborNegX,
        const Chunk* neighborPosZ,
        const Chunk* neighborNegZ,
        const World* world = nullptr);

    /// Convenience overload that reads chunk.neighbors[] — main-thread only.
    static ChunkMeshingSnapshotPtr captureSnapshot(const Chunk& chunk, const World* world = nullptr);

    static ChunkMeshData buildMeshData(const ChunkMeshingSnapshot& snapshot);
    static void generateMesh(Chunk& chunk);

private:
    static bool shouldRenderFace(const ChunkMeshingSnapshot& snapshot,
                                 int nx,
                                 int ny,
                                 int nz,
                                 BlockID currentId);

    static void addFace(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        int face,
                        const BlockDef& def,
                        int x, int y, int z,
                        const ChunkMeshingSnapshot& snapshot);

    static void addCrossedQuads(std::vector<BlockVertex>& vertices,
                                const glm::vec3& pos,
                                const BlockDef& def,
                                int x, int y, int z,
                                const ChunkMeshingSnapshot& snapshot);
};

#endif // MECRAFT_CHUNKMESHER_H


