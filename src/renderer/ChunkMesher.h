#ifndef MECRAFT_CHUNKMESHER_H
#define MECRAFT_CHUNKMESHER_H

#include <array>
#include <vector>

#include <glm/vec3.hpp>

#include "../resource/ResourceMgr.h"
#include "../world/Chunk.h"

constexpr std::size_t CHUNK_BLOCK_COUNT = static_cast<std::size_t>(Chunk::SIZE_X) * Chunk::SIZE_Y * Chunk::SIZE_Z;

struct ChunkMeshingSnapshot {
    std::array<BlockID, CHUNK_BLOCK_COUNT> blocks{};
    std::array<uint8_t, CHUNK_BLOCK_COUNT> lightMap{}; // same packing as Chunk::m_lightMap

    std::array<BlockID, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> posXBorder{};
    std::array<BlockID, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> negXBorder{};
    std::array<BlockID, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> posZBorder{};
    std::array<BlockID, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> negZBorder{};

    // Border light data for cross-chunk AO and light lookups
    std::array<uint8_t, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> posXLightBorder{};
    std::array<uint8_t, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> negXLightBorder{};
    std::array<uint8_t, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> posZLightBorder{};
    std::array<uint8_t, static_cast<std::size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> negZLightBorder{};
};

struct ChunkMeshData {
    std::vector<BlockVertex> opaqueVertices;
    std::vector<BlockVertex> cutoutVertices;
    std::vector<BlockVertex> transparentVertices;
    bool hasBounds = false;
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

class ChunkMesher {
public:
    static ChunkMeshingSnapshot captureSnapshot(const Chunk& chunk);
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


