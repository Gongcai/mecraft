#include "ChunkMesher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include "MeshBuilderRegistry.h"
#include "../world/FluidFlow.h"
#include "../world/BlockStateRegistry.h"
#include "../world/FluidState.h"
#include "../world/PropIndices.h"
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
    float animationFrameCount = 1.0f;
    float animationFps = 0.0f;
    float animated = 0.0f;
    bool flipDiagonal = false;
    uint8_t uvQuarterTurns = 0;
};

struct FaceMergeKey {
    BlockID blockId = 0;
    int tileIndex = 0;
    bool flipDiagonal = false;
    uint8_t uvQuarterTurns = 0;
    std::array<uint8_t, 4> ao{};
    std::array<uint16_t, 4> sun{};
    std::array<uint16_t, 4> block{};
    uint64_t hash = 0;
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
constexpr float kGreedyFaceOverlapEpsilon = 1.0f / 1024.0f;

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

// Sub-chunk local index (16x16x16)
std::size_t scToIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(z) * SubChunk::SIZE +
           static_cast<std::size_t>(y) * SubChunk::SIZE * SubChunk::SIZE;
}

std::size_t haloToIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x + 1) +
           static_cast<std::size_t>(z + 1) * SC_HALO_SIZE +
           static_cast<std::size_t>(y + 1) * SC_HALO_SIZE * SC_HALO_SIZE;
}

// Border index helpers for sub-chunk borders (16x16 slice)
std::size_t toBorderXZIndex(const int x, const int z) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * SubChunk::SIZE;
}

// ======================== Neighbor-aware block/light lookup (sub-chunk) ========================

BlockID getNeighborAwareBlockSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    // Out of Y range — above top = air, below bottom = stone/air
    if (y < 0) {
        if (snapshot.isBottomSection) return 0;
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.negYBorder[toBorderXZIndex(x, z)];
    }
    if (y >= SubChunk::SIZE) {
        if (snapshot.isTopSection) return 0;  // Above world = air
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.posYBorder[toBorderXZIndex(x, z)];
    }

    // X borders
    if (x < 0) {
        if (z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.negXBorder[toBorderXZIndex(y, z)];
    }
    if (x >= SubChunk::SIZE) {
        if (z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.posXBorder[toBorderXZIndex(y, z)];
    }

    // Z borders
    if (z < 0) {
        return snapshot.negZBorder[toBorderXZIndex(y, x)];
    }
    if (z >= SubChunk::SIZE) {
        return snapshot.posZBorder[toBorderXZIndex(y, x)];
    }

    return snapshot.blocks[scToIndex(x, y, z)];
}

uint8_t getNeighborAwareLightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (y < 0) {
        if (snapshot.isBottomSection) return 0;
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.negYLightBorder[toBorderXZIndex(x, z)];
    }
    if (y >= SubChunk::SIZE) {
        if (snapshot.isTopSection) return 0;
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.posYLightBorder[toBorderXZIndex(x, z)];
    }

    if (x < 0) {
        if (z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.negXLightBorder[toBorderXZIndex(y, z)];
    }
    if (x >= SubChunk::SIZE) {
        if (z < 0 || z >= SubChunk::SIZE) return 0;
        return snapshot.posXLightBorder[toBorderXZIndex(y, z)];
    }

    if (z < 0) {
        return snapshot.negZLightBorder[toBorderXZIndex(y, x)];
    }
    if (z >= SubChunk::SIZE) {
        return snapshot.posZLightBorder[toBorderXZIndex(y, x)];
    }

    return snapshot.lightMap[scToIndex(x, y, z)];
}

uint8_t getNeighborSunlightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>((getNeighborAwareLightSC(snapshot, x, y, z) >> 4) & 0x0F);
}

uint8_t getNeighborBlockLightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>(getNeighborAwareLightSC(snapshot, x, y, z) & 0x0F);
}

BlockID getResolvedBlockSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (x < -1 || x > SubChunk::SIZE ||
        y < -1 || y > SubChunk::SIZE ||
        z < -1 || z > SubChunk::SIZE) {
        return 0;
    }
    return snapshot.haloBlocks[haloToIndex(x, y, z)];
}

BlockID getResolvedFluidSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (x < -1 || x > SubChunk::SIZE ||
        y < -1 || y > SubChunk::SIZE ||
        z < -1 || z > SubChunk::SIZE) {
        return 0;
    }
    const BlockID fluidId = snapshot.haloFluidBlocks[haloToIndex(x, y, z)];
    if (fluidId != 0) {
        return fluidId;
    }
    const BlockID blockId = snapshot.haloBlocks[haloToIndex(x, y, z)];
    if (FluidState::decode(blockId).kind != FluidKind::None) {
        return blockId;
    }
    return 0;
}

uint8_t getResolvedLightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (x < -1 || x > SubChunk::SIZE ||
        y < -1 || y > SubChunk::SIZE ||
        z < -1 || z > SubChunk::SIZE) {
        return 0;
    }
    return snapshot.haloLightMap[haloToIndex(x, y, z)];
}

uint8_t getResolvedSunlightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>((getResolvedLightSC(snapshot, x, y, z) >> 4) & 0x0F);
}

uint8_t getResolvedBlockLightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return static_cast<uint8_t>(getResolvedLightSC(snapshot, x, y, z) & 0x0F);
}

// ======================== AO / light computation ========================

uint8_t computeVertexAO(const bool side1, const bool side2, const bool corner) {
    if (side1 && side2) {
        return 0;
    }
    return static_cast<uint8_t>(3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

bool isSolidForAO(const SubChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    const BlockID id = getResolvedBlockSC(snapshot, x, y, z);
    return BlockRegistry::getFast(id).isSolid;
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

// Check if coordinates fall in a "gap" that the 6-direction border data cannot
// resolve.  This happens when two or more axes are simultaneously out of range
// (e.g. y>=SIZE and x<0, or x<0 and z<0, or all three at once).
// The 6-direction borders only store single-axis neighbours; any multi-axis
// out-of-bounds position has no stored data and would return 0, producing dark
// seams at chunk/sub-chunk boundaries — particularly visible on flat surfaces
// at sea level where the Y boundary and chunk X/Z boundary coincide.
bool isUnresolvablePosition(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    const bool xOut = (x < 0 || x >= SubChunk::SIZE);
    const bool yOut = (y < 0 || y >= SubChunk::SIZE);
    const bool zOut = (z < 0 || z >= SubChunk::SIZE);

    // Two or more axes out of range — no border data covers this
    if ((xOut && zOut) || (xOut && yOut) || (yOut && zOut)) {
        return true;
    }

    // Single-axis Y out of range where the section has no neighbour above/below
    if (yOut) {
        if (y < 0 && snapshot.isBottomSection) return true;
        if (y >= SubChunk::SIZE && snapshot.isTopSection) return true;
    }

    return false;
}

uint8_t safeSunLevel(const SubChunkMeshingSnapshot& snapshot,
                     const int x,
                     const int y,
                     const int z,
                     const bool isSolid,
                     const uint8_t base) {
    if (!isSolid) {
        // Above the top of the world — full sky light
        if (y >= SubChunk::SIZE && snapshot.isTopSection) {
            return 15;
        }
    }
    return getResolvedSunlightSC(snapshot, x, y, z);
}

uint8_t safeBlockLevel(const SubChunkMeshingSnapshot& snapshot,
                       const int x,
                       const int y,
                       const int z,
                       const bool isSolid,
                       const uint8_t base) {
    if (!isSolid) {
        if (y >= SubChunk::SIZE && snapshot.isTopSection) {
            return 0;
        }
    }
    return getResolvedBlockLightSC(snapshot, x, y, z);
}

std::array<VertexLightData, 4> computeFaceVertexData(const SubChunkMeshingSnapshot& snapshot,
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
    const uint8_t baseSun = getResolvedSunlightSC(snapshot, bx, by, bz);
    const uint8_t baseBlock = getResolvedBlockLightSC(snapshot, bx, by, bz);

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

int getFaceTextureIndex(const StateTextureIndices& textures, const int face) {
    switch (face) {
        case FACE_TOP:    return textures.texTop;
        case FACE_BOTTOM: return textures.texBottom;
        case FACE_FRONT:  return textures.texFront;
        case FACE_BACK:   return textures.texBack;
        case FACE_LEFT:   return textures.texLeft;
        case FACE_RIGHT:  return textures.texRight;
        default:          return 0;
    }
}

const AnimatedTextureRef& getFaceTextureRef(const StateTextureIndices& textures, const int face) {
    switch (face) {
        case FACE_TOP:    return textures.worldTop;
        case FACE_BOTTOM: return textures.worldBottom;
        case FACE_FRONT:  return textures.worldFront;
        case FACE_BACK:   return textures.worldBack;
        case FACE_LEFT:   return textures.worldLeft;
        case FACE_RIGHT:  return textures.worldRight;
        default:          return textures.worldTop;
    }
}

uint8_t getFaceUvQuarterTurns(const BlockID blockId, const int face) {
    if (PropIndices::AXIS == PropIndices::INVALID) {
        return 0;
    }

    const uint16_t axisValue = BlockStateRegistry::getPropertyIndex(blockId, PropIndices::AXIS);
    if (axisValue == PropIndices::INVALID) {
        return 0;
    }

    if (axisValue == PropIndices::AXIS_X) {
        switch (face) {
            case FACE_TOP:
            case FACE_BOTTOM:
            case FACE_FRONT:
            case FACE_BACK:
                return 1;
            default:
                return 0;
        }
    }

    if (axisValue == PropIndices::AXIS_Z) {
        switch (face) {
            case FACE_LEFT:
            case FACE_RIGHT:
                return 1;
            default:
                return 0;
        }
    }

    return 0;
}

FaceRenderData buildFaceRenderData(const SubChunkMeshingSnapshot& snapshot,
                                   const BlockID blockId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z,
                                   const int face) {
    FaceRenderData renderData;
    static_cast<void>(def);
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(blockId);
    const AnimatedTextureRef& faceTexture = getFaceTextureRef(textures, face);
    renderData.tileIndex = std::max(0, faceTexture.firstLayer);
    renderData.layer = static_cast<float>(faceTexture.firstLayer);
    renderData.animationFrameCount = static_cast<float>(std::max<uint16_t>(1, faceTexture.frameCount));
    renderData.animationFps = faceTexture.isAnimated ? faceTexture.fps : 0.0f;
    renderData.animated = faceTexture.isAnimated ? 1.0f : 0.0f;
    renderData.vertices = computeFaceVertexData(snapshot, x, y, z, face);
    renderData.uvQuarterTurns = getFaceUvQuarterTurns(blockId, face);

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

uint64_t computeMergeKeyHash(const FaceMergeKey& key) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<uint64_t>(key.blockId));
    mix(static_cast<uint64_t>(key.tileIndex));
    mix(static_cast<uint64_t>(key.flipDiagonal));
    mix(static_cast<uint64_t>(key.uvQuarterTurns));
    for (size_t i = 0; i < 4; ++i) {
        mix(static_cast<uint64_t>(key.ao[i]));
    }
    for (size_t i = 0; i < 4; ++i) {
        mix(static_cast<uint64_t>(key.sun[i]));
    }
    for (size_t i = 0; i < 4; ++i) {
        mix(static_cast<uint64_t>(key.block[i]));
    }
    return h;
}

FaceMergeKey buildFaceMergeKey(const BlockID blockId, const FaceRenderData& renderData) {
    FaceMergeKey key;
    key.blockId = blockId;
    key.tileIndex = renderData.tileIndex;
    key.flipDiagonal = renderData.flipDiagonal;
    key.uvQuarterTurns = renderData.uvQuarterTurns;
    for (size_t i = 0; i < renderData.vertices.size(); ++i) {
        key.ao[i] = renderData.vertices[i].ao;
        key.sun[i] = quantizeNormalized(renderData.vertices[i].sunNormalized);
        key.block[i] = quantizeNormalized(renderData.vertices[i].blockNormalized);
    }
    key.hash = computeMergeKeyHash(key);
    return key;
}

bool sameMergeKey(const FaceMergeKey& lhs, const FaceMergeKey& rhs) {
    return lhs.hash == rhs.hash &&
           lhs.blockId == rhs.blockId &&
           lhs.tileIndex == rhs.tileIndex &&
           lhs.flipDiagonal == rhs.flipDiagonal &&
           lhs.uvQuarterTurns == rhs.uvQuarterTurns &&
           lhs.ao == rhs.ao &&
           lhs.sun == rhs.sun &&
           lhs.block == rhs.block;
}

bool shouldRenderFaceImpl(const SubChunkMeshingSnapshot& snapshot,
                          const int nx,
                          const int ny,
                          const int nz,
                          const BlockID currentId,
                          const BlockDef& currentDef) {
    const BlockID neighborId = getResolvedBlockSC(snapshot, nx, ny, nz);
    if (currentDef.renderShape == BlockRenderShape::Cube &&
        currentDef.isTransparent &&
        neighborId == currentId) {
        return false;
    }

    if (neighborId == 0) {
        return true;
    }

    const BlockDef& neighborDef = BlockRegistry::getFast(neighborId);

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

// ======================== Snapshot capture helpers ========================

BlockID sampleMissingNeighborBlock(const World* world, const int wx, const int y, const int wz) {
    if (world == nullptr) {
        return 0;
    }
    return world->sampleGeneratedBlock(wx, y, wz);
}

uint8_t sampleMissingNeighborLight(const World* world, const int wx, const int y, const int wz) {
    if (world == nullptr || y < 0 || y >= Chunk::SIZE_Y) {
        return 0;
    }

    const glm::ivec2 chunkCoords = world->getChunkCoords(wx, wz);
    const auto it = world->getActiveChunks().find(World::chunkKey(chunkCoords.x, chunkCoords.y));
    if (it == world->getActiveChunks().end() || !it->second) {
        return 0;
    }

    const int localX = wx - chunkCoords.x * Chunk::SIZE_X;
    const int localZ = wz - chunkCoords.y * Chunk::SIZE_Z;
    return it->second->getPackedLight(localX, y, localZ);
}

BlockID sampleHaloBlock(const Chunk& chunk,
                        const int localX,
                        const int worldY,
                        const int localZ,
                        const Chunk* neighborPosX,
                        const Chunk* neighborNegX,
                        const Chunk* neighborPosZ,
                        const Chunk* neighborNegZ,
                        const World* world) {
    const glm::ivec3 offset = chunk.getWorldOffset();
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getBlock(localX, worldY, localZ);
    }

    if (world != nullptr) {
        return world->sampleGeneratedBlock(offset.x + localX, worldY, offset.z + localZ);
    }

    if (!zInRange) {
        return 0;
    }
    if (localX < 0) {
        return neighborNegX ? neighborNegX->getBlock(Chunk::SIZE_X - 1, worldY, localZ) : 0;
    }
    if (localX >= Chunk::SIZE_X) {
        return neighborPosX ? neighborPosX->getBlock(0, worldY, localZ) : 0;
    }
    if (localZ < 0) {
        return neighborNegZ ? neighborNegZ->getBlock(localX, worldY, Chunk::SIZE_Z - 1) : 0;
    }
    return neighborPosZ ? neighborPosZ->getBlock(localX, worldY, 0) : 0;
}

BlockID sampleHaloFluid(const Chunk& chunk,
                        const int localX,
                        const int worldY,
                        const int localZ,
                        const Chunk* neighborPosX,
                        const Chunk* neighborNegX,
                        const Chunk* neighborPosZ,
                        const Chunk* neighborNegZ,
                        const World* world) {
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getFluidState(localX, worldY, localZ);
    }

    if (world != nullptr) {
        const glm::ivec3 offset = chunk.getWorldOffset();
        return world->getFluidState(offset.x + localX, worldY, offset.z + localZ);
    }

    if (!zInRange) {
        return 0;
    }
    if (localX < 0) {
        return neighborNegX ? neighborNegX->getFluidState(Chunk::SIZE_X - 1, worldY, localZ) : 0;
    }
    if (localX >= Chunk::SIZE_X) {
        return neighborPosX ? neighborPosX->getFluidState(0, worldY, localZ) : 0;
    }
    if (localZ < 0) {
        return neighborNegZ ? neighborNegZ->getFluidState(localX, worldY, Chunk::SIZE_Z - 1) : 0;
    }
    return neighborPosZ ? neighborPosZ->getFluidState(localX, worldY, 0) : 0;
}

uint8_t sampleHaloLight(const Chunk& chunk,
                        const int localX,
                        const int worldY,
                        const int localZ,
                        const Chunk* neighborPosX,
                        const Chunk* neighborNegX,
                        const Chunk* neighborPosZ,
                        const Chunk* neighborNegZ,
                        const World* world) {
    const glm::ivec3 offset = chunk.getWorldOffset();
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getPackedLight(localX, worldY, localZ);
    }

    if (world != nullptr) {
        return sampleMissingNeighborLight(world, offset.x + localX, worldY, offset.z + localZ);
    }

    if (!zInRange) {
        return 0;
    }
    if (localX < 0) {
        return neighborNegX ? neighborNegX->getPackedLight(Chunk::SIZE_X - 1, worldY, localZ) : 0;
    }
    if (localX >= Chunk::SIZE_X) {
        return neighborPosX ? neighborPosX->getPackedLight(0, worldY, localZ) : 0;
    }
    if (localZ < 0) {
        return neighborNegZ ? neighborNegZ->getPackedLight(localX, worldY, Chunk::SIZE_Z - 1) : 0;
    }
    return neighborPosZ ? neighborPosZ->getPackedLight(localX, worldY, 0) : 0;
}

void captureSubChunkHalo(const Chunk& chunk,
                         const int scy,
                         SubChunkMeshingSnapshot& snapshot,
                         const Chunk* neighborPosX,
                         const Chunk* neighborNegX,
                         const Chunk* neighborPosZ,
                         const Chunk* neighborNegZ,
                         const World* world) {
    const int yBase = scy * SubChunk::SIZE;
    for (int ly = -1; ly <= SubChunk::SIZE; ++ly) {
        const int worldY = yBase + ly;
        for (int lz = -1; lz <= SubChunk::SIZE; ++lz) {
            for (int lx = -1; lx <= SubChunk::SIZE; ++lx) {
                const std::size_t haloIdx = haloToIndex(lx, ly, lz);
                snapshot.haloBlocks[haloIdx] = sampleHaloBlock(chunk, lx, worldY, lz,
                                                               neighborPosX, neighborNegX,
                                                               neighborPosZ, neighborNegZ,
                                                               world);
                snapshot.haloFluidBlocks[haloIdx] = sampleHaloFluid(chunk, lx, worldY, lz,
                                                                    neighborPosX, neighborNegX,
                                                                    neighborPosZ, neighborNegZ,
                                                                    world);
                snapshot.haloLightMap[haloIdx] = sampleHaloLight(chunk, lx, worldY, lz,
                                                                 neighborPosX, neighborNegX,
                                                                 neighborPosZ, neighborNegZ,
                                                                 world);
            }
        }
    }
}

// Capture horizontal borders for a specific sub-chunk (y in [yBase, yBase+16))
void captureSubChunkBorders(const Chunk& chunk,
                            int scy,
                            SubChunkMeshingSnapshot& snapshot,
                            const Chunk* neighborPosX,
                            const Chunk* neighborNegX,
                            const Chunk* neighborPosZ,
                            const Chunk* neighborNegZ,
                            const World* world) {
    const glm::ivec3 offset = chunk.getWorldOffset();
    const int yBase = scy * SubChunk::SIZE;

    // +Y border: query through Chunk so missing sky-only sub-chunks still expose implicit sunlight.
    if (scy + 1 < Chunk::NUM_SUB_CHUNKS) {
        const int aboveY = yBase + SubChunk::SIZE;
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
                const auto idx = toBorderXZIndex(lx, lz);
                snapshot.posYBorder[idx] = chunk.getBlock(lx, aboveY, lz);
                snapshot.posYLightBorder[idx] = chunk.getPackedLight(lx, aboveY, lz);
            }
        }
    }
    // else: isTopSection, border stays 0

    // -Y border: query through Chunk for consistency with implicit light defaults.
    if (scy - 1 >= 0) {
        const int belowY = yBase - 1;
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
                const auto idx = toBorderXZIndex(lx, lz);
                snapshot.negYBorder[idx] = chunk.getBlock(lx, belowY, lz);
                snapshot.negYLightBorder[idx] = chunk.getPackedLight(lx, belowY, lz);
            }
        }
    }

    // Horizontal borders (+X, -X, +Z, -Z) — same as before but only for yBase..yBase+15
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int columnY = yBase + ly;
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            const auto idx = toBorderXZIndex(ly, lz);
            snapshot.posXBorder[idx] = neighborPosX
                ? neighborPosX->getBlock(0, columnY, lz)
                : sampleMissingNeighborBlock(world, offset.x + Chunk::SIZE_X, columnY, offset.z + lz);
            snapshot.negXBorder[idx] = neighborNegX
                ? neighborNegX->getBlock(Chunk::SIZE_X - 1, columnY, lz)
                : sampleMissingNeighborBlock(world, offset.x - 1, columnY, offset.z + lz);
            snapshot.posXLightBorder[idx] = neighborPosX
                ? neighborPosX->getPackedLight(0, columnY, lz) : 0;
            snapshot.negXLightBorder[idx] = neighborNegX
                ? neighborNegX->getPackedLight(Chunk::SIZE_X - 1, columnY, lz) : 0;
        }
        for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
            const auto idx = toBorderXZIndex(ly, lx);
            snapshot.posZBorder[idx] = neighborPosZ
                ? neighborPosZ->getBlock(lx, columnY, 0)
                : sampleMissingNeighborBlock(world, offset.x + lx, columnY, offset.z + Chunk::SIZE_Z);
            snapshot.negZBorder[idx] = neighborNegZ
                ? neighborNegZ->getBlock(lx, columnY, Chunk::SIZE_Z - 1)
                : sampleMissingNeighborBlock(world, offset.x + lx, columnY, offset.z - 1);
            snapshot.posZLightBorder[idx] = neighborPosZ
                ? neighborPosZ->getPackedLight(lx, columnY, 0) : 0;
            snapshot.negZLightBorder[idx] = neighborNegZ
                ? neighborNegZ->getPackedLight(lx, columnY, Chunk::SIZE_Z - 1) : 0;
        }
    }
}

// ======================== Face vertex emission ========================

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
            renderData.layer,
            renderData.animationFrameCount,
            renderData.animationFps,
            renderData.animated
        });
    }
}

// Greedy meshing corners — coordinates are in sub-chunk local space
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

void expandGreedyFaceCornersInPlane(std::array<glm::vec3, 4>& corners, const int face) {
    glm::vec3 center(0.0f);
    for (const glm::vec3& corner : corners) {
        center += corner;
    }
    center *= 0.25f;

    const auto expandAxis = [](float& value, const float centerValue) {
        value += (value >= centerValue) ? kGreedyFaceOverlapEpsilon : -kGreedyFaceOverlapEpsilon;
    };

    switch (face) {
        case FACE_TOP:
        case FACE_BOTTOM:
            for (glm::vec3& corner : corners) {
                expandAxis(corner.x, center.x);
                expandAxis(corner.z, center.z);
            }
            break;
        case FACE_FRONT:
        case FACE_BACK:
            for (glm::vec3& corner : corners) {
                expandAxis(corner.x, center.x);
                expandAxis(corner.y, center.y);
            }
            break;
        case FACE_LEFT:
        case FACE_RIGHT:
            for (glm::vec3& corner : corners) {
                expandAxis(corner.y, center.y);
                expandAxis(corner.z, center.z);
            }
            break;
        default:
            break;
    }
}

std::array<glm::vec2, 4> buildFaceUv(const float width,
                                     const float height,
                                     const uint8_t quarterTurns) {
    switch (quarterTurns % 4) {
        case 1:
            return {{{0.0f, 0.0f},
                     {0.0f, width},
                     {height, width},
                     {height, 0.0f}}};
        case 2:
            return {{{width, height},
                     {0.0f, height},
                     {0.0f, 0.0f},
                     {width, 0.0f}}};
        case 3:
            return {{{height, width},
                     {height, 0.0f},
                     {0.0f, 0.0f},
                     {0.0f, width}}};
        case 0:
        default:
            return {{{0.0f, 0.0f},
                     {width, 0.0f},
                     {width, height},
                     {0.0f, height}}};
    }
}

void emitGreedyFace(std::vector<BlockVertex>& vertices,
                    ChunkMeshData& meshData,
                    const FaceCell& cell,
                    const int face,
                    const int width,
                    const int height) {
    std::array<glm::vec3, 4> corners = buildGreedyFaceCorners(face, cell.x, cell.y, cell.z, width, height);
    // Greedy quads can form T-junctions against neighbouring smaller quads.
    // Expanding them by a tiny amount in-plane hides raster cracks without
    // changing the face depth.
    expandGreedyFaceCornersInPlane(corners, face);
    const std::array<glm::vec2, 4> faceUV = buildFaceUv(
        static_cast<float>(width),
        static_cast<float>(height),
        cell.renderData.uvQuarterTurns);

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

void emitUnitFace(std::vector<BlockVertex>& vertices,
                  const glm::vec3& pos,
                  const int face,
                  const FaceRenderData& renderData) {
    const std::array<glm::vec2, 4> faceUV = buildFaceUv(1.0f, 1.0f, renderData.uvQuarterTurns);
    std::array<glm::vec3, 4> corners{};
    for (size_t i = 0; i < corners.size(); ++i) {
        corners[i] = pos + kFaceCorners[static_cast<size_t>(face)][i];
    }
    appendFaceVertices(vertices, corners, faceUV, face, renderData);
}

void emitCustomFace(std::vector<BlockVertex>& vertices,
                    const std::array<glm::vec3, 4>& corners,
                    const int face,
                    const FaceRenderData& renderData) {
    const std::array<glm::vec2, 4> faceUV = buildFaceUv(1.0f, 1.0f, renderData.uvQuarterTurns);
    appendFaceVertices(vertices, corners, faceUV, face, renderData);
}

bool shouldRenderWaterFace(const SubChunkMeshingSnapshot& snapshot,
                           const int nx,
                           const int ny,
                           const int nz,
                           const BlockID currentId) {
    const BlockID neighborId = getResolvedBlockSC(snapshot, nx, ny, nz);
    const DecodedFluid currentFluid = FluidState::decode(currentId);
    if (currentFluid.kind != FluidKind::None &&
        FluidState::decode(neighborId).kind == currentFluid.kind) {
        return false;
    }
    // Check fluid layer for waterlogged neighbors
    const BlockID neighborFluidId = getResolvedFluidSC(snapshot, nx, ny, nz);
    if (currentFluid.kind != FluidKind::None &&
        FluidState::decode(neighborFluidId).kind == currentFluid.kind) {
        return false;
    }
    if (neighborId == BlockIds::AIR && neighborFluidId == 0) {
        return true;
    }

    const BlockDef& neighborDef = BlockRegistry::getFast(neighborId);
    if (!neighborDef.isSolid) {
        return true;
    }

    return neighborDef.isTransparent;
}

float sampleWaterColumnSurfaceHeight(const SubChunkMeshingSnapshot& snapshot,
                                     const int x,
                                     const int y,
                                     const int z) {
    const BlockID aboveId = getResolvedBlockSC(snapshot, x, y + 1, z);
    const BlockID id = getResolvedBlockSC(snapshot, x, y, z);
    const DecodedFluid fluid = FluidState::decode(id);
    if (fluid.kind == FluidKind::None) {
        return 0.0f;
    }
    if (FluidState::decode(aboveId).kind == fluid.kind) {
        return 1.0f;
    }
    return FluidState::surfaceHeight(id);
}

bool isOpenWaterSurfaceSample(const BlockID id) {
    if (id == BlockIds::AIR) {
        return true;
    }

    if (FluidState::isWater(id)) {
        return false;
    }

    return !BlockRegistry::getFast(id).isSolid;
}

float computeWaterCornerHeight(const SubChunkMeshingSnapshot& snapshot,
                               const BlockID currentId,
                               const int x0,
                               const int y,
                               const int z0,
                               const int x1,
                               const int z1,
                               const int x2,
                               const int z2,
                               const int x3,
                               const int z3) {
    const std::array<glm::ivec2, 4> samples = {{
        {x0, z0},
        {x1, z1},
        {x2, z2},
        {x3, z3}
    }};

    float liquidPercentSum = 0.0f;
    int weightSum = 0;
    for (const glm::ivec2& sample : samples) {
        const BlockID aboveId = getResolvedBlockSC(snapshot, sample.x, y + 1, sample.y);
        if (FluidState::isWater(aboveId)) {
            return 1.0f;
        }

        const BlockID sampleId = getResolvedBlockSC(snapshot, sample.x, y, sample.y);
        if (FluidState::isWater(sampleId)) {
            const float liquidPercent = static_cast<float>(FluidState::level(sampleId) + 1) / 9.0f;
            const int weight = (FluidState::level(sampleId) == 0) ? 10 : 1;
            liquidPercentSum += liquidPercent * static_cast<float>(weight);
            weightSum += weight;

            liquidPercentSum += liquidPercent;
            ++weightSum;
            continue;
        }

        if (isOpenWaterSurfaceSample(sampleId)) {
            liquidPercentSum += 1.0f;
            ++weightSum;
        }
    }

    if (weightSum == 0) {
        return sampleWaterColumnSurfaceHeight(snapshot, x1, y, z1);
    }
    static_cast<void>(currentId);
    return 1.0f - liquidPercentSum / static_cast<float>(weightSum);
}

void expandBoundsForCorners(ChunkMeshData& meshData, const std::array<glm::vec3, 4>& corners) {
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

uint8_t computeWaterTopQuarterTurns(const glm::vec3& flow) {
    if (std::abs(flow.x) >= std::abs(flow.z)) {
        if (flow.x > 0.001f) {
            return 1;
        }
        if (flow.x < -0.001f) {
            return 3;
        }
        return 0;
    }

    if (flow.z > 0.001f) {
        return 2;
    }
    if (flow.z < -0.001f) {
        return 0;
    }
    return 0;
}

bool isFlowingWaterVector(const glm::vec3& flow) {
    return std::abs(flow.x) > 0.001f ||
           std::abs(flow.y) > 0.001f ||
           std::abs(flow.z) > 0.001f;
}

const AnimatedTextureRef* findNamedWaterTexture(const BlockDef& def, const char* alias) {
    const auto it = def.namedTextureAnimations.find(alias);
    if (it == def.namedTextureAnimations.end()) {
        return nullptr;
    }
    return &it->second.ref;
}

void applyWaterTextureRef(FaceRenderData& renderData, const AnimatedTextureRef& texture) {
    renderData.tileIndex = std::max(0, texture.firstLayer);
    renderData.layer = static_cast<float>(texture.firstLayer);
    renderData.animationFrameCount = static_cast<float>(std::max<uint16_t>(1, texture.frameCount));
    renderData.animationFps = texture.isAnimated ? texture.fps : 0.0f;
    renderData.animated = texture.isAnimated ? 1.0f : 0.0f;
}

void addWaterFacesImpl(ChunkMeshData& meshData,
                       const SubChunkMeshingSnapshot& snapshot,
                       const BlockID blockId,
                       const BlockDef& def,
                       const int x,
                       const int y,
                       const int z) {
    const float frontLeft = computeWaterCornerHeight(snapshot, blockId, x - 1, y, z, x, z, x - 1, z + 1, x, z + 1);
    const float frontRight = computeWaterCornerHeight(snapshot, blockId, x, y, z, x + 1, z, x, z + 1, x + 1, z + 1);
    const float backRight = computeWaterCornerHeight(snapshot, blockId, x, y, z - 1, x + 1, z - 1, x, z, x + 1, z);
    const float backLeft = computeWaterCornerHeight(snapshot, blockId, x - 1, y, z - 1, x, z - 1, x - 1, z, x, z);

    const glm::vec3 flow = computeFluidFlowVector(snapshot, x, y, z, FluidKind::Water);
    const bool flowing = isFlowingWaterVector(flow);
    const uint8_t flowQuarterTurns = computeWaterTopQuarterTurns(flow);
    const AnimatedTextureRef* waterTexture = findNamedWaterTexture(def, flowing ? "flow" : "still");

    const glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    const auto emitWaterFace = [&](const int face, const std::array<glm::vec3, 4>& corners) {
        FaceRenderData renderData = buildFaceRenderData(snapshot, blockId, def, x, y, z, face);
        if (waterTexture != nullptr) {
            applyWaterTextureRef(renderData, *waterTexture);
        }
        if (face == FACE_TOP) {
            renderData.uvQuarterTurns = flowing ? flowQuarterTurns : 0;
        } else if (flow.y < -0.001f && (face == FACE_BACK || face == FACE_RIGHT)) {
            renderData.uvQuarterTurns = 2;
        }
        emitCustomFace(meshData.transparentVertices, corners, face, renderData);
        expandBoundsForCorners(meshData, corners);
        ++meshData.transparentFaceCountBeforeGreedy;
        ++meshData.transparentFaceCountAfterGreedy;
    };

    if (shouldRenderWaterFace(snapshot, x, y + 1, z, blockId)) {
        emitWaterFace(FACE_TOP, {{
            pos + glm::vec3(0.0f, frontLeft, 1.0f),
            pos + glm::vec3(1.0f, frontRight, 1.0f),
            pos + glm::vec3(1.0f, backRight, 0.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y - 1, z, blockId)) {
        emitWaterFace(FACE_BOTTOM, {{
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(1.0f, 0.0f, 0.0f),
            pos + glm::vec3(1.0f, 0.0f, 1.0f),
            pos + glm::vec3(0.0f, 0.0f, 1.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y, z + 1, blockId) &&
        (frontLeft > 0.0f || frontRight > 0.0f)) {
        emitWaterFace(FACE_FRONT, {{
            pos + glm::vec3(0.0f, 0.0f, 1.0f),
            pos + glm::vec3(1.0f, 0.0f, 1.0f),
            pos + glm::vec3(1.0f, frontRight, 1.0f),
            pos + glm::vec3(0.0f, frontLeft, 1.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y, z - 1, blockId) &&
        (backLeft > 0.0f || backRight > 0.0f)) {
        emitWaterFace(FACE_BACK, {{
            pos + glm::vec3(1.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f),
            pos + glm::vec3(1.0f, backRight, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x - 1, y, z, blockId) &&
        (frontLeft > 0.0f || backLeft > 0.0f)) {
        emitWaterFace(FACE_LEFT, {{
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, 0.0f, 1.0f),
            pos + glm::vec3(0.0f, frontLeft, 1.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x + 1, y, z, blockId) &&
        (frontRight > 0.0f || backRight > 0.0f)) {
        emitWaterFace(FACE_RIGHT, {{
            pos + glm::vec3(1.0f, 0.0f, 1.0f),
            pos + glm::vec3(1.0f, 0.0f, 0.0f),
            pos + glm::vec3(1.0f, backRight, 0.0f),
            pos + glm::vec3(1.0f, frontRight, 1.0f)
        }});
    }
}

bool isOpaqueCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && !def.isTransparent;
}

bool isTransparentCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && def.isTransparent;
}

bool populateOpaqueFaceCell(const SubChunkMeshingSnapshot& snapshot,
                            const int face,
                            const int x,
                            const int y,
                            const int z,
                            FaceCell& outCell) {
    const BlockID blockId = snapshot.blocks[scToIndex(x, y, z)];
    if (blockId == 0) {
        return false;
    }

    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!isOpaqueCubeCandidate(def)) {
        return false;
    }

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, blockId, def)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, blockId, def, x, y, z, face);
    outCell.key = buildFaceMergeKey(blockId, outCell.renderData);
    return true;
}

bool populateTransparentFaceCell(const SubChunkMeshingSnapshot& snapshot,
                                 const int face,
                                 const int x,
                                 const int y,
                                 const int z,
                                 FaceCell& outCell) {
    const BlockID blockId = snapshot.blocks[scToIndex(x, y, z)];
    if (blockId == 0) {
        return false;
    }

    const BlockDef& def = BlockRegistry::getFast(blockId);
    if (!isTransparentCubeCandidate(def)) {
        return false;
    }

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, blockId, def)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, blockId, def, x, y, z, face);
    outCell.key = buildFaceMergeKey(blockId, outCell.renderData);
    return true;
}

template <typename PopulateCellFn>
void buildCubeGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                          ChunkMeshData& meshData,
                          std::vector<BlockVertex>& targetVertices,
                          uint32_t& faceCountBeforeGreedy,
                          uint32_t& faceCountAfterGreedy,
                          PopulateCellFn&& populateCell) {
    // Max plane size for sub-chunk: 16 * 16 = 256 FaceCells
    constexpr size_t kMaxPlaneSize = static_cast<size_t>(SubChunk::SIZE) * SubChunk::SIZE;
    std::vector<FaceCell> plane(kMaxPlaneSize);
    std::vector<bool> consumed(kMaxPlaneSize, false);

    auto buildPlane = [&](const int face, const int width, const int height, const int slices, auto&& mapper) {
        const size_t planeSize = static_cast<size_t>(width) * static_cast<size_t>(height);

        for (int slice = 0; slice < slices; ++slice) {
            for (size_t i = 0; i < planeSize; ++i) {
                plane[i].valid = false;
                consumed[i] = false;
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

                    const uint64_t startHash = plane[startIndex].key.hash;

                    int runWidth = 1;
                    while (u + runWidth < width) {
                        const size_t nextIndex = static_cast<size_t>(u + runWidth) + static_cast<size_t>(v) * static_cast<size_t>(width);
                        if (consumed[nextIndex] || !plane[nextIndex].valid ||
                            plane[nextIndex].key.hash != startHash ||
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
                                plane[candidateIndex].key.hash != startHash ||
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

    constexpr int S = SubChunk::SIZE;

    buildPlane(FACE_TOP, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u; y = slice; z = v;
               });
    buildPlane(FACE_BOTTOM, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u; y = slice; z = v;
               });
    buildPlane(FACE_FRONT, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u; y = v; z = slice;
               });
    buildPlane(FACE_BACK, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = u; y = v; z = slice;
               });
    buildPlane(FACE_LEFT, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = slice; y = v; z = u;
               });
    buildPlane(FACE_RIGHT, S, S, S,
               [](const int slice, const int u, const int v, int& x, int& y, int& z) {
                   x = slice; y = v; z = u;
               });
}

void buildOpaqueGreedyFaces(const SubChunkMeshingSnapshot& snapshot, ChunkMeshData& meshData) {
    buildCubeGreedyFaces(snapshot,
                         meshData,
                         meshData.opaqueVertices,
                         meshData.opaqueFaceCountBeforeGreedy,
                         meshData.opaqueFaceCountAfterGreedy,
                         populateOpaqueFaceCell);
}

void buildTransparentGreedyFaces(const SubChunkMeshingSnapshot& snapshot, ChunkMeshData& meshData) {
    buildCubeGreedyFaces(snapshot,
                         meshData,
                         meshData.transparentVertices,
                         meshData.transparentFaceCountBeforeGreedy,
                         meshData.transparentFaceCountAfterGreedy,
                         populateTransparentFaceCell);
}

void addCrossedQuadsImpl(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const BlockID blockId,
                          const BlockDef& def,
                          const int x,
                          const int y,
                          const int z,
                          const SubChunkMeshingSnapshot& snapshot) {
    static_cast<void>(def);
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(blockId);
    const float layer = static_cast<float>(textures.worldTop.firstLayer);

    uint8_t sunLevel = getResolvedSunlightSC(snapshot, x, y, z);
    uint8_t blockLevel = getResolvedBlockLightSC(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        const int nx = x + kFaceNormals[static_cast<size_t>(d)].x;
        const int ny = y + kFaceNormals[static_cast<size_t>(d)].y;
        const int nz = z + kFaceNormals[static_cast<size_t>(d)].z;
        sunLevel = std::max(sunLevel, getResolvedSunlightSC(snapshot, nx, ny, nz));
        blockLevel = std::max(blockLevel, getResolvedBlockLightSC(snapshot, nx, ny, nz));
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

// ── Torch mesh builder ──────────────────────────────────────────────────────
// Torch texture: 16×16 pixels, valid region = center 2 columns × bottom 10 rows.
// UV mapping:  U = [7/16, 9/16],  V = [6/16, 16/16]  (bottom-aligned)
//
// Geometry:
//   floor  — two thin crossed quads standing upright, centered in the block
//   north  — single quad in Z-min plane, offset toward -Z wall
//   south  — single quad in Z-max plane, offset toward +Z wall
//   east   — single quad in X-max plane, offset toward +X wall
//   west   — single quad in X-min plane, offset toward -X wall

// Torch opaque texels occupy inclusive pixel coordinates:
// left=7, right=8, top=6, bottom=15 on a 16x16 tile.
// We sample at texel centers to avoid pulling in the transparent neighbors.
constexpr float kTorchPixelLeft = 7.0f;
constexpr float kTorchPixelRight = 8.0f;
constexpr float kTorchPixelTop = 6.0f;
constexpr float kTorchPixelBottom = 15.0f;
constexpr float kTorchU0 = (kTorchPixelLeft + 0.5f) / 16.0f;
constexpr float kTorchU1 = (kTorchPixelRight + 0.5f) / 16.0f;
constexpr float kTorchSideV0 = (kTorchPixelTop + 0.5f) / 16.0f;
constexpr float kTorchSideV1 = (kTorchPixelBottom + 0.5f) / 16.0f;
constexpr float kTorchTopV0 = (kTorchPixelTop + 0.5f) / 16.0f;
constexpr float kTorchTopV1 = (kTorchPixelTop + 1.5f) / 16.0f;

// Torch half-width in world units (1 pixel = 1/16 block)
constexpr float kTorchHW = 1.0f / 16.0f;
constexpr float kTorchHeight = 10.0f / 16.0f;
constexpr float kFloorTorchBottom = 0.0f;
constexpr float kFloorTorchTop = kFloorTorchBottom + kTorchHeight;
constexpr float kWallTorchBottom = 3.0f / 16.0f;
constexpr float kWallTorchTop = kWallTorchBottom + kTorchHeight;
// Wall offset: how far the torch center sits from the wall
constexpr float kTorchWallOffset = 1.0f / 16.0f;

void addTorchCuboidImpl(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        const BlockID blockId,
                        const int x,
                        const int y,
                        const int z,
                        const SubChunkMeshingSnapshot& snapshot) {
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(blockId);
    int tileIndex = textures.texTop;
    if (tileIndex < 0) tileIndex = 0;
    const float layer = static_cast<float>(tileIndex);

    // Lighting: take max of self + all 6 neighbors (same as cross)
    uint8_t sunLevel = getResolvedSunlightSC(snapshot, x, y, z);
    uint8_t blockLevel = getResolvedBlockLightSC(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        sunLevel = std::max(sunLevel, getResolvedSunlightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
        blockLevel = std::max(blockLevel, getResolvedBlockLightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
    }
    const float sunNorm = lightToNormalized(sunLevel);
    const float blockNorm = lightToNormalized(blockLevel);

    // Determine facing from block state
    uint16_t facingValue = PropIndices::FACING_FLOOR;
    if (PropIndices::FACING != PropIndices::INVALID) {
        facingValue = BlockStateRegistry::getPropertyIndex(blockId, PropIndices::FACING);
    }

    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};

    const std::array<glm::vec2, 4> sideUV = {{
        {kTorchU0, kTorchSideV0},
        {kTorchU1, kTorchSideV0},
        {kTorchU1, kTorchSideV1},
        {kTorchU0, kTorchSideV1}
    }};
    const std::array<glm::vec2, 4> topUV = {{
        {kTorchU0, kTorchTopV0},
        {kTorchU1, kTorchTopV0},
        {kTorchU1, kTorchTopV1},
        {kTorchU0, kTorchTopV1}
    }};

    const auto emitFace = [&](const std::array<glm::vec3, 4>& corners,
                              const int face,
                              const std::array<glm::vec2, 4>& uv) {
        for (const int idx : indices) {
            vertices.push_back({
                pos.x + corners[static_cast<size_t>(idx)].x,
                pos.y + corners[static_cast<size_t>(idx)].y,
                pos.z + corners[static_cast<size_t>(idx)].z,
                uv[static_cast<size_t>(idx)].x,
                uv[static_cast<size_t>(idx)].y,
                static_cast<float>(face),
                sunNorm,
                blockNorm,
                3.0f,
                layer,
                1.0f,
                0.0f,
                0.0f
            });
        }
    };

    float x0 = 0.5f - kTorchHW;
    float x1 = 0.5f + kTorchHW;
    float z0 = 0.5f - kTorchHW;
    float z1 = 0.5f + kTorchHW;
    float y0 = kFloorTorchBottom;
    float y1 = kFloorTorchTop;
    const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
        emitFace(corners, FACE_FRONT, sideUV);
    };

    if (facingValue == PropIndices::FACING_FLOOR) {
        // ── Floor torch: two thin crossed quads, centered ──
        // Quad A: diagonal along (X+Z)
        emitQuad({{
            {0.5f - kTorchHW, kFloorTorchBottom, 0.5f - kTorchHW},
            {0.5f + kTorchHW, kFloorTorchBottom, 0.5f + kTorchHW},
            {0.5f + kTorchHW, kFloorTorchTop, 0.5f + kTorchHW},
            {0.5f - kTorchHW, kFloorTorchTop, 0.5f - kTorchHW}
        }});
        // Quad B: diagonal along (X-Z)
        emitQuad({{
            {0.5f + kTorchHW, kFloorTorchBottom, 0.5f - kTorchHW},
            {0.5f - kTorchHW, kFloorTorchBottom, 0.5f + kTorchHW},
            {0.5f - kTorchHW, kFloorTorchTop, 0.5f + kTorchHW},
            {0.5f + kTorchHW, kFloorTorchTop, 0.5f - kTorchHW}
        }});
    } else if (facingValue == PropIndices::FACING_NORTH) {
        // ── Wall torch on -Z face ──
        const float cz = kTorchWallOffset;
        emitQuad({{
            {0.5f - kTorchHW, kWallTorchBottom, cz - kTorchHW},
            {0.5f + kTorchHW, kWallTorchBottom, cz + kTorchHW},
            {0.5f + kTorchHW, kWallTorchTop, cz + kTorchHW},
            {0.5f - kTorchHW, kWallTorchTop, cz - kTorchHW}
        }});
    } else if (facingValue == PropIndices::FACING_SOUTH) {
        // ── Wall torch on +Z face ──
        const float cz = 1.0f - kTorchWallOffset;
        emitQuad({{
            {0.5f + kTorchHW, kWallTorchBottom, cz + kTorchHW},
            {0.5f - kTorchHW, kWallTorchBottom, cz - kTorchHW},
            {0.5f - kTorchHW, kWallTorchTop, cz - kTorchHW},
            {0.5f + kTorchHW, kWallTorchTop, cz + kTorchHW}
        }});
    } else if (facingValue == PropIndices::FACING_WEST) {
        // ── Wall torch on -X face ──
        const float cx = kTorchWallOffset;
        emitQuad({{
            {cx + kTorchHW, kWallTorchBottom, 0.5f - kTorchHW},
            {cx - kTorchHW, kWallTorchBottom, 0.5f + kTorchHW},
            {cx - kTorchHW, kWallTorchTop, 0.5f + kTorchHW},
            {cx + kTorchHW, kWallTorchTop, 0.5f - kTorchHW}
        }});
    } else if (facingValue == PropIndices::FACING_EAST) {
        // ── Wall torch on +X face ──
        const float cx = 1.0f - kTorchWallOffset;
        emitQuad({{
            {cx - kTorchHW, kWallTorchBottom, 0.5f + kTorchHW},
            {cx + kTorchHW, kWallTorchBottom, 0.5f - kTorchHW},
            {cx + kTorchHW, kWallTorchTop, 0.5f - kTorchHW},
            {cx - kTorchHW, kWallTorchTop, 0.5f + kTorchHW}
        }});
    }
}

void addTorchPrismImpl(std::vector<BlockVertex>& vertices,
                       const glm::vec3& pos,
                       const BlockID blockId,
                       const int x,
                       const int y,
                       const int z,
                       const SubChunkMeshingSnapshot& snapshot) {
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(blockId);
    const float layer = static_cast<float>(textures.worldTop.firstLayer);

    uint8_t sunLevel = getResolvedSunlightSC(snapshot, x, y, z);
    uint8_t blockLevel = getResolvedBlockLightSC(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        sunLevel = std::max(sunLevel, getResolvedSunlightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
        blockLevel = std::max(blockLevel, getResolvedBlockLightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
    }
    const float sunNorm = lightToNormalized(sunLevel);
    const float blockNorm = lightToNormalized(blockLevel);

    uint16_t facingValue = PropIndices::FACING_FLOOR;
    if (PropIndices::FACING != PropIndices::INVALID) {
        facingValue = BlockStateRegistry::getPropertyIndex(blockId, PropIndices::FACING);
    }

    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};
    const std::array<glm::vec2, 4> sideUV = {{
        {kTorchU0, kTorchSideV0},
        {kTorchU1, kTorchSideV0},
        {kTorchU1, kTorchSideV1},
        {kTorchU0, kTorchSideV1}
    }};
    const std::array<glm::vec2, 4> topUV = {{
        {kTorchU0, kTorchTopV0},
        {kTorchU1, kTorchTopV0},
        {kTorchU1, kTorchTopV1},
        {kTorchU0, kTorchTopV1}
    }};

    const auto emitFace = [&](const std::array<glm::vec3, 4>& corners,
                              const int face,
                              const std::array<glm::vec2, 4>& uv) {
        for (const int idx : indices) {
            vertices.push_back({
                pos.x + corners[static_cast<size_t>(idx)].x,
                pos.y + corners[static_cast<size_t>(idx)].y,
                pos.z + corners[static_cast<size_t>(idx)].z,
                uv[static_cast<size_t>(idx)].x,
                uv[static_cast<size_t>(idx)].y,
                static_cast<float>(face),
                sunNorm,
                blockNorm,
                3.0f,
                layer,
                1.0f,
                0.0f,
                0.0f
            });
        }
    };

    float x0 = 0.5f - kTorchHW;
    float x1 = 0.5f + kTorchHW;
    float z0 = 0.5f - kTorchHW;
    float z1 = 0.5f + kTorchHW;
    float y0 = kFloorTorchBottom;
    float y1 = kFloorTorchTop;

    if (facingValue == PropIndices::FACING_NORTH) {
        z0 = 0.0f;
        z1 = 2.0f * kTorchHW;
        y0 = kWallTorchBottom;
        y1 = kWallTorchTop;
    } else if (facingValue == PropIndices::FACING_SOUTH) {
        z0 = 1.0f - 2.0f * kTorchHW;
        z1 = 1.0f;
        y0 = kWallTorchBottom;
        y1 = kWallTorchTop;
    } else if (facingValue == PropIndices::FACING_WEST) {
        x0 = 0.0f;
        x1 = 2.0f * kTorchHW;
        y0 = kWallTorchBottom;
        y1 = kWallTorchTop;
    } else if (facingValue == PropIndices::FACING_EAST) {
        x0 = 1.0f - 2.0f * kTorchHW;
        x1 = 1.0f;
        y0 = kWallTorchBottom;
        y1 = kWallTorchTop;
    }

    emitFace({{
        {x0, y1, z1},
        {x1, y1, z1},
        {x1, y1, z0},
        {x0, y1, z0}
    }}, FACE_TOP, topUV);
    emitFace({{
        {x0, y0, z0},
        {x1, y0, z0},
        {x1, y0, z1},
        {x0, y0, z1}
    }}, FACE_BOTTOM, topUV);
    emitFace({{
        {x0, y0, z1},
        {x1, y0, z1},
        {x1, y1, z1},
        {x0, y1, z1}
    }}, FACE_FRONT, sideUV);
    emitFace({{
        {x1, y0, z0},
        {x0, y0, z0},
        {x0, y1, z0},
        {x1, y1, z0}
    }}, FACE_BACK, sideUV);
    emitFace({{
        {x0, y0, z0},
        {x0, y0, z1},
        {x0, y1, z1},
        {x0, y1, z0}
    }}, FACE_LEFT, sideUV);
    emitFace({{
        {x1, y0, z1},
        {x1, y0, z0},
        {x1, y1, z0},
        {x1, y1, z1}
    }}, FACE_RIGHT, sideUV);
}

constexpr float kTorchModelPixel = 1.0f / 16.0f;
constexpr float kTorchModelCoreMin = 7.0f * kTorchModelPixel;
constexpr float kTorchModelCoreMax = 9.0f * kTorchModelPixel;
constexpr float kTorchModelCoreTop = 10.0f * kTorchModelPixel;

struct TorchModelUvRect {
    float u0;
    float v0;
    float u1;
    float v1;
};

TorchModelUvRect makeTorchModelSourceUvRect(const float left,
                                            const float top,
                                            const float right,
                                            const float bottom) {
    return {
        left * kTorchModelPixel,
        1.0f - bottom * kTorchModelPixel,
        right * kTorchModelPixel,
        1.0f - top * kTorchModelPixel
    };
}

glm::vec3 transformTorchModelPoint(const glm::mat4& transform, const glm::vec3& point) {
    return glm::vec3(transform * glm::vec4(point, 1.0f));
}

glm::mat4 makeTorchModelRotation(const float angleDegrees,
                                 const glm::vec3& axis,
                                 const glm::vec3& origin) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, origin);
    transform = glm::rotate(transform, glm::radians(angleDegrees), axis);
    transform = glm::translate(transform, -origin);
    return transform;
}

glm::mat4 buildWallTorchModelTransform(const uint16_t facingValue) {
    const glm::mat4 tilt = makeTorchModelRotation(-22.5f,
                                                  glm::vec3(0.0f, 0.0f, 1.0f),
                                                  glm::vec3(0.0f, 3.5f * kTorchModelPixel, 8.0f * kTorchModelPixel));

    float yDegrees = 0.0f;
    if (facingValue == PropIndices::FACING_NORTH) {
        yDegrees = 90.0f;
    } else if (facingValue == PropIndices::FACING_SOUTH) {
        yDegrees = -90.0f;
    } else if (facingValue == PropIndices::FACING_WEST) {
        yDegrees = 180.0f;
    } else if (facingValue == PropIndices::FACING_EAST) {
        yDegrees = 0.0f;
    }

    const glm::mat4 yaw = makeTorchModelRotation(yDegrees,
                                                 glm::vec3(0.0f, 1.0f, 0.0f),
                                                 glm::vec3(0.5f, 0.5f, 0.5f));
    return yaw * tilt;
}

void emitTorchModelFace(std::vector<BlockVertex>& vertices,
                        const glm::vec3& pos,
                        const float layer,
                        const float sunNorm,
                        const float blockNorm,
                        const int face,
                        const std::array<glm::vec3, 4>& localCorners,
                        const TorchModelUvRect& uvRect,
                        const glm::mat4& transform = glm::mat4(1.0f)) {
    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};
    const std::array<glm::vec2, 4> uv = {{
        {uvRect.u0, uvRect.v0},
        {uvRect.u1, uvRect.v0},
        {uvRect.u1, uvRect.v1},
        {uvRect.u0, uvRect.v1}
    }};

    for (const int idx : indices) {
        const glm::vec3 localPos = transformTorchModelPoint(transform, localCorners[static_cast<size_t>(idx)]);
        vertices.push_back({
            pos.x + localPos.x,
            pos.y + localPos.y,
            pos.z + localPos.z,
            uv[static_cast<size_t>(idx)].x,
            uv[static_cast<size_t>(idx)].y,
            static_cast<float>(face),
            sunNorm,
            blockNorm,
            3.0f,
            layer,
            1.0f,
            0.0f,
            0.0f
        });
    }
}

void emitTorchModelCuboidFaces(std::vector<BlockVertex>& vertices,
                               const glm::vec3& pos,
                               const float layer,
                               const float sunNorm,
                               const float blockNorm,
                               const glm::vec3& from,
                               const glm::vec3& to,
                               const glm::mat4& transform,
                               const bool emitTop,
                               const TorchModelUvRect& topUv,
                               const bool emitBottom,
                               const TorchModelUvRect& bottomUv,
                               const bool emitFront,
                               const TorchModelUvRect& frontUv,
                               const bool emitBack,
                               const TorchModelUvRect& backUv,
                               const bool emitLeft,
                               const TorchModelUvRect& leftUv,
                               const bool emitRight,
                               const TorchModelUvRect& rightUv) {
    if (emitTop) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_TOP, {{
            {from.x, to.y, to.z},
            {to.x, to.y, to.z},
            {to.x, to.y, from.z},
            {from.x, to.y, from.z}
        }}, topUv, transform);
    }
    if (emitBottom) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_BOTTOM, {{
            {from.x, from.y, from.z},
            {to.x, from.y, from.z},
            {to.x, from.y, to.z},
            {from.x, from.y, to.z}
        }}, bottomUv, transform);
    }
    if (emitFront) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_FRONT, {{
            {from.x, from.y, to.z},
            {to.x, from.y, to.z},
            {to.x, to.y, to.z},
            {from.x, to.y, to.z}
        }}, frontUv, transform);
    }
    if (emitBack) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_BACK, {{
            {to.x, from.y, from.z},
            {from.x, from.y, from.z},
            {from.x, to.y, from.z},
            {to.x, to.y, from.z}
        }}, backUv, transform);
    }
    if (emitLeft) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_LEFT, {{
            {from.x, from.y, from.z},
            {from.x, from.y, to.z},
            {from.x, to.y, to.z},
            {from.x, to.y, from.z}
        }}, leftUv, transform);
    }
    if (emitRight) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_RIGHT, {{
            {to.x, from.y, to.z},
            {to.x, from.y, from.z},
            {to.x, to.y, from.z},
            {to.x, to.y, to.z}
        }}, rightUv, transform);
    }
}

void addTorchTemplateImpl(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const BlockID blockId,
                          const int x,
                          const int y,
                          const int z,
                          const SubChunkMeshingSnapshot& snapshot) {
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(blockId);
    const float layer = static_cast<float>(textures.worldTop.firstLayer);

    uint8_t sunLevel = getResolvedSunlightSC(snapshot, x, y, z);
    uint8_t blockLevel = getResolvedBlockLightSC(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        sunLevel = std::max(sunLevel, getResolvedSunlightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
        blockLevel = std::max(blockLevel, getResolvedBlockLightSC(snapshot,
            x + kFaceNormals[static_cast<size_t>(d)].x,
            y + kFaceNormals[static_cast<size_t>(d)].y,
            z + kFaceNormals[static_cast<size_t>(d)].z));
    }
    const float sunNorm = lightToNormalized(sunLevel);
    const float blockNorm = lightToNormalized(blockLevel);

    uint16_t facingValue = PropIndices::FACING_FLOOR;
    if (PropIndices::FACING != PropIndices::INVALID) {
        facingValue = BlockStateRegistry::getPropertyIndex(blockId, PropIndices::FACING);
    }

    const TorchModelUvRect kTorchTopUv = makeTorchModelSourceUvRect(7.0f, 6.0f, 9.0f, 8.0f);
    const TorchModelUvRect kTorchBottomUv = makeTorchModelSourceUvRect(7.0f, 13.0f, 9.0f, 15.0f);
    const TorchModelUvRect kTorchFullUv = makeTorchModelSourceUvRect(0.0f, 0.0f, 16.0f, 16.0f);

    if (facingValue == PropIndices::FACING_FLOOR) {
        emitTorchModelCuboidFaces(vertices,
                                  pos,
                                  layer,
                                  sunNorm,
                                  blockNorm,
                                  glm::vec3(kTorchModelCoreMin, 0.0f, kTorchModelCoreMin),
                                  glm::vec3(kTorchModelCoreMax, kTorchModelCoreTop, kTorchModelCoreMax),
                                  glm::mat4(1.0f),
                                  true, kTorchTopUv,
                                  true, kTorchBottomUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv);
        emitTorchModelCuboidFaces(vertices,
                                  pos,
                                  layer,
                                  sunNorm,
                                  blockNorm,
                                  glm::vec3(kTorchModelCoreMin, 0.0f, 0.0f),
                                  glm::vec3(kTorchModelCoreMax, 1.0f, 1.0f),
                                  glm::mat4(1.0f),
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  true, kTorchFullUv,
                                  true, kTorchFullUv);
        emitTorchModelCuboidFaces(vertices,
                                  pos,
                                  layer,
                                  sunNorm,
                                  blockNorm,
                                  glm::vec3(0.0f, 0.0f, kTorchModelCoreMin),
                                  glm::vec3(1.0f, 1.0f, kTorchModelCoreMax),
                                  glm::mat4(1.0f),
                                  false, kTorchFullUv,
                                  false, kTorchFullUv,
                                  true, kTorchFullUv,
                                  true, kTorchFullUv,
                                  false, kTorchFullUv,
                                  false, kTorchFullUv);
        return;
    }

    const glm::mat4 wallTransform = buildWallTorchModelTransform(facingValue);
    emitTorchModelCuboidFaces(vertices,
                              pos,
                              layer,
                              sunNorm,
                              blockNorm,
                              glm::vec3(-1.0f * kTorchModelPixel, 3.5f * kTorchModelPixel, 7.0f * kTorchModelPixel),
                              glm::vec3( 1.0f * kTorchModelPixel, 13.5f * kTorchModelPixel, 9.0f * kTorchModelPixel),
                              wallTransform,
                              true, kTorchTopUv,
                              true, kTorchBottomUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv);
    emitTorchModelCuboidFaces(vertices,
                              pos,
                              layer,
                              sunNorm,
                              blockNorm,
                              glm::vec3(-1.0f * kTorchModelPixel, 3.5f * kTorchModelPixel, 0.0f),
                              glm::vec3( 1.0f * kTorchModelPixel, 19.5f * kTorchModelPixel, 1.0f),
                              wallTransform,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              true, kTorchFullUv,
                              true, kTorchFullUv);
    emitTorchModelCuboidFaces(vertices,
                              pos,
                              layer,
                              sunNorm,
                              blockNorm,
                              glm::vec3(-8.0f * kTorchModelPixel, 3.5f * kTorchModelPixel, 7.0f * kTorchModelPixel),
                              glm::vec3( 8.0f * kTorchModelPixel, 19.5f * kTorchModelPixel, 9.0f * kTorchModelPixel),
                              wallTransform,
                              false, kTorchFullUv,
                              false, kTorchFullUv,
                              true, kTorchFullUv,
                              true, kTorchFullUv,
                              false, kTorchFullUv,
                              false, kTorchFullUv);
}

} // anonymous namespace

void ChunkMeshBuilders::buildCross(ChunkMeshData& meshData,
                                   const SubChunkMeshingSnapshot& snapshot,
                                   const BlockID blockId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z) {
    addCrossedQuadsImpl(meshData.cutoutVertices,
                        glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                        blockId, def, x, y, z, snapshot);
    expandBounds(meshData,
                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
}

void ChunkMeshBuilders::buildTorch(ChunkMeshData& meshData,
                                    const SubChunkMeshingSnapshot& snapshot,
                                    const BlockID blockId,
                                    const BlockDef& /*def*/,
                                    const int x,
                                    const int y,
                                    const int z) {
    addTorchTemplateImpl(meshData.cutoutVertices,
                      glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                      blockId, x, y, z, snapshot);
    expandBounds(meshData,
                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
}

void ChunkMeshBuilders::buildWater(ChunkMeshData& meshData,
                                   const SubChunkMeshingSnapshot& snapshot,
                                   const BlockID blockId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z) {
    addWaterFacesImpl(meshData, snapshot, blockId, def, x, y, z);
}

void ChunkMeshBuilders::buildUnitFaces(ChunkMeshData& meshData,
                                       const SubChunkMeshingSnapshot& snapshot,
                                       const BlockID blockId,
                                       const BlockDef& def,
                                       const int x,
                                       const int y,
                                       const int z) {
    const bool transparent = def.isTransparent;
    for (int face = 0; face < 6; ++face) {
        const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
        const int nx = x + normal.x;
        const int ny = y + normal.y;
        const int nz = z + normal.z;

        if (!shouldRenderFaceImpl(snapshot, nx, ny, nz, blockId, def)) {
            continue;
        }

        auto& target = transparent ? meshData.transparentVertices : meshData.opaqueVertices;
        FaceRenderData renderData = buildFaceRenderData(snapshot, blockId, def, x, y, z, face);
        emitUnitFace(target,
                     glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                     face, renderData);
        expandBounds(meshData,
                     glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                     glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
    }
}


// ======================== Public API: Per-sub-chunk ========================

SubChunkMeshingSnapshotPtr ChunkMesher::captureSubChunkSnapshot(
    const Chunk& chunk,
    const int scy,
    const Chunk* neighborPosX,
    const Chunk* neighborNegX,
    const Chunk* neighborPosZ,
    const Chunk* neighborNegZ,
    const World* world) {
    auto snapshot = std::make_shared<SubChunkMeshingSnapshot>();
    snapshot->scy = scy;
    snapshot->yBase = scy * SubChunk::SIZE;
    snapshot->isTopSection = (scy == Chunk::NUM_SUB_CHUNKS - 1);
    snapshot->isBottomSection = (scy == 0);

    const SubChunk* sc = chunk.getSubChunk(scy);
    if (!sc) {
        // All-air sub-chunk — blocks and lightMap default to 0
        // Still capture borders for completeness
        captureSubChunkBorders(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, world);
        captureSubChunkHalo(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, world);
        return snapshot;
    }

    // Copy block data
    sc->copyBlocksTo(snapshot->blocks);

    // Copy fluid layer data
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
                snapshot->fluidBlocks[scToIndex(lx, ly, lz)] = sc->getFluidLayer(lx, ly, lz);
            }
        }
    }

    // Copy light data
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
                snapshot->lightMap[scToIndex(lx, ly, lz)] = sc->m_lightMap[SubChunk::toIndex(lx, ly, lz)];
            }
        }
    }

    // Capture all 6-direction borders
    captureSubChunkBorders(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, world);
    captureSubChunkHalo(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, world);

    return snapshot;
}

SubChunkMeshingSnapshotPtr ChunkMesher::captureSubChunkSnapshot(
    const Chunk& chunk,
    const int scy,
    const World* world) {
    return captureSubChunkSnapshot(chunk, scy,
                                   chunk.neighbors[0], chunk.neighbors[1],
                                   chunk.neighbors[2], chunk.neighbors[3],
                                   world);
}

ChunkMeshData ChunkMesher::buildSubChunkMeshData(const SubChunkMeshingSnapshot& snapshot) {
    const auto startTime = std::chrono::steady_clock::now();

    ChunkMeshData meshData;
    meshData.opaqueVertices.reserve(2048);
    meshData.cutoutVertices.reserve(512);
    meshData.transparentVertices.reserve(1024);

    buildOpaqueGreedyFaces(snapshot, meshData);
    buildTransparentGreedyFaces(snapshot, meshData);

    // Non-cube blocks (cross shapes, etc.) and waterlogged fluid rendering
    constexpr int S = SubChunk::SIZE;
    for (int y = 0; y < S; ++y) {
        for (int z = 0; z < S; ++z) {
            for (int x = 0; x < S; ++x) {
                const BlockID blockId = snapshot.blocks[scToIndex(x, y, z)];
                const BlockID fluidId = snapshot.fluidBlocks[scToIndex(x, y, z)];

                // Render the block (if any)
                if (blockId != 0) {
                    const BlockDef& def = BlockRegistry::getFast(blockId);

                    if (!isOpaqueCubeCandidate(def) && !isTransparentCubeCandidate(def)) {
                        MeshBuilderFn builder = MeshBuilderRegistry::getBuilder(def.renderShapeTag);
                        if (builder == nullptr) {
                            builder = &ChunkMeshBuilders::buildUnitFaces;
                        }
                        builder(meshData, snapshot, blockId, def, x, y, z);
                    }
                }

                // Render waterlogged fluid overlay
                if (fluidId != 0 && FluidState::decode(fluidId).kind != FluidKind::None) {
                    // Only render water for waterlogged blocks (non-fluid block present)
                    // Pure water positions are already handled by the water builder above
                    if (blockId != 0 && FluidState::decode(blockId).kind == FluidKind::None) {
                        const BlockDef& fluidDef = BlockRegistry::getFast(fluidId);
                        ChunkMeshBuilders::buildWater(meshData, snapshot, fluidId, fluidDef, x, y, z);
                    }
                }
            }
        }
    }

    meshData.opaqueVertexCount = static_cast<uint32_t>(meshData.opaqueVertices.size());
    meshData.buildTimeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
    return meshData;
}

void ChunkMesher::generateSubChunkMesh(Chunk& chunk, const int scy) {
    const SubChunkMeshingSnapshotPtr snapshot = captureSubChunkSnapshot(chunk, scy);
    ChunkMeshData meshData = buildSubChunkMeshData(*snapshot);

    // Offset bounds from sub-chunk local to column-local
    const int yBase = scy * SubChunk::SIZE;
    if (meshData.hasBounds) {
        meshData.boundsMin.y += static_cast<float>(yBase);
        meshData.boundsMax.y += static_cast<float>(yBase);
    }

    SubChunkMesh mesh;
    mesh.upload(meshData.opaqueVertices);
    mesh.uploadCutout(meshData.cutoutVertices);
    mesh.uploadTransparent(meshData.transparentVertices);
    mesh.hasBounds = meshData.hasBounds;
    mesh.boundsMin = meshData.boundsMin;
    mesh.boundsMax = meshData.boundsMax;
    chunk.setSubChunkMesh(scy, mesh);
}

bool ChunkMesher::shouldSkipSubChunk(const Chunk& chunk, const int scy) {
    const SubChunk* sc = chunk.getSubChunk(scy);
    if (!sc) {
        return true;
    }

    const SubChunkType type = sc->getType();
    if (type == SubChunkType::Air) {
        return true;
    }
    if (type != SubChunkType::Solid) {
        return false;
    }

    for (const SubChunk* neighbor : sc->neighbors) {
        if (!neighbor || neighbor->getType() != SubChunkType::Solid) {
            return false;
        }
    }

    return true;
}
