#include "ChunkMesher.h"

#include <array>
#include <glm/vec2.hpp>

namespace {
struct IVec3 {
    int x;
    int y;
    int z;
};

constexpr int FACE_TOP = 0;
constexpr int FACE_BOTTOM = 1;
constexpr int FACE_FRONT = 2;
constexpr int FACE_BACK = 3;
constexpr int FACE_LEFT = 4;
constexpr int FACE_RIGHT = 5;

constexpr std::array<IVec3, 6> kFaceNormals = {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}}};

constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // bottom
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // front (+z)
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // back (-z)
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // left (-x)
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}  // right (+x)
}};

std::size_t toIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(z) * Chunk::SIZE_X +
           static_cast<std::size_t>(y) * Chunk::SIZE_X * Chunk::SIZE_Z;
}

std::size_t toBorderYZIndex(const int y, const int z) {
    return static_cast<std::size_t>(z) + static_cast<std::size_t>(y) * Chunk::SIZE_Z;
}

std::size_t toBorderYXIndex(const int y, const int x) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * Chunk::SIZE_X;
}

BlockID getNeighborAwareBlock(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return BlockType::AIR;
    }

    // Snapshot only stores +/-X and +/-Z borders, not diagonal corner neighbors.
    if ((x < 0 || x >= Chunk::SIZE_X) && (z < 0 || z >= Chunk::SIZE_Z)) {
        return BlockType::AIR;
    }

    if (x < 0) {
        return snapshot.negXBorder[toBorderYZIndex(y, z)];
    }
    if (x >= Chunk::SIZE_X) {
        return snapshot.posXBorder[toBorderYZIndex(y, z)];
    }
    if (z < 0) {
        return snapshot.negZBorder[toBorderYXIndex(y, x)];
    }
    if (z >= Chunk::SIZE_Z) {
        return snapshot.posZBorder[toBorderYXIndex(y, x)];
    }

    return snapshot.blocks[toIndex(x, y, z)];
}

int getFaceTextureIndex(const BlockDef& def, const int face) {
    switch (face) {
        case FACE_TOP:
            return def.texTop;
        case FACE_BOTTOM:
            return def.texBottom;
        case FACE_FRONT:
            return def.texFront;
        case FACE_BACK:
            return def.texBack;
        case FACE_LEFT:
            return def.texLeft;
        case FACE_RIGHT:
            return def.texRight;
        default:
            return 0;
    }
}

void captureBorders(const Chunk& chunk, ChunkMeshingSnapshot& snapshot) {
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            const std::size_t index = toBorderYZIndex(y, z);
            const Chunk* posX = chunk.neighbors[0];
            const Chunk* negX = chunk.neighbors[1];
            snapshot.posXBorder[index] = posX ? posX->getBlock(0, y, z) : BlockType::AIR;
            snapshot.negXBorder[index] = negX ? negX->getBlock(Chunk::SIZE_X - 1, y, z) : BlockType::AIR;
        }
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const std::size_t index = toBorderYXIndex(y, x);
            const Chunk* posZ = chunk.neighbors[2];
            const Chunk* negZ = chunk.neighbors[3];
            snapshot.posZBorder[index] = posZ ? posZ->getBlock(x, y, 0) : BlockType::AIR;
            snapshot.negZBorder[index] = negZ ? negZ->getBlock(x, y, Chunk::SIZE_Z - 1) : BlockType::AIR;
        }
    }
}
}

ChunkMeshingSnapshot ChunkMesher::captureSnapshot(const Chunk& chunk) {
    ChunkMeshingSnapshot snapshot;

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const std::size_t index = toIndex(x, y, z);
                snapshot.blocks[index] = chunk.getBlock(x, y, z);
            }
        }
    }

    captureBorders(chunk, snapshot);
    return snapshot;
}

ChunkMeshData ChunkMesher::buildMeshData(const ChunkMeshingSnapshot& snapshot, const TextureAtlas& atlas) {
    ChunkMeshData meshData;
    meshData.opaqueVertices.reserve(Chunk::SIZE_X * Chunk::SIZE_Z * 256);
    meshData.transparentVertices.reserve(Chunk::SIZE_X * Chunk::SIZE_Z * 64);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID blockId = snapshot.blocks[toIndex(x, y, z)];
                if (blockId == BlockType::AIR) {
                    continue;
                }

                const BlockDef& def = BlockRegistry::get(blockId);
                const bool transparent = def.isTransparent;

                for (int face = 0; face < 6; ++face) {
                    const IVec3 normal = kFaceNormals[face];
                    const int nx = x + normal.x;
                    const int ny = y + normal.y;
                    const int nz = z + normal.z;

                    if (!shouldRenderFace(snapshot, nx, ny, nz, blockId)) {
                        continue;
                    }

                    auto& target = transparent ? meshData.transparentVertices : meshData.opaqueVertices;
                    addFace(target,
                            glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                            face,
                            def,
                            atlas);
                }
            }
        }
    }

    return meshData;
}

void ChunkMesher::generateMesh(Chunk& chunk, const TextureAtlas& atlas) {
    const ChunkMeshingSnapshot snapshot = captureSnapshot(chunk);
    ChunkMeshData meshData = buildMeshData(snapshot, atlas);
    ChunkMesh mesh;
    mesh.upload(meshData.opaqueVertices);
    mesh.uploadTransparent(meshData.transparentVertices);
    chunk.setMesh(mesh);
}

bool ChunkMesher::shouldRenderFace(const ChunkMeshingSnapshot& snapshot,
                                   const int nx,
                                   const int ny,
                                   const int nz,
                                   const BlockID currentId) {
    const BlockID neighborId = getNeighborAwareBlock(snapshot, nx, ny, nz);
    if (currentId == BlockType::WATER && neighborId == BlockType::WATER) {
        return false;
    }

    if (neighborId == BlockType::AIR) {
        return true;
    }

    const BlockDef& currentDef = BlockRegistry::get(currentId);
    const BlockDef& neighborDef = BlockRegistry::get(neighborId);

    if (!neighborDef.isSolid) {
        return true;
    }

    if (neighborDef.isTransparent) {
        if (!currentDef.isTransparent) {
            return true;
        }
        return neighborId != currentId;
    }

    return false;
}

void ChunkMesher::addFace(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const int face,
                          const BlockDef& def,
                          const TextureAtlas& atlas) {
    int tileIndex = getFaceTextureIndex(def, face);
    if (tileIndex < 0) {
        tileIndex = 0;
    }

    const auto uv = atlas.getUV(tileIndex);
    const float uMin = uv.first.x;
    const float vMin = uv.first.y;
    const float uMax = uv.second.x;
    const float vMax = uv.second.y;

    const std::array<glm::vec2, 4> faceUV = {{{uMin, vMin}, {uMax, vMin}, {uMax, vMax}, {uMin, vMax}}};
    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};

    for (const int index : indices) {
        const glm::vec3 local = kFaceCorners[face][index];
        const glm::vec2 uvCoord = faceUV[index];

        vertices.push_back({
            pos.x + local.x,
            pos.y + local.y,
            pos.z + local.z,
            uvCoord.x,
            uvCoord.y,
            static_cast<float>(face)
        });
    }
}




