#include "ChunkMesher.h"

#include <array>
#include <cstddef>
#include <utility>

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

constexpr std::array<IVec3, 6> kFaceAxisA = {{{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<IVec3, 6> kFaceAxisB = {{{0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}}};

constexpr std::array<std::pair<int, int>, 4> kCornerSigns = {{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

size_t toIndex(const int x, const int y, const int z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(z) * Chunk::SIZE_X +
           static_cast<size_t>(y) * Chunk::SIZE_X * Chunk::SIZE_Z;
}

size_t toBorderYZIndex(const int y, const int z) {
    return static_cast<size_t>(z) + static_cast<size_t>(y) * Chunk::SIZE_Z;
}

size_t toBorderYXIndex(const int y, const int x) {
    return static_cast<size_t>(x) + static_cast<size_t>(y) * Chunk::SIZE_X;
}

IVec3 add(const IVec3& a, const IVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

IVec3 mul(const IVec3& a, const int scalar) {
    return {a.x * scalar, a.y * scalar, a.z * scalar};
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

bool isSolidForAO(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return false;
    }
    const BlockID id = getNeighborAwareBlock(snapshot, x, y, z);
    return BlockRegistry::get(id).isSolid;
}

uint8_t calcAOValue(const bool side1, const bool side2, const bool corner) {
    if (side1 && side2) {
        return 0;
    }
    const uint8_t blocked = static_cast<uint8_t>(side1) + static_cast<uint8_t>(side2) + static_cast<uint8_t>(corner);
    return static_cast<uint8_t>(3 - blocked);
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

void computeFaceAO(const ChunkMeshingSnapshot& snapshot, const int x, const int y, const int z, const int face, float outAO[4]) {
    const IVec3 blockPos{x, y, z};
    const IVec3 normal = kFaceNormals[face];
    const IVec3 axisA = kFaceAxisA[face];
    const IVec3 axisB = kFaceAxisB[face];

    const IVec3 base = add(blockPos, normal);

    for (int i = 0; i < 4; ++i) {
        const int signA = kCornerSigns[i].first;
        const int signB = kCornerSigns[i].second;

        const IVec3 side1Pos = add(base, mul(axisA, signA));
        const IVec3 side2Pos = add(base, mul(axisB, signB));
        const IVec3 cornerPos = add(add(base, mul(axisA, signA)), mul(axisB, signB));

        const bool side1 = isSolidForAO(snapshot, side1Pos.x, side1Pos.y, side1Pos.z);
        const bool side2 = isSolidForAO(snapshot, side2Pos.x, side2Pos.y, side2Pos.z);
        const bool corner = isSolidForAO(snapshot, cornerPos.x, cornerPos.y, cornerPos.z);

        outAO[i] = static_cast<float>(calcAOValue(side1, side2, corner));
    }
}

uint8_t getSunlight(const ChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    return static_cast<uint8_t>((snapshot.lightMap[toIndex(x, y, z)] >> 4) & 0x0F);
}

uint8_t getBlockLight(const ChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    return static_cast<uint8_t>(snapshot.lightMap[toIndex(x, y, z)] & 0x0F);
}

void captureBorders(const Chunk& chunk, ChunkMeshingSnapshot& snapshot) {
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            const size_t index = toBorderYZIndex(y, z);
            const Chunk* posX = chunk.neighbors[0];
            const Chunk* negX = chunk.neighbors[1];
            snapshot.posXBorder[index] = posX ? posX->getBlock(0, y, z) : BlockType::AIR;
            snapshot.negXBorder[index] = negX ? negX->getBlock(Chunk::SIZE_X - 1, y, z) : BlockType::AIR;
        }
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const size_t index = toBorderYXIndex(y, x);
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
                const size_t index = toIndex(x, y, z);
                snapshot.blocks[index] = chunk.getBlock(x, y, z);
                snapshot.lightMap[index] = static_cast<uint8_t>((chunk.getSunlight(x, y, z) << 4) | chunk.getBlockLight(x, y, z));
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

                    const uint8_t sunlight = getSunlight(snapshot, x, y, z);
                    const uint8_t blockLight = getBlockLight(snapshot, x, y, z);

                    float aoValues[4] = {3.0f, 3.0f, 3.0f, 3.0f};
                    computeFaceAO(snapshot, x, y, z, face, aoValues);

                    auto& target = transparent ? meshData.transparentVertices : meshData.opaqueVertices;
                    addFace(target,
                            glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                            face,
                            def,
                            atlas,
                            sunlight,
                            blockLight,
                            aoValues);
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
    chunk.setMesh(std::move(mesh));
}

bool ChunkMesher::shouldRenderFace(const ChunkMeshingSnapshot& snapshot,
                                   const int nx,
                                   const int ny,
                                   const int nz,
                                   const BlockID currentId) {
    const BlockID neighborId = getNeighborAwareBlock(snapshot, nx, ny, nz);
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
                          const TextureAtlas& atlas,
                          const uint8_t sunlight,
                          const uint8_t blockLight,
                          const float aoValues[4]) {
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
            static_cast<float>(face),
            static_cast<float>(sunlight),
            static_cast<float>(blockLight),
            aoValues[index]
        });
    }
}

uint8_t ChunkMesher::calcAO(const bool side1, const bool side2, const bool corner) {
    if (side1 && side2) {
        return 0;
    }
    const uint8_t blocked = static_cast<uint8_t>(side1) + static_cast<uint8_t>(side2) + static_cast<uint8_t>(corner);
    return static_cast<uint8_t>(3 - blocked);
}




