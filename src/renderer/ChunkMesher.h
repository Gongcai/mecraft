#ifndef MECRAFT_CHUNKMESHER_H
#define MECRAFT_CHUNKMESHER_H

#include <array>
#include <vector>

#include <glm/vec3.hpp>

#include "../resource/ResourceMgr.h"
#include "../world/Chunk.h"

constexpr size_t CHUNK_BLOCK_COUNT = static_cast<size_t>(Chunk::SIZE_X) * Chunk::SIZE_Y * Chunk::SIZE_Z;

struct ChunkMeshingSnapshot {
    std::array<BlockID, CHUNK_BLOCK_COUNT> blocks{};
    std::array<uint8_t, CHUNK_BLOCK_COUNT> lightMap{};

    std::array<BlockID, static_cast<size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> posXBorder{};
    std::array<BlockID, static_cast<size_t>(Chunk::SIZE_Y) * Chunk::SIZE_Z> negXBorder{};
    std::array<BlockID, static_cast<size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> posZBorder{};
    std::array<BlockID, static_cast<size_t>(Chunk::SIZE_Y) * Chunk::SIZE_X> negZBorder{};
};

struct ChunkMeshData {
    std::vector<BlockVertex> opaqueVertices;
    std::vector<BlockVertex> transparentVertices;
};

class ChunkMesher {
public:
    static ChunkMeshingSnapshot captureSnapshot(const Chunk& chunk);
    static ChunkMeshData buildMeshData(const ChunkMeshingSnapshot& snapshot, const TextureAtlas& atlas);
    static void generateMesh(Chunk& chunk, const TextureAtlas& atlas);

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
                        const TextureAtlas& atlas,
                        uint8_t sunlight,
                        uint8_t blockLight,
                        const float aoValues[4]);

    static uint8_t calcAO(bool side1, bool side2, bool corner);
};

#endif // MECRAFT_CHUNKMESHER_H


