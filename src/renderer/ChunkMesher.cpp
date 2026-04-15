#include "ChunkMesher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <glm/vec2.hpp>

#include "../world/World.h"

namespace {
struct IVec3 {
    int x;
    int y;
    int z;
};

struct VertexLightData {
    uint8_t ao = 0;
    uint8_t sunLight = 0;
    uint8_t blockLight = 0;
    float sunNormalized = 0.0f;
    float blockNormalized = 0.0f;
};

struct FaceRenderData {
    std::array<VertexLightData, 4> vertices{};
    int tileIndex = 0;
    float layer = 0.0f;
    bool flipDiagonal = false;
};

struct FaceMergeKey {
    BlockID blockId = 0;
    int tileIndex = 0;
    bool flipDiagonal = false;
    std::array<uint8_t, 4> ao{};
    std::array<uint16_t, 4> sun{};
    std::array<uint16_t, 4> block{};
};

struct FaceCell {
    bool valid = false;
    int x = 0;
    int y = 0;
    int z = 0;
    FaceRenderData renderData{};
    FaceMergeKey key{};
};

constexpr int FACE_TOP = 0;
constexpr int FACE_BOTTOM = 1;
constexpr int FACE_FRONT = 2;
constexpr int FACE_BACK = 3;
constexpr int FACE_LEFT = 4;
constexpr int FACE_RIGHT = 5;
constexpr float CROSS_GRASS_MARKER = -1.0f;
constexpr float CROSS_FLOWER_MARKER = -2.0f;
constexpr float kNormalizedQuantizationScale = 180.0f;

constexpr std::array<IVec3, 6> kFaceNormals = {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}}};

constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}
}};

constexpr std::array<glm::vec3, 4> kCrossQuadA = {{{0.1464f, 0.0f, 0.1464f}, {0.8536f, 0.0f, 0.8536f}, {0.8536f, 1.0f, 0.8536f}, {0.1464f, 1.0f, 0.1464f}}};
constexpr std::array<glm::vec3, 4> kCrossQuadB = {{{0.8536f, 0.0f, 0.1464f}, {0.1464f, 0.0f, 0.8536f}, {0.1464f, 1.0f, 0.8536f}, {0.8536f, 1.0f, 0.1464f}}};

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
        return 0;
    }

    if ((x < 0 || x >= Chunk::SIZE_X) && (z < 0 || z >= Chunk::SIZE_Z)) {
        return 0;
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

uint8_t getNeighborAwareLight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return 0;
    }

    if ((x < 0 || x >= Chunk::SIZE_X) && (z < 0 || z >= Chunk::SIZE_Z)) {
        return 0;
    }

    if (x < 0) {
        return snapshot.negXLightBorder[toBorderYZIndex(y, z)];
    }
    if (x >= Chunk::SIZE_X) {
        return snapshot.posXLightBorder[toBorderYZIndex(y, z)];
    }
    if (z < 0) {
        return snapshot.negZLightBorder[toBorderYXIndex(y, x)];
    }
    if (z >= Chunk::SIZE_Z) {
        return snapshot.posZLightBorder[toBorderYXIndex(y, x)];
    }

    return snapshot.lightMap[toIndex(x, y, z)];
}

uint8_t getNeighborSunlight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>((getNeighborAwareLight(snapshot, x, y, z) >> 4) & 0x0F);
}

uint8_t getNeighborBlockLight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>(getNeighborAwareLight(snapshot, x, y, z) & 0x0F);
}

uint8_t computeVertexAO(const bool side1, const bool side2, const bool corner) {
    if (side1 && side2) {
        return 0;
    }
    return static_cast<uint8_t>(3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

bool isSolidForAO(const ChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    const BlockID id = getNeighborAwareBlock(snapshot, x, y, z);
    return BlockRegistry::get(id).isSolid;
}

float lightToNormalized(const uint8_t level) {
    return static_cast<float>(level) / 15.0f;
}

float computeVertexNormalized(const uint8_t base,
                              const uint8_t s1,
                              const uint8_t s2,
                              const uint8_t cn,
                              const bool s1Solid,
                              const bool s2Solid) {
    float avg = 0.0f;
    if (s1Solid && s2Solid) {
        avg = static_cast<float>(base + s1 + s2) / 3.0f;
    } else {
        avg = static_cast<float>(base + s1 + s2 + cn) / 4.0f;
    }
    return avg / 15.0f;
}

uint8_t safeSunLevel(const ChunkMeshingSnapshot& snapshot,
                     const int x,
                     const int y,
                     const int z,
                     const bool isSolid,
                     const uint8_t base) {
    if (!isSolid) {
        if (y >= Chunk::SIZE_Y) {
            return 15;
        }
        if ((x < 0 || x >= Chunk::SIZE_X) && (z < 0 || z >= Chunk::SIZE_Z)) {
            return base;
        }
    }
    return getNeighborSunlight(snapshot, x, y, z);
}

uint8_t safeBlockLevel(const ChunkMeshingSnapshot& snapshot,
                       const int x,
                       const int y,
                       const int z,
                       const bool isSolid,
                       const uint8_t base) {
    if (!isSolid) {
        if (y >= Chunk::SIZE_Y) {
            return 0;
        }
        if ((x < 0 || x >= Chunk::SIZE_X) && (z < 0 || z >= Chunk::SIZE_Z)) {
            return base;
        }
    }
    return getNeighborBlockLight(snapshot, x, y, z);
}

std::array<VertexLightData, 4> computeFaceVertexData(const ChunkMeshingSnapshot& snapshot,
                                                     const int x,
                                                     const int y,
                                                     const int z,
                                                     const int face) {
    const int nx = kFaceNormals[face].x;
    const int ny = kFaceNormals[face].y;
    const int nz = kFaceNormals[face].z;

    const int bx = x + nx;
    const int by = y + ny;
    const int bz = z + nz;

    int a0 = 0;
    int a1 = 0;
    if (ny != 0) {
        a0 = 0;
        a1 = 2;
    } else if (nz != 0) {
        a0 = 0;
        a1 = 1;
    } else {
        a0 = 2;
        a1 = 1;
    }

    std::array<VertexLightData, 4> data{};
    const uint8_t baseSun = getNeighborSunlight(snapshot, bx, by, bz);
    const uint8_t baseBlock = getNeighborBlockLight(snapshot, bx, by, bz);

    for (int i = 0; i < 4; ++i) {
        const glm::vec3 corner = kFaceCorners[face][i];
        const int d0 = (corner[static_cast<size_t>(a0)] > 0.5f) ? 1 : -1;
        const int d1 = (corner[static_cast<size_t>(a1)] > 0.5f) ? 1 : -1;

        int s1[3] = {bx, by, bz};
        int s2[3] = {bx, by, bz};
        int cn[3] = {bx, by, bz};
        s1[a0] += d0;
        s2[a1] += d1;
        cn[a0] += d0;
        cn[a1] += d1;

        const bool side1 = isSolidForAO(snapshot, s1[0], s1[1], s1[2]);
        const bool side2 = isSolidForAO(snapshot, s2[0], s2[1], s2[2]);
        const bool cornerSolid = isSolidForAO(snapshot, cn[0], cn[1], cn[2]);

        data[i].ao = computeVertexAO(side1, side2, cornerSolid);

        const uint8_t s1Sun = safeSunLevel(snapshot, s1[0], s1[1], s1[2], side1, baseSun);
        const uint8_t s2Sun = safeSunLevel(snapshot, s2[0], s2[1], s2[2], side2, baseSun);
        const uint8_t cnSun = safeSunLevel(snapshot, cn[0], cn[1], cn[2], cornerSolid, baseSun);
        const uint8_t s1Block = safeBlockLevel(snapshot, s1[0], s1[1], s1[2], side1, baseBlock);
        const uint8_t s2Block = safeBlockLevel(snapshot, s2[0], s2[1], s2[2], side2, baseBlock);
        const uint8_t cnBlock = safeBlockLevel(snapshot, cn[0], cn[1], cn[2], cornerSolid, baseBlock);

        if (side1 && side2) {
            data[i].sunLight = static_cast<uint8_t>((baseSun + s1Sun + s2Sun) / 3);
            data[i].blockLight = static_cast<uint8_t>((baseBlock + s1Block + s2Block) / 3);
        } else {
            data[i].sunLight = static_cast<uint8_t>((baseSun + s1Sun + s2Sun + cnSun) / 4);
            data[i].blockLight = static_cast<uint8_t>((baseBlock + s1Block + s2Block + cnBlock) / 4);
        }

        data[i].sunNormalized = computeVertexNormalized(baseSun, s1Sun, s2Sun, cnSun, side1, side2);
        data[i].blockNormalized = computeVertexNormalized(baseBlock, s1Block, s2Block, cnBlock, side1, side2);
    }

    return data;
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

FaceRenderData buildFaceRenderData(const ChunkMeshingSnapshot& snapshot,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z,
                                   const int face) {
    FaceRenderData renderData;
    renderData.tileIndex = std::max(0, getFaceTextureIndex(def, face));
    renderData.layer = static_cast<float>(renderData.tileIndex);
    renderData.vertices = computeFaceVertexData(snapshot, x, y, z, face);

    int metric02 = 0;
    int metric13 = 0;
    for (const int index : {0, 2}) {
        metric02 += renderData.vertices[index].ao;
        metric02 += renderData.vertices[index].sunLight;
        metric02 += renderData.vertices[index].blockLight;
    }
    for (const int index : {1, 3}) {
        metric13 += renderData.vertices[index].ao;
        metric13 += renderData.vertices[index].sunLight;
        metric13 += renderData.vertices[index].blockLight;
    }
    renderData.flipDiagonal = metric02 < metric13;
    return renderData;
}

uint16_t quantizeNormalized(const float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint16_t>(std::lround(clamped * kNormalizedQuantizationScale));
}

FaceMergeKey buildFaceMergeKey(const BlockID blockId, const FaceRenderData& renderData) {
    FaceMergeKey key;
    key.blockId = blockId;
    key.tileIndex = renderData.tileIndex;
    key.flipDiagonal = renderData.flipDiagonal;
    for (size_t i = 0; i < renderData.vertices.size(); ++i) {
        key.ao[i] = renderData.vertices[i].ao;
        key.sun[i] = quantizeNormalized(renderData.vertices[i].sunNormalized);
        key.block[i] = quantizeNormalized(renderData.vertices[i].blockNormalized);
    }
    return key;
}

bool sameMergeKey(const FaceMergeKey& lhs, const FaceMergeKey& rhs) {
    return lhs.blockId == rhs.blockId &&
           lhs.tileIndex == rhs.tileIndex &&
           lhs.flipDiagonal == rhs.flipDiagonal &&
           lhs.ao == rhs.ao &&
           lhs.sun == rhs.sun &&
           lhs.block == rhs.block;
}

bool shouldRenderFaceImpl(const ChunkMeshingSnapshot& snapshot,
                          const int nx,
                          const int ny,
                          const int nz,
                          const BlockID currentId) {
    const BlockDef& currentDef = BlockRegistry::get(currentId);
    const BlockID neighborId = getNeighborAwareBlock(snapshot, nx, ny, nz);
    if (currentDef.renderShape == BlockRenderShape::Cube &&
        currentDef.isTransparent &&
        neighborId == currentId) {
        return false;
    }

    if (neighborId == 0) {
        return true;
    }

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

BlockID sampleMissingNeighborBlock(const World* world, const int wx, const int y, const int wz) {
    if (world == nullptr) {
        return 0;
    }
    return world->sampleGeneratedBlock(wx, y, wz);
}

void captureBorders(const Chunk& chunk,
                    ChunkMeshingSnapshot& snapshot,
                    const Chunk* neighborPosX,
                    const Chunk* neighborNegX,
                    const Chunk* neighborPosZ,
                    const Chunk* neighborNegZ,
                    const World* world) {
    const glm::ivec3 offset = chunk.getWorldOffset();
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            const std::size_t index = toBorderYZIndex(y, z);
            snapshot.posXBorder[index] = neighborPosX ? neighborPosX->getBlock(0, y, z)
                                                      : sampleMissingNeighborBlock(world, offset.x + Chunk::SIZE_X, y, offset.z + z);
            snapshot.negXBorder[index] = neighborNegX ? neighborNegX->getBlock(Chunk::SIZE_X - 1, y, z)
                                                      : sampleMissingNeighborBlock(world, offset.x - 1, y, offset.z + z);
            snapshot.posXLightBorder[index] = neighborPosX ? neighborPosX->m_lightMap[neighborPosX->toIndex(0, y, z)] : 0;
            snapshot.negXLightBorder[index] = neighborNegX ? neighborNegX->m_lightMap[neighborNegX->toIndex(Chunk::SIZE_X - 1, y, z)] : 0;
        }
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const std::size_t index = toBorderYXIndex(y, x);
            snapshot.posZBorder[index] = neighborPosZ ? neighborPosZ->getBlock(x, y, 0)
                                                      : sampleMissingNeighborBlock(world, offset.x + x, y, offset.z + Chunk::SIZE_Z);
            snapshot.negZBorder[index] = neighborNegZ ? neighborNegZ->getBlock(x, y, Chunk::SIZE_Z - 1)
                                                      : sampleMissingNeighborBlock(world, offset.x + x, y, offset.z - 1);
            snapshot.posZLightBorder[index] = neighborPosZ ? neighborPosZ->m_lightMap[neighborPosZ->toIndex(x, y, 0)] : 0;
            snapshot.negZLightBorder[index] = neighborNegZ ? neighborNegZ->m_lightMap[neighborNegZ->toIndex(x, y, Chunk::SIZE_Z - 1)] : 0;
        }
    }
}

void expandBounds(ChunkMeshData& meshData, const glm::vec3& blockMin, const glm::vec3& blockMax) {
    if (!meshData.hasBounds) {
        meshData.hasBounds = true;
        meshData.boundsMin = blockMin;
        meshData.boundsMax = blockMax;
        return;
    }

    meshData.boundsMin.x = std::min(meshData.boundsMin.x, blockMin.x);
    meshData.boundsMin.y = std::min(meshData.boundsMin.y, blockMin.y);
    meshData.boundsMin.z = std::min(meshData.boundsMin.z, blockMin.z);
    meshData.boundsMax.x = std::max(meshData.boundsMax.x, blockMax.x);
    meshData.boundsMax.y = std::max(meshData.boundsMax.y, blockMax.y);
    meshData.boundsMax.z = std::max(meshData.boundsMax.z, blockMax.z);
}

void appendFaceVertices(std::vector<BlockVertex>& vertices,
                        const std::array<glm::vec3, 4>& corners,
                        const std::array<glm::vec2, 4>& faceUV,
                        const int face,
                        const FaceRenderData& renderData) {
    const std::array<int, 6> indices = renderData.flipDiagonal
        ? std::array<int, 6>{{1, 2, 3, 1, 3, 0}}
        : std::array<int, 6>{{0, 1, 2, 0, 2, 3}};

    for (const int index : indices) {
        vertices.push_back({
            corners[static_cast<size_t>(index)].x,
            corners[static_cast<size_t>(index)].y,
            corners[static_cast<size_t>(index)].z,
            faceUV[static_cast<size_t>(index)].x,
            faceUV[static_cast<size_t>(index)].y,
            static_cast<float>(face),
            renderData.vertices[static_cast<size_t>(index)].sunNormalized,
            renderData.vertices[static_cast<size_t>(index)].blockNormalized,
            static_cast<float>(renderData.vertices[static_cast<size_t>(index)].ao),
            renderData.layer
        });
    }
}

std::array<glm::vec3, 4> buildGreedyFaceCorners(const int face,
                                                const int x,
                                                const int y,
                                                const int z,
                                                const int width,
                                                const int height) {
    switch (face) {
        case FACE_TOP:
            return {{{static_cast<float>(x), static_cast<float>(y + 1), static_cast<float>(z + height)},
                     {static_cast<float>(x + width), static_cast<float>(y + 1), static_cast<float>(z + height)},
                     {static_cast<float>(x + width), static_cast<float>(y + 1), static_cast<float>(z)},
                     {static_cast<float>(x), static_cast<float>(y + 1), static_cast<float>(z)}}};
        case FACE_BOTTOM:
            return {{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x + width), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x + width), static_cast<float>(y), static_cast<float>(z + height)},
                     {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z + height)}}};
        case FACE_FRONT:
            return {{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z + 1)},
                     {static_cast<float>(x + width), static_cast<float>(y), static_cast<float>(z + 1)},
                     {static_cast<float>(x + width), static_cast<float>(y + height), static_cast<float>(z + 1)},
                     {static_cast<float>(x), static_cast<float>(y + height), static_cast<float>(z + 1)}}};
        case FACE_BACK:
            return {{{static_cast<float>(x + width), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x), static_cast<float>(y + height), static_cast<float>(z)},
                     {static_cast<float>(x + width), static_cast<float>(y + height), static_cast<float>(z)}}};
        case FACE_LEFT:
            return {{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z + width)},
                     {static_cast<float>(x), static_cast<float>(y + height), static_cast<float>(z + width)},
                     {static_cast<float>(x), static_cast<float>(y + height), static_cast<float>(z)}}};
        case FACE_RIGHT:
        default:
            return {{{static_cast<float>(x + 1), static_cast<float>(y), static_cast<float>(z + width)},
                     {static_cast<float>(x + 1), static_cast<float>(y), static_cast<float>(z)},
                     {static_cast<float>(x + 1), static_cast<float>(y + height), static_cast<float>(z)},
                     {static_cast<float>(x + 1), static_cast<float>(y + height), static_cast<float>(z + width)}}};
    }
}

void emitGreedyFace(std::vector<BlockVertex>& vertices,
                    ChunkMeshData& meshData,
                    const FaceCell& cell,
                    const int face,
                    const int width,
                    const int height) {
    const std::array<glm::vec3, 4> corners = buildGreedyFaceCorners(face, cell.x, cell.y, cell.z, width, height);
    const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f},
                                              {static_cast<float>(width), 0.0f},
                                              {static_cast<float>(width), static_cast<float>(height)},
                                              {0.0f, static_cast<float>(height)}}};

    appendFaceVertices(vertices, corners, faceUV, face, cell.renderData);

    glm::vec3 boundsMin = corners[0];
    glm::vec3 boundsMax = corners[0];
    for (const glm::vec3& corner : corners) {
        boundsMin.x = std::min(boundsMin.x, corner.x);
        boundsMin.y = std::min(boundsMin.y, corner.y);
        boundsMin.z = std::min(boundsMin.z, corner.z);
        boundsMax.x = std::max(boundsMax.x, corner.x);
        boundsMax.y = std::max(boundsMax.y, corner.y);
        boundsMax.z = std::max(boundsMax.z, corner.z);
    }
    expandBounds(meshData, boundsMin, boundsMax);
}

void emitLegacyFace(std::vector<BlockVertex>& vertices,
                    const glm::vec3& pos,
                    const int face,
                    const FaceRenderData& renderData) {
    const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    std::array<glm::vec3, 4> corners{};
    for (size_t i = 0; i < corners.size(); ++i) {
        corners[i] = pos + kFaceCorners[static_cast<size_t>(face)][i];
    }
    appendFaceVertices(vertices, corners, faceUV, face, renderData);
}

bool isOpaqueCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && !def.isTransparent;
}

bool isTransparentCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && def.isTransparent;
}

bool populateOpaqueFaceCell(const ChunkMeshingSnapshot& snapshot,
                            const int face,
                            const int x,
                            const int y,
                            const int z,
                            FaceCell& outCell) {
    const BlockID blockId = snapshot.blocks[toIndex(x, y, z)];
    if (blockId == 0) {
        return false;
    }

    const BlockDef& def = BlockRegistry::get(blockId);
    if (!isOpaqueCubeCandidate(def)) {
        return false;
    }

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, blockId)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, def, x, y, z, face);
    outCell.key = buildFaceMergeKey(blockId, outCell.renderData);
    return true;
}

bool populateTransparentFaceCell(const ChunkMeshingSnapshot& snapshot,
                                 const int face,
                                 const int x,
                                 const int y,
                                 const int z,
                                 FaceCell& outCell) {
    const BlockID blockId = snapshot.blocks[toIndex(x, y, z)];
    if (blockId == 0) {
        return false;
    }

    const BlockDef& def = BlockRegistry::get(blockId);
    if (!isTransparentCubeCandidate(def)) {
        return false;
    }

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, blockId)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, def, x, y, z, face);
    outCell.key = buildFaceMergeKey(blockId, outCell.renderData);
    return true;
}

template <typename PopulateCellFn>
void buildCubeGreedyFaces(const ChunkMeshingSnapshot& snapshot,
                          ChunkMeshData& meshData,
                          std::vector<BlockVertex>& targetVertices,
                          uint32_t& faceCountBeforeGreedy,
                          uint32_t& faceCountAfterGreedy,
                          PopulateCellFn&& populateCell) {
    // Pre-allocate reusable buffers outside the slice loop to avoid per-slice heap allocation.
    // The maximum plane size is SIZE_X * SIZE_Y = 16 * 256 = 4096 FaceCells.
    constexpr size_t kMaxPlaneSize = static_cast<size_t>(Chunk::SIZE_X) * Chunk::SIZE_Y;
    std::vector<FaceCell> plane(kMaxPlaneSize);
    std::vector<bool> consumed(kMaxPlaneSize, false);

    auto buildPlane = [&](const int face, const int width, const int height, const int slices, auto&& mapper) {
        const size_t planeSize = static_cast<size_t>(width) * static_cast<size_t>(height);

        for (int slice = 0; slice < slices; ++slice) {
            std::fill_n(consumed.begin(), planeSize, false);
            for (size_t i = 0; i < planeSize; ++i) {
                plane[i] = FaceCell{};
            }

            for (int v = 0; v < height; ++v) {
                for (int u = 0; u < width; ++u) {
                    int x = 0;
                    int y = 0;
                    int z = 0;
                    mapper(slice, u, v, x, y, z);
                    FaceCell& cell = plane[static_cast<size_t>(u) + static_cast<size_t>(v) * static_cast<size_t>(width)];
                    if (populateCell(snapshot, face, x, y, z, cell)) {
                        ++faceCountBeforeGreedy;
                    }
                }
            }

            for (int v = 0; v < height; ++v) {
                for (int u = 0; u < width; ++u) {
                    const size_t startIndex = static_cast<size_t>(u) + static_cast<size_t>(v) * static_cast<size_t>(width);
                    if (consumed[startIndex] || !plane[startIndex].valid) {
                        continue;
                    }

                    int runWidth = 1;
                    while (u + runWidth < width) {
                        const size_t nextIndex = static_cast<size_t>(u + runWidth) + static_cast<size_t>(v) * static_cast<size_t>(width);
                        if (consumed[nextIndex] || !plane[nextIndex].valid ||
                            !sameMergeKey(plane[startIndex].key, plane[nextIndex].key)) {
                            break;
                        }
                        ++runWidth;
                    }

                    int runHeight = 1;
                    bool canGrow = true;
                    while (v + runHeight < height && canGrow) {
                        for (int rowX = 0; rowX < runWidth; ++rowX) {
                            const size_t candidateIndex = static_cast<size_t>(u + rowX) +
                                                          static_cast<size_t>(v + runHeight) * static_cast<size_t>(width);
                            if (consumed[candidateIndex] || !plane[candidateIndex].valid ||
                                !sameMergeKey(plane[startIndex].key, plane[candidateIndex].key)) {
                                canGrow = false;
                                break;
                            }
                        }
                        if (canGrow) {
                            ++runHeight;
                        }
                    }

                    for (int dy = 0; dy < runHeight; ++dy) {
                        for (int dx = 0; dx < runWidth; ++dx) {
                            consumed[static_cast<size_t>(u + dx) +
                                     static_cast<size_t>(v + dy) * static_cast<size_t>(width)] = true;
                        }
                    }

                    emitGreedyFace(targetVertices, meshData, plane[startIndex], face, runWidth, runHeight);
                    ++faceCountAfterGreedy;
                }
            }
        }
    };

    buildPlane(FACE_TOP, Chunk::SIZE_X, Chunk::SIZE_Z, Chunk::SIZE_Y,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u;
                   y = slice;
                   z = v;
               });
    buildPlane(FACE_BOTTOM, Chunk::SIZE_X, Chunk::SIZE_Z, Chunk::SIZE_Y,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u;
                   y = slice;
                   z = v;
               });
    buildPlane(FACE_FRONT, Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u;
                   y = v;
                   z = slice;
               });
    buildPlane(FACE_BACK, Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u;
                   y = v;
                   z = slice;
               });
    buildPlane(FACE_LEFT, Chunk::SIZE_Z, Chunk::SIZE_Y, Chunk::SIZE_X,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = slice;
                   y = v;
                   z = u;
               });
    buildPlane(FACE_RIGHT, Chunk::SIZE_Z, Chunk::SIZE_Y, Chunk::SIZE_X,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = slice;
                   y = v;
                   z = u;
               });
}

void buildOpaqueGreedyFaces(const ChunkMeshingSnapshot& snapshot, ChunkMeshData& meshData) {
    buildCubeGreedyFaces(snapshot,
                         meshData,
                         meshData.opaqueVertices,
                         meshData.opaqueFaceCountBeforeGreedy,
                         meshData.opaqueFaceCountAfterGreedy,
                         populateOpaqueFaceCell);
}

void buildTransparentGreedyFaces(const ChunkMeshingSnapshot& snapshot, ChunkMeshData& meshData) {
    buildCubeGreedyFaces(snapshot,
                         meshData,
                         meshData.transparentVertices,
                         meshData.transparentFaceCountBeforeGreedy,
                         meshData.transparentFaceCountAfterGreedy,
                         populateTransparentFaceCell);
}
}

ChunkMeshingSnapshotPtr ChunkMesher::captureSnapshot(
    const Chunk& chunk,
    const Chunk* neighborPosX,
    const Chunk* neighborNegX,
    const Chunk* neighborPosZ,
    const Chunk* neighborNegZ,
    const World* world) {
    auto snapshot = std::make_shared<ChunkMeshingSnapshot>();
    chunk.copyBlocksTo(snapshot->blocks);
    snapshot->lightMap = chunk.m_lightMap;

    captureBorders(chunk, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, world);
    return snapshot;
}

ChunkMeshingSnapshotPtr ChunkMesher::captureSnapshot(const Chunk& chunk, const World* world) {
    return captureSnapshot(chunk,
                           chunk.neighbors[0], chunk.neighbors[1],
                           chunk.neighbors[2], chunk.neighbors[3],
                           world);
}

ChunkMeshData ChunkMesher::buildMeshData(const ChunkMeshingSnapshot& snapshot) {
    const auto startTime = std::chrono::steady_clock::now();

    ChunkMeshData meshData;
    // Conservative initial reserves — avoids massive over-allocation for sparse chunks.
    // Greedy meshing typically reduces vertex count significantly.
    meshData.opaqueVertices.reserve(8192);
    meshData.cutoutVertices.reserve(2048);
    meshData.transparentVertices.reserve(4096);

    buildOpaqueGreedyFaces(snapshot, meshData);
    buildTransparentGreedyFaces(snapshot, meshData);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID blockId = snapshot.blocks[toIndex(x, y, z)];
                if (blockId == 0) {
                    continue;
                }

                const BlockDef& def = BlockRegistry::get(blockId);
                if (def.renderShape == BlockRenderShape::Cross) {
                    addCrossedQuads(meshData.cutoutVertices,
                                    glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                    def,
                                    x,
                                    y,
                                    z,
                                    snapshot);
                    expandBounds(meshData,
                                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
                    continue;
                }

                if (isOpaqueCubeCandidate(def) || isTransparentCubeCandidate(def)) {
                    continue;
                }

                const bool transparent = def.isTransparent;
                for (int face = 0; face < 6; ++face) {
                    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
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
                            x,
                            y,
                            z,
                            snapshot);
                    expandBounds(meshData,
                                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
                }
            }
        }
    }

    meshData.opaqueVertexCount = static_cast<uint32_t>(meshData.opaqueVertices.size());
    meshData.buildTimeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
    return meshData;
}

void ChunkMesher::generateMesh(Chunk& chunk) {
    const ChunkMeshingSnapshotPtr snapshot = captureSnapshot(chunk);
    ChunkMeshData meshData = buildMeshData(*snapshot);
    ChunkMesh mesh;
    mesh.upload(meshData.opaqueVertices);
    mesh.uploadCutout(meshData.cutoutVertices);
    mesh.uploadTransparent(meshData.transparentVertices);
    mesh.hasBounds = meshData.hasBounds;
    mesh.boundsMin = meshData.boundsMin;
    mesh.boundsMax = meshData.boundsMax;
    chunk.setMesh(mesh);
}

bool ChunkMesher::shouldRenderFace(const ChunkMeshingSnapshot& snapshot,
                                   const int nx,
                                   const int ny,
                                   const int nz,
                                   const BlockID currentId) {
    return shouldRenderFaceImpl(snapshot, nx, ny, nz, currentId);
}

void ChunkMesher::addFace(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const int face,
                          const BlockDef& def,
                          const int x,
                          const int y,
                          const int z,
                          const ChunkMeshingSnapshot& snapshot) {
    const FaceRenderData renderData = buildFaceRenderData(snapshot, def, x, y, z, face);
    emitLegacyFace(vertices, pos, face, renderData);
}

void ChunkMesher::addCrossedQuads(std::vector<BlockVertex>& vertices,
                                  const glm::vec3& pos,
                                  const BlockDef& def,
                                  const int x,
                                  const int y,
                                  const int z,
                                  const ChunkMeshingSnapshot& snapshot) {
    int tileIndex = def.texTop;
    if (tileIndex < 0) {
        tileIndex = 0;
    }

    const float layer = static_cast<float>(tileIndex);

    uint8_t sunLevel = getNeighborSunlight(snapshot, x, y, z);
    uint8_t blockLevel = getNeighborBlockLight(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        const int nx = x + kFaceNormals[static_cast<size_t>(d)].x;
        const int ny = y + kFaceNormals[static_cast<size_t>(d)].y;
        const int nz = z + kFaceNormals[static_cast<size_t>(d)].z;
        sunLevel = std::max(sunLevel, getNeighborSunlight(snapshot, nx, ny, nz));
        blockLevel = std::max(blockLevel, getNeighborBlockLight(snapshot, nx, ny, nz));
    }
    const float sunNormalized = lightToNormalized(sunLevel);
    const float blockNormalized = lightToNormalized(blockLevel);

    const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};
    const float crossMarker = def.useGrassTint ? CROSS_GRASS_MARKER : CROSS_FLOWER_MARKER;

    const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
        for (const int index : indices) {
            vertices.push_back({
                pos.x + corners[static_cast<size_t>(index)].x,
                pos.y + corners[static_cast<size_t>(index)].y,
                pos.z + corners[static_cast<size_t>(index)].z,
                quadUV[static_cast<size_t>(index)].x,
                quadUV[static_cast<size_t>(index)].y,
                crossMarker,
                sunNormalized,
                blockNormalized,
                3.0f,
                layer
            });
        }
    };

    emitQuad(kCrossQuadA);
    emitQuad(kCrossQuadB);
}
