#include "ChunkMesher.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include "MeshBuilderRegistry.h"
#include "../../world/fluid/FluidFlow.h"
#include "../../world/block/BlockModelRegistry.h"
#include "../../world/block/BlockStateRegistry.h"
#include "../../world/fluid/FluidState.h"
#include "../../world/block/PropIndices.h"
#include "../../world/redstone/WireFaceGeometry.h"
#include "../../world/IWorldView.h"
#include "../../world/World.h"

#if defined(_MSC_VER)
#define MECRAFT_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MECRAFT_FORCEINLINE inline __attribute__((always_inline))
#else
#define MECRAFT_FORCEINLINE inline
#endif

namespace {
std::atomic_bool g_debugDisableGreedyMeshing{false};

struct IVec3 {
    int x;
    int y;
    int z;
};

struct VertexLightData {
    uint8_t ao = 0;
    uint8_t sunLight = 0;
    uint8_t blockLight = 0;
    uint16_t sunKey = 0;
    uint16_t blockKey = 0;
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
    uint8_t tintKind = BlockTintKinds::NONE;
    uint8_t tintU = 0;
    uint8_t tintV = 0;
    uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT;
    uint8_t uvQuarterTurns = 0;
};

struct CachedModelFace {
    std::array<glm::vec3, 4> localCorners{};
    std::array<glm::vec2, 4> uv{};
    std::string textureName;
    int transformedFace = 0;
    uint8_t cullfaceBits = 0;
    int8_t tintIndex = -1;
    bool ambientOcclusion = true;
};

struct CachedModelGeometry {
    std::vector<CachedModelFace> faces;
};

struct ModelGeometryCacheKey {
    const BlockModel* model = nullptr;
    uint16_t rotX = 0;
    uint16_t rotY = 0;
    uint16_t rotZ = 0;
    bool uvLock = false;

    bool operator==(const ModelGeometryCacheKey& other) const {
        return model == other.model &&
               rotX == other.rotX &&
               rotY == other.rotY &&
               rotZ == other.rotZ &&
               uvLock == other.uvLock;
    }
};

struct ModelGeometryCacheKeyHash {
    size_t operator()(const ModelGeometryCacheKey& key) const {
        size_t hash = std::hash<const BlockModel*>{}(key.model);
        hash ^= static_cast<size_t>(key.rotX) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<size_t>(key.rotY) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<size_t>(key.rotZ) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<size_t>(key.uvLock ? 1u : 0u) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

std::mutex g_modelGeometryCacheMutex;
std::unordered_map<ModelGeometryCacheKey,
                   std::shared_ptr<const CachedModelGeometry>,
                   ModelGeometryCacheKeyHash> g_modelGeometryCache;

struct FaceMergeKey {
    BlockStateId stateId = NULL_BLOCK_STATE;
    int tileIndex = 0;
    bool flipDiagonal = false;
    uint8_t tintKind = BlockTintKinds::NONE;
    uint8_t tintU = 0;
    uint8_t tintV = 0;
    uint8_t derivativeMaterialId = DerivativeMaterialIds::DEFAULT;
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

constexpr size_t kMaxGreedyPlaneSize = static_cast<size_t>(SubChunk::SIZE) * SubChunk::SIZE;

struct MeshFaceInfo {
    int tileIndex = 0;
    float layer = 0.0f;
    float animationFrameCount = 1.0f;
    float animationFps = 0.0f;
    float animated = 0.0f;
    uint8_t uvQuarterTurns = 0;
};

enum class MeshCubeClass : uint8_t {
    Air,
    Opaque,
    Transparent,
    Water,
    Cutout,
    CutoutDistance,
    Other
};

struct MeshBlockInfo {
    const BlockDef* def = nullptr;
    std::array<MeshFaceInfo, 6> faces{};
    MeshCubeClass cubeClass = MeshCubeClass::Air;
    bool isSolid = false;
    bool isTransparent = false;
};

MECRAFT_FORCEINLINE const MeshBlockInfo& getMeshBlockInfo(BlockStateId stateId);

constexpr int FACE_TOP = 0;
constexpr int FACE_BOTTOM = 1;
constexpr int FACE_FRONT = 2;
constexpr int FACE_BACK = 3;
constexpr int FACE_LEFT = 4;
constexpr int FACE_RIGHT = 5;
constexpr float CROSS_BIOME_TINT_MARKER = -1.0f;
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
MECRAFT_FORCEINLINE std::size_t scToIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(z) * SubChunk::SIZE +
           static_cast<std::size_t>(y) * SubChunk::SIZE * SubChunk::SIZE;
}

MECRAFT_FORCEINLINE std::size_t haloToIndex(const int x, const int y, const int z) {
    return static_cast<std::size_t>(x + 1) +
           static_cast<std::size_t>(z + 1) * SC_HALO_SIZE +
           static_cast<std::size_t>(y + 1) * SC_HALO_SIZE * SC_HALO_SIZE;
}

// Border index helpers for sub-chunk borders (16x16 slice)
std::size_t toBorderXZIndex(const int x, const int z) {
    return static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * SubChunk::SIZE;
}

// ======================== Neighbor-aware block/light lookup (sub-chunk) ========================

BlockStateId getNeighborAwareBlockSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    // Out of Y range — above top = air, below bottom = stone/air
    if (y < 0) {
        if (snapshot.isBottomSection) return NULL_BLOCK_STATE;
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return NULL_BLOCK_STATE;
        return snapshot.negYBorder[toBorderXZIndex(x, z)];
    }
    if (y >= SubChunk::SIZE) {
        if (snapshot.isTopSection) return NULL_BLOCK_STATE;  // Above world = air
        if (x < 0 || x >= SubChunk::SIZE || z < 0 || z >= SubChunk::SIZE) return NULL_BLOCK_STATE;
        return snapshot.posYBorder[toBorderXZIndex(x, z)];
    }

    // X borders
    if (x < 0) {
        if (z < 0 || z >= SubChunk::SIZE) return NULL_BLOCK_STATE;
        return snapshot.negXBorder[toBorderXZIndex(y, z)];
    }
    if (x >= SubChunk::SIZE) {
        if (z < 0 || z >= SubChunk::SIZE) return NULL_BLOCK_STATE;
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

MECRAFT_FORCEINLINE BlockStateId getResolvedBlockSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (x < -1 || x > SubChunk::SIZE ||
        y < -1 || y > SubChunk::SIZE ||
        z < -1 || z > SubChunk::SIZE) {
        return NULL_BLOCK_STATE;
    }
    return snapshot.haloBlocks[haloToIndex(x, y, z)];
}

BlockStateId getResolvedFluidSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    if (x < -1 || x > SubChunk::SIZE ||
        y < -1 || y > SubChunk::SIZE ||
        z < -1 || z > SubChunk::SIZE) {
        return NULL_BLOCK_STATE;
    }
    const BlockStateId fluidState = snapshot.haloFluidBlocks[haloToIndex(x, y, z)];
    if (fluidState != NULL_BLOCK_STATE) {
        return fluidState;
    }
    const BlockStateId blockState = snapshot.haloBlocks[haloToIndex(x, y, z)];
    if (FluidState::decode(blockState).kind != FluidKind::None) {
        return blockState;
    }
    return NULL_BLOCK_STATE;
}

MECRAFT_FORCEINLINE uint8_t getResolvedLightSC(const SubChunkMeshingSnapshot& snapshot, int x, int y, int z) {
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

MECRAFT_FORCEINLINE uint8_t computeVertexAO(const bool side1, const bool side2, const bool corner) {
    if (side1 && side2) {
        return 0;
    }
    return static_cast<uint8_t>(3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner)));
}

MECRAFT_FORCEINLINE bool isSolidForAO(const SubChunkMeshingSnapshot& snapshot, const int x, const int y, const int z) {
    const BlockStateId stateId = getResolvedBlockSC(snapshot, x, y, z);
    return getMeshBlockInfo(stateId).isSolid;
}

MECRAFT_FORCEINLINE uint8_t getSafePackedLightForAO(const SubChunkMeshingSnapshot& snapshot,
                                                    const int x,
                                                    const int y,
                                                    const int z,
                                                    const bool isSolid,
                                                    const uint8_t basePacked) {
    if (isSolid) {
        return basePacked;
    }
    if (y >= SubChunk::SIZE && snapshot.isTopSection) {
        return 0xF0;
    }
    return getResolvedLightSC(snapshot, x, y, z);
}

MECRAFT_FORCEINLINE float lightToNormalized(const uint8_t level) {
    return static_cast<float>(level) / 15.0f;
}

MECRAFT_FORCEINLINE float computeVertexNormalized(const uint8_t base,
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

MECRAFT_FORCEINLINE uint16_t computeVertexLightKey(const uint8_t base,
                                                   const uint8_t s1,
                                                   const uint8_t s2,
                                                   const uint8_t cn,
                                                   const bool s1Solid,
                                                   const bool s2Solid) {
    const int sum = s1Solid && s2Solid
        ? static_cast<int>(base) + static_cast<int>(s1) + static_cast<int>(s2)
        : static_cast<int>(base) + static_cast<int>(s1) + static_cast<int>(s2) + static_cast<int>(cn);
    const int denominator = s1Solid && s2Solid ? 45 : 60;
    return static_cast<uint16_t>((sum * static_cast<int>(kNormalizedQuantizationScale) + denominator / 2) /
                                 denominator);
}

void computeTintMapPosition(const SubChunkMeshingSnapshot& snapshot,
                            const int x,
                            const int z,
                            uint8_t& outU,
                            uint8_t& outV) {
    const int worldX = snapshot.worldOffsetX + x;
    const int worldZ = snapshot.worldOffsetZ + z;
    double temperature = 0.70;
    double moisture = 0.65;

    if (snapshot.worldView != nullptr) {
        switch (snapshot.worldView->getBiome(worldX, worldZ)) {
            case TerrainBiome::Arid:
                temperature = 0.95;
                moisture = 0.18;
                break;
            case TerrainBiome::Mountain:
                temperature = 0.55;
                moisture = 0.50;
                break;
            case TerrainBiome::HighMountain:
                temperature = 0.35;
                moisture = 0.40;
                break;
            case TerrainBiome::Temperate:
            default:
                temperature = 0.70;
                moisture = 0.65;
                break;
        }

        const double detail = std::sin(static_cast<double>(worldX) * 0.071 + static_cast<double>(worldZ) * 0.043) * 0.035;
        temperature = std::clamp(temperature + detail, 0.0, 1.0);
        moisture = std::clamp(moisture - detail, 0.0, 1.0);
    }

    outU = static_cast<uint8_t>(std::lround((1.0 - temperature) * 255.0));
    outV = static_cast<uint8_t>(std::lround((1.0 - moisture * temperature) * 255.0));
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
    const uint8_t basePacked = getResolvedLightSC(snapshot, bx, by, bz);
    const uint8_t baseSun = static_cast<uint8_t>((basePacked >> 4) & 0x0F);
    const uint8_t baseBlock = static_cast<uint8_t>(basePacked & 0x0F);

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

        const uint8_t s1Packed = getSafePackedLightForAO(snapshot, s1[0], s1[1], s1[2], side1, basePacked);
        const uint8_t s2Packed = getSafePackedLightForAO(snapshot, s2[0], s2[1], s2[2], side2, basePacked);
        const uint8_t cnPacked = getSafePackedLightForAO(snapshot, cn[0], cn[1], cn[2], cornerSolid, basePacked);

        const uint8_t s1Sun = static_cast<uint8_t>((s1Packed >> 4) & 0x0F);
        const uint8_t s2Sun = static_cast<uint8_t>((s2Packed >> 4) & 0x0F);
        const uint8_t cnSun = static_cast<uint8_t>((cnPacked >> 4) & 0x0F);
        const uint8_t s1Block = static_cast<uint8_t>(s1Packed & 0x0F);
        const uint8_t s2Block = static_cast<uint8_t>(s2Packed & 0x0F);
        const uint8_t cnBlock = static_cast<uint8_t>(cnPacked & 0x0F);

        if (side1 && side2) {
            data[i].sunLight = static_cast<uint8_t>((baseSun + s1Sun + s2Sun) / 3);
            data[i].blockLight = static_cast<uint8_t>((baseBlock + s1Block + s2Block) / 3);
        } else {
            data[i].sunLight = static_cast<uint8_t>((baseSun + s1Sun + s2Sun + cnSun) / 4);
            data[i].blockLight = static_cast<uint8_t>((baseBlock + s1Block + s2Block + cnBlock) / 4);
        }

        data[i].sunNormalized = computeVertexNormalized(baseSun, s1Sun, s2Sun, cnSun, side1, side2);
        data[i].blockNormalized = computeVertexNormalized(baseBlock, s1Block, s2Block, cnBlock, side1, side2);
        data[i].sunKey = computeVertexLightKey(baseSun, s1Sun, s2Sun, cnSun, side1, side2);
        data[i].blockKey = computeVertexLightKey(baseBlock, s1Block, s2Block, cnBlock, side1, side2);
    }

    return data;
}

const AnimatedTextureRef& getFaceTextureRef(const StateTextureIndices& textures, const int face) {
    switch (face) {
        case FACE_TOP:    return textures.faceTop;
        case FACE_BOTTOM: return textures.faceBottom;
        case FACE_FRONT:  return textures.faceFront;
        case FACE_BACK:   return textures.faceBack;
        case FACE_LEFT:   return textures.faceLeft;
        case FACE_RIGHT:  return textures.faceRight;
        default:          return textures.faceTop;
    }
}

uint8_t getFaceUvQuarterTurns(const BlockStateId stateId, const int face) {
    if (PropIndices::AXIS == PropIndices::INVALID) {
        return 0;
    }

    const uint16_t axisValue = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::AXIS);
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

MeshFaceInfo buildMeshFaceInfo(const BlockStateId stateId, const int face) {
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(stateId);
    const AnimatedTextureRef& faceTexture = getFaceTextureRef(textures, face);

    MeshFaceInfo info;
    info.tileIndex = std::max(0, faceTexture.firstLayer);
    info.layer = static_cast<float>(faceTexture.firstLayer);
    info.animationFrameCount = static_cast<float>(std::max<uint16_t>(1, faceTexture.frameCount));
    info.animationFps = faceTexture.isAnimated ? faceTexture.fps : 0.0f;
    info.animated = faceTexture.isAnimated ? 1.0f : 0.0f;
    info.uvQuarterTurns = getFaceUvQuarterTurns(stateId, face);
    return info;
}

FaceRenderData buildFaceRenderData(const SubChunkMeshingSnapshot& snapshot,
                                   const BlockStateId stateId,
                                   const BlockDef& def,
                                   const MeshBlockInfo& info,
                                   const int x,
                                   const int y,
                                   const int z,
                                   const int face) {
    FaceRenderData renderData;
    const MeshFaceInfo& faceInfo = info.faces[static_cast<size_t>(face)];
    renderData.tileIndex = faceInfo.tileIndex;
    renderData.layer = faceInfo.layer;
    renderData.animationFrameCount = faceInfo.animationFrameCount;
    renderData.animationFps = faceInfo.animationFps;
    renderData.animated = faceInfo.animated;
    renderData.tintKind = blockTintKindFromBiomeTint(def.biomeTint);
    renderData.derivativeMaterialId = def.derivativeMaterialId;
    if (renderData.tintKind != BlockTintKinds::NONE) {
        computeTintMapPosition(snapshot, x, z, renderData.tintU, renderData.tintV);
    }
    renderData.vertices = computeFaceVertexData(snapshot, x, y, z, face);
    renderData.uvQuarterTurns = faceInfo.uvQuarterTurns;

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

FaceRenderData buildFaceRenderData(const SubChunkMeshingSnapshot& snapshot,
                                   const BlockStateId stateId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z,
                                   const int face) {
    return buildFaceRenderData(snapshot, stateId, def, getMeshBlockInfo(stateId), x, y, z, face);
}

uint64_t computeMergeKeyHash(const FaceMergeKey& key) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<uint64_t>(key.stateId.registryIndex()));
    mix(static_cast<uint64_t>(key.tileIndex));
    mix(static_cast<uint64_t>(key.flipDiagonal));
    mix(static_cast<uint64_t>(key.tintKind));
    mix(static_cast<uint64_t>(key.tintU));
    mix(static_cast<uint64_t>(key.tintV));
    mix(static_cast<uint64_t>(key.derivativeMaterialId));
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

FaceMergeKey buildFaceMergeKey(const BlockStateId stateId, const FaceRenderData& renderData) {
    FaceMergeKey key;
    key.stateId = stateId;
    key.tileIndex = renderData.tileIndex;
    key.flipDiagonal = renderData.flipDiagonal;
    key.tintKind = renderData.tintKind;
    key.tintU = renderData.tintU;
    key.tintV = renderData.tintV;
    key.derivativeMaterialId = renderData.derivativeMaterialId;
    key.uvQuarterTurns = renderData.uvQuarterTurns;
    for (size_t i = 0; i < renderData.vertices.size(); ++i) {
        key.ao[i] = renderData.vertices[i].ao;
        key.sun[i] = renderData.vertices[i].sunKey;
        key.block[i] = renderData.vertices[i].blockKey;
    }
    key.hash = computeMergeKeyHash(key);
    return key;
}

MECRAFT_FORCEINLINE bool sameMergeKeyPayload(const FaceMergeKey& lhs, const FaceMergeKey& rhs) {
    return lhs.stateId == rhs.stateId &&
           lhs.tileIndex == rhs.tileIndex &&
           lhs.flipDiagonal == rhs.flipDiagonal &&
           lhs.tintKind == rhs.tintKind &&
           lhs.tintU == rhs.tintU &&
           lhs.tintV == rhs.tintV &&
           lhs.derivativeMaterialId == rhs.derivativeMaterialId &&
           lhs.uvQuarterTurns == rhs.uvQuarterTurns &&
           lhs.ao == rhs.ao &&
           lhs.sun == rhs.sun &&
           lhs.block == rhs.block;
}

MECRAFT_FORCEINLINE bool sameMergeKey(const FaceMergeKey& lhs, const FaceMergeKey& rhs) {
    return lhs.hash == rhs.hash &&
           sameMergeKeyPayload(lhs, rhs);
}

MECRAFT_FORCEINLINE bool samePlaneFaceCell(const FaceCell& lhs, const FaceCell& rhs) {
    return rhs.valid &&
           lhs.key.hash == rhs.key.hash &&
           sameMergeKeyPayload(lhs.key, rhs.key);
}

std::vector<MeshBlockInfo> g_meshBlockInfoCache;
std::once_flag g_meshBlockInfoCacheInitFlag;

MeshBlockInfo buildMeshBlockInfo(const BlockStateId stateId) {
    MeshBlockInfo info;
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& def = BlockRegistry::getFast(blockId);
    info.def = &def;
    info.isSolid = def.isSolid;
    info.isTransparent = def.isTransparent;
    for (int face = 0; face < 6; ++face) {
        info.faces[static_cast<size_t>(face)] = buildMeshFaceInfo(stateId, face);
    }

    if (stateId == NULL_BLOCK_STATE) {
        info.cubeClass = MeshCubeClass::Air;
    } else if (def.renderShape != BlockRenderShape::Cube) {
        info.cubeClass = MeshCubeClass::Other;
    } else if (def.renderLayer == BlockRenderLayer::Opaque) {
        info.cubeClass = MeshCubeClass::Opaque;
    } else if (def.renderLayer == BlockRenderLayer::Transparent) {
        info.cubeClass = usesWaterRendering(def) ? MeshCubeClass::Water : MeshCubeClass::Transparent;
    } else if (def.renderLayer == BlockRenderLayer::Cutout) {
        info.cubeClass = def.cutoutDistanceCull ? MeshCubeClass::CutoutDistance : MeshCubeClass::Cutout;
    } else {
        info.cubeClass = MeshCubeClass::Other;
    }
    return info;
}

void ensureMeshBlockInfoCache() {
    std::call_once(g_meshBlockInfoCacheInitFlag, []() {
        const std::size_t count = BlockStateRegistry::getStateCount();
        g_meshBlockInfoCache.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            g_meshBlockInfoCache[i] = buildMeshBlockInfo(BlockStateId::fromRegistryIndex(i));
        }
    });
}

MECRAFT_FORCEINLINE const MeshBlockInfo& getMeshBlockInfo(const BlockStateId stateId) {
    return g_meshBlockInfoCache[stateId.registryIndex()];
}

struct SubChunkMeshClassPresence {
    bool hasOpaqueCube = false;
    bool hasTransparentCube = false;
    bool hasWaterCube = false;
    bool hasCutoutCube = false;
    bool hasCutoutDistanceCube = false;
    bool hasCustomBlock = false;
    bool hasFluidLayer = false;
    bool hasAnyWater = false;
};

SubChunkMeshClassPresence scanMeshClassPresence(const SubChunkMeshingSnapshot& snapshot) {
    SubChunkMeshClassPresence presence;
    for (std::size_t i = 0; i < SC_BLOCK_COUNT; ++i) {
        const BlockStateId stateId = snapshot.blocks[i];
        if (stateId != NULL_BLOCK_STATE) {
            switch (getMeshBlockInfo(stateId).cubeClass) {
                case MeshCubeClass::Opaque:
                    presence.hasOpaqueCube = true;
                    break;
                case MeshCubeClass::Transparent:
                    presence.hasTransparentCube = true;
                    break;
                case MeshCubeClass::Water:
                    presence.hasWaterCube = true;
                    presence.hasAnyWater = true;
                    break;
                case MeshCubeClass::Cutout:
                    presence.hasCutoutCube = true;
                    break;
                case MeshCubeClass::CutoutDistance:
                    presence.hasCutoutDistanceCube = true;
                    break;
                case MeshCubeClass::Air:
                case MeshCubeClass::Other:
                    presence.hasCustomBlock = true;
                    break;
                default:
                    break;
            }

            if (!presence.hasAnyWater && FluidState::isWater(stateId)) {
                presence.hasAnyWater = true;
            }
        }

        const BlockStateId fluidState = snapshot.fluidBlocks[i];
        if (fluidState != NULL_BLOCK_STATE) {
            presence.hasFluidLayer = true;
            if (FluidState::isWater(fluidState)) {
                presence.hasAnyWater = true;
            }
        }
    }
    return presence;
}

bool shouldRenderFaceImpl(const SubChunkMeshingSnapshot& snapshot,
                          const int nx,
                          const int ny,
                          const int nz,
                          const BlockStateId currentState,
                          const BlockDef& currentDef) {
    const BlockStateId neighborState = getResolvedBlockSC(snapshot, nx, ny, nz);
    if (currentDef.renderShape == BlockRenderShape::Cube &&
        currentDef.isTransparent &&
        neighborState == currentState) {
        return false;
    }

    if (neighborState == NULL_BLOCK_STATE) {
        return true;
    }

    const MeshBlockInfo& neighborInfo = getMeshBlockInfo(neighborState);

    if (!neighborInfo.isSolid) {
        return true;
    }

    if (neighborInfo.isTransparent) {
        if (!currentDef.isTransparent) {
            return true;
        }
        return neighborState != currentState;
    }

    return false;
}

MECRAFT_FORCEINLINE bool shouldRenderOpaqueCubeFace(const SubChunkMeshingSnapshot& snapshot,
                                                    const int nx,
                                                    const int ny,
                                                    const int nz) {
    const BlockStateId neighborState = getResolvedBlockSC(snapshot, nx, ny, nz);
    if (neighborState == NULL_BLOCK_STATE) {
        return true;
    }

    const MeshBlockInfo& neighborInfo = getMeshBlockInfo(neighborState);
    return !neighborInfo.isSolid || neighborInfo.isTransparent;
}

// ======================== Snapshot capture helpers ========================

const Chunk* getDirectHorizontalNeighbor(const int dx,
                                         const int dz,
                                         const Chunk* neighborPosX,
                                         const Chunk* neighborNegX,
                                         const Chunk* neighborPosZ,
                                         const Chunk* neighborNegZ) {
    if (dx > 0) return neighborPosX;
    if (dx < 0) return neighborNegX;
    if (dz > 0) return neighborPosZ;
    if (dz < 0) return neighborNegZ;
    return nullptr;
}

uint8_t maxPackedLightComponents(const uint8_t lhs, const uint8_t rhs) {
    const uint8_t sky = std::max(static_cast<uint8_t>((lhs >> 4) & 0x0F),
                                 static_cast<uint8_t>((rhs >> 4) & 0x0F));
    const uint8_t block = std::max(static_cast<uint8_t>(lhs & 0x0F),
                                   static_cast<uint8_t>(rhs & 0x0F));
    return static_cast<uint8_t>((sky << 4) | block);
}

const Chunk* resolveHorizontalSampleChunk(const int localX,
                                          const int localZ,
                                          const Chunk* neighborPosX,
                                          const Chunk* neighborNegX,
                                          const Chunk* neighborPosZ,
                                          const Chunk* neighborNegZ,
                                          int& outLocalX,
                                          int& outLocalZ) {
    const int dx = localX < 0 ? -1 : (localX >= Chunk::SIZE_X ? 1 : 0);
    const int dz = localZ < 0 ? -1 : (localZ >= Chunk::SIZE_Z ? 1 : 0);
    outLocalX = std::clamp(localX, 0, Chunk::SIZE_X - 1);
    outLocalZ = std::clamp(localZ, 0, Chunk::SIZE_Z - 1);

    if (dx == 0 && dz == 0) {
        return nullptr;
    }

    if (dx != 0) {
        outLocalX = dx > 0 ? 0 : Chunk::SIZE_X - 1;
    }
    if (dz != 0) {
        outLocalZ = dz > 0 ? 0 : Chunk::SIZE_Z - 1;
    }

    if (dx == 0 || dz == 0) {
        return getDirectHorizontalNeighbor(dx, dz,
                                           neighborPosX, neighborNegX,
                                           neighborPosZ, neighborNegZ);
    }

    return nullptr;
}

uint8_t fallbackHorizontalEdgeLight(const Chunk& chunk,
                                    const int localX,
                                    const int worldY,
                                    const int localZ,
                                    const Chunk* neighborPosX,
                                    const Chunk* neighborNegX,
                                    const Chunk* neighborPosZ,
                                    const Chunk* neighborNegZ) {
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) {
        return 0;
    }

    const int clampedX = std::clamp(localX, 0, Chunk::SIZE_X - 1);
    const int clampedZ = std::clamp(localZ, 0, Chunk::SIZE_Z - 1);
    uint8_t best = chunk.getPackedLight(clampedX, worldY, clampedZ);

    const bool xOut = localX < 0 || localX >= Chunk::SIZE_X;
    const bool zOut = localZ < 0 || localZ >= Chunk::SIZE_Z;
    if (xOut) {
        const Chunk* xNeighbor = localX < 0 ? neighborNegX : neighborPosX;
        if (xNeighbor) {
            const int nx = localX < 0 ? Chunk::SIZE_X - 1 : 0;
            best = maxPackedLightComponents(best, xNeighbor->getPackedLight(nx, worldY, clampedZ));
        }
    }
    if (zOut) {
        const Chunk* zNeighbor = localZ < 0 ? neighborNegZ : neighborPosZ;
        if (zNeighbor) {
            const int nz = localZ < 0 ? Chunk::SIZE_Z - 1 : 0;
            best = maxPackedLightComponents(best, zNeighbor->getPackedLight(clampedX, worldY, nz));
        }
    }

    return best;
}

BlockStateId sampleHaloBlock(const Chunk& chunk,
                             const int localX,
                             const int worldY,
                             const int localZ,
                             const Chunk* neighborPosX,
                             const Chunk* neighborNegX,
                             const Chunk* neighborPosZ,
                             const Chunk* neighborNegZ,
                             const IWorldView* worldView) {
    static_cast<void>(worldView);
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getBlock(localX, worldY, localZ);
    }

    int sampleX = 0;
    int sampleZ = 0;
    if (const Chunk* sampleChunk = resolveHorizontalSampleChunk(localX, localZ,
                                                                neighborPosX, neighborNegX,
                                                                neighborPosZ, neighborNegZ,
                                                                sampleX, sampleZ)) {
        return sampleChunk->getBlock(sampleX, worldY, sampleZ);
    }

    return NULL_BLOCK_STATE;
}

BlockStateId sampleHaloFluid(const Chunk& chunk,
                             const int localX,
                             const int worldY,
                             const int localZ,
                             const Chunk* neighborPosX,
                             const Chunk* neighborNegX,
                             const Chunk* neighborPosZ,
                             const Chunk* neighborNegZ,
                             const IWorldView* worldView) {
    static_cast<void>(worldView);
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getFluidState(localX, worldY, localZ);
    }

    int sampleX = 0;
    int sampleZ = 0;
    if (const Chunk* sampleChunk = resolveHorizontalSampleChunk(localX, localZ,
                                                                neighborPosX, neighborNegX,
                                                                neighborPosZ, neighborNegZ,
                                                                sampleX, sampleZ)) {
        return sampleChunk->getFluidState(sampleX, worldY, sampleZ);
    }

    return NULL_BLOCK_STATE;
}

uint8_t sampleHaloLight(const Chunk& chunk,
                        const int localX,
                        const int worldY,
                        const int localZ,
                        const Chunk* neighborPosX,
                        const Chunk* neighborNegX,
                        const Chunk* neighborPosZ,
                        const Chunk* neighborNegZ,
                        const IWorldView* worldView) {
    static_cast<void>(worldView);
    const bool xInRange = localX >= 0 && localX < Chunk::SIZE_X;
    const bool zInRange = localZ >= 0 && localZ < Chunk::SIZE_Z;
    if (xInRange && zInRange) {
        return chunk.getPackedLight(localX, worldY, localZ);
    }

    int sampleX = 0;
    int sampleZ = 0;
    if (const Chunk* sampleChunk = resolveHorizontalSampleChunk(localX, localZ,
                                                                neighborPosX, neighborNegX,
                                                                neighborPosZ, neighborNegZ,
                                                                sampleX, sampleZ)) {
        return sampleChunk->getPackedLight(sampleX, worldY, sampleZ);
    }

    return fallbackHorizontalEdgeLight(chunk, localX, worldY, localZ,
                                       neighborPosX, neighborNegX,
                                       neighborPosZ, neighborNegZ);
}

void captureSubChunkHalo(const Chunk& chunk,
                         const int scy,
                         SubChunkMeshingSnapshot& snapshot,
                         const Chunk* neighborPosX,
                         const Chunk* neighborNegX,
                         const Chunk* neighborPosZ,
                         const Chunk* neighborNegZ,
                         const IWorldView* worldView) {
    const int yBase = scy * SubChunk::SIZE;
    for (int ly = -1; ly <= SubChunk::SIZE; ++ly) {
        const int worldY = yBase + ly;
        for (int lz = -1; lz <= SubChunk::SIZE; ++lz) {
            for (int lx = -1; lx <= SubChunk::SIZE; ++lx) {
                const std::size_t haloIdx = haloToIndex(lx, ly, lz);
                snapshot.haloBlocks[haloIdx] = sampleHaloBlock(chunk, lx, worldY, lz,
                                                               neighborPosX, neighborNegX,
                                                               neighborPosZ, neighborNegZ,
                                                               worldView);
                snapshot.haloFluidBlocks[haloIdx] = sampleHaloFluid(chunk, lx, worldY, lz,
                                                                    neighborPosX, neighborNegX,
                                                                    neighborPosZ, neighborNegZ,
                                                                    worldView);
                snapshot.haloLightMap[haloIdx] = sampleHaloLight(chunk, lx, worldY, lz,
                                                                 neighborPosX, neighborNegX,
                                                                 neighborPosZ, neighborNegZ,
                                                                 worldView);
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
                            const IWorldView* worldView) {
    static_cast<void>(worldView);
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

    // Horizontal borders: sample only neighbors captured by this job; missing
    // neighbors stay air so the worker never invents terrain outside its snapshot.
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int columnY = yBase + ly;
        for (int lz = 0; lz < SubChunk::SIZE; ++lz) {
            const auto idx = toBorderXZIndex(ly, lz);
            snapshot.posXBorder[idx] = neighborPosX
                ? neighborPosX->getBlock(0, columnY, lz)
                : NULL_BLOCK_STATE;
            snapshot.negXBorder[idx] = neighborNegX
                ? neighborNegX->getBlock(Chunk::SIZE_X - 1, columnY, lz)
                : NULL_BLOCK_STATE;
            snapshot.posXLightBorder[idx] = neighborPosX
                ? neighborPosX->getPackedLight(0, columnY, lz) : 0;
            snapshot.negXLightBorder[idx] = neighborNegX
                ? neighborNegX->getPackedLight(Chunk::SIZE_X - 1, columnY, lz) : 0;
        }
        for (int lx = 0; lx < SubChunk::SIZE; ++lx) {
            const auto idx = toBorderXZIndex(ly, lx);
            snapshot.posZBorder[idx] = neighborPosZ
                ? neighborPosZ->getBlock(lx, columnY, 0)
                : NULL_BLOCK_STATE;
            snapshot.negZBorder[idx] = neighborNegZ
                ? neighborNegZ->getBlock(lx, columnY, Chunk::SIZE_Z - 1)
                : NULL_BLOCK_STATE;
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
        vertices.push_back(makeBlockVertex(
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
            renderData.animated,
            renderData.tintKind,
            renderData.tintU,
            renderData.tintV,
            renderData.derivativeMaterialId
        ));
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

glm::vec3 rotatePointX90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {p.x, 1.0f - p.z, p.y};
        case 2: return {p.x, 1.0f - p.y, 1.0f - p.z};
        case 3: return {p.x, p.z, 1.0f - p.y};
        case 0:
        default: return p;
    }
}

glm::vec3 rotatePointY90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.z, p.y, p.x};
        case 2: return {1.0f - p.x, p.y, 1.0f - p.z};
        case 3: return {p.z, p.y, 1.0f - p.x};
        case 0:
        default: return p;
    }
}

glm::vec3 rotatePointZ90(const glm::vec3& p, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {1.0f - p.y, p.x, p.z};
        case 2: return {1.0f - p.x, 1.0f - p.y, p.z};
        case 3: return {p.y, 1.0f - p.x, p.z};
        case 0:
        default: return p;
    }
}

glm::vec3 applyModelTransform(glm::vec3 p, const ModelTransform& transform) {
    p = rotatePointX90(p, transform.rotX);
    p = rotatePointY90(p, transform.rotY);
    p = rotatePointZ90(p, transform.rotZ);
    return p;
}

IVec3 rotateDirectionX90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {direction.x, -direction.z, direction.y};
        case 2: return {direction.x, -direction.y, -direction.z};
        case 3: return {direction.x, direction.z, -direction.y};
        case 0:
        default: return direction;
    }
}

IVec3 rotateDirectionY90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.z, direction.y, direction.x};
        case 2: return {-direction.x, direction.y, -direction.z};
        case 3: return {direction.z, direction.y, -direction.x};
        case 0:
        default: return direction;
    }
}

IVec3 rotateDirectionZ90(const IVec3 direction, const uint16_t rotation) {
    switch ((rotation / 90u) % 4u) {
        case 1: return {-direction.y, direction.x, direction.z};
        case 2: return {-direction.x, -direction.y, direction.z};
        case 3: return {direction.y, -direction.x, direction.z};
        case 0:
        default: return direction;
    }
}

IVec3 applyModelTransformToDirection(IVec3 direction, const ModelTransform& transform) {
    direction = rotateDirectionX90(direction, transform.rotX);
    direction = rotateDirectionY90(direction, transform.rotY);
    direction = rotateDirectionZ90(direction, transform.rotZ);
    return direction;
}

int faceFromDirection(const IVec3 direction) {
    if (direction.x == 0 && direction.y == 1 && direction.z == 0) return FACE_TOP;
    if (direction.x == 0 && direction.y == -1 && direction.z == 0) return FACE_BOTTOM;
    if (direction.x == 0 && direction.y == 0 && direction.z == 1) return FACE_FRONT;
    if (direction.x == 0 && direction.y == 0 && direction.z == -1) return FACE_BACK;
    if (direction.x == -1 && direction.y == 0 && direction.z == 0) return FACE_LEFT;
    if (direction.x == 1 && direction.y == 0 && direction.z == 0) return FACE_RIGHT;
    throw std::runtime_error("Model transform produced an invalid face direction");
}

int transformFaceIndex(const int face, const ModelTransform& transform) {
    return faceFromDirection(applyModelTransformToDirection(kFaceNormals[static_cast<size_t>(face)], transform));
}

uint8_t transformCullfaceBits(const uint8_t bits, const ModelTransform& transform) {
    uint8_t transformed = 0;
    for (int face = 0; face < 6; ++face) {
        if ((bits & static_cast<uint8_t>(1u << static_cast<uint8_t>(face))) == 0) {
            continue;
        }
        const int transformedFace = transformFaceIndex(face, transform);
        transformed |= static_cast<uint8_t>(1u << static_cast<uint8_t>(transformedFace));
    }
    return transformed;
}

bool shouldCullModelFace(const uint8_t transformedCullfaceBits,
                         const SubChunkMeshingSnapshot& snapshot,
                         const int x,
                         const int y,
                         const int z) {
    for (int face = 0; face < 6; ++face) {
        if ((transformedCullfaceBits & static_cast<uint8_t>(1u << static_cast<uint8_t>(face))) == 0) {
            continue;
        }
        const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
        const BlockStateId neighborState = getResolvedBlockSC(snapshot, x + normal.x, y + normal.y, z + normal.z);
        const MeshBlockInfo& neighborInfo = getMeshBlockInfo(neighborState);
        if (neighborInfo.cubeClass == MeshCubeClass::Opaque && neighborInfo.isSolid) {
            return true;
        }
    }
    return false;
}

std::array<glm::vec3, 4> buildModelFaceCorners(const ModelElement& element, const int face) {
    const float x0 = element.from[0] / 16.0f;
    const float y0 = element.from[1] / 16.0f;
    const float z0 = element.from[2] / 16.0f;
    const float x1 = element.to[0] / 16.0f;
    const float y1 = element.to[1] / 16.0f;
    const float z1 = element.to[2] / 16.0f;

    switch (face) {
        case FACE_TOP:
            return {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}};
        case FACE_BOTTOM:
            return {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}};
        case FACE_FRONT:
            return {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}};
        case FACE_BACK:
            return {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}};
        case FACE_LEFT:
            return {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}};
        case FACE_RIGHT:
        default:
            return {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}};
    }
}

std::array<glm::vec2, 4> buildModelFaceUv(const ModelFace& face) {
    const float x0 = face.uv[0] / 16.0f;
    const float y0 = face.uv[1] / 16.0f;
    const float x1 = face.uv[2] / 16.0f;
    const float y1 = face.uv[3] / 16.0f;

    switch ((face.uvRotation / 90u) % 4u) {
        case 1:
            return {{{x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}};
        case 2:
            return {{{x1, y1}, {x0, y1}, {x0, y0}, {x1, y0}}};
        case 3:
            return {{{x0, y1}, {x0, y0}, {x1, y0}, {x1, y1}}};
        case 0:
        default:
            return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
    }
}

std::array<glm::vec2, 4> applyHorizontalUvLock(std::array<glm::vec2, 4> uv,
                                               const int face,
                                               const ModelTransform& transform) {
    if (!transform.uvLock || (face != FACE_TOP && face != FACE_BOTTOM)) {
        return uv;
    }

    const uint16_t turns = static_cast<uint16_t>((transform.rotY / 90u) % 4u);
    if (turns == 0) {
        return uv;
    }

    std::array<glm::vec2, 4> locked{};
    for (size_t i = 0; i < uv.size(); ++i) {
        locked[i] = uv[(i + uv.size() - turns) % uv.size()];
    }
    return locked;
}

std::string resolveModelFaceTextureName(const BlockModel& model, const ModelFace& face) {
    const std::string textureKey = face.textureVar.substr(1);
    const auto it = model.textures.find(textureKey);
    if (it == model.textures.end()) {
        throw std::runtime_error("Model face references unknown texture variable: " + model.name + "." + textureKey);
    }
    return it->second;
}

std::shared_ptr<const CachedModelGeometry> buildCachedModelGeometry(const ModelVariant& variant) {
    const BlockModel& model = *variant.model;
    auto geometry = std::make_shared<CachedModelGeometry>();

    size_t faceCount = 0;
    for (const ModelElement& element : model.elements) {
        for (const std::unique_ptr<ModelFace>& face : element.faces) {
            if (face) {
                ++faceCount;
            }
        }
    }
    geometry->faces.reserve(faceCount);

    for (const ModelElement& element : model.elements) {
        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
            const std::unique_ptr<ModelFace>& facePtr = element.faces[static_cast<size_t>(faceIndex)];
            if (!facePtr) {
                continue;
            }

            const ModelFace& face = *facePtr;
            CachedModelFace cached;
            cached.localCorners = buildModelFaceCorners(element, faceIndex);
            for (glm::vec3& corner : cached.localCorners) {
                corner = applyModelTransform(corner, variant.transform);
            }
            cached.uv = applyHorizontalUvLock(buildModelFaceUv(face), faceIndex, variant.transform);
            cached.textureName = resolveModelFaceTextureName(model, face);
            cached.transformedFace = transformFaceIndex(faceIndex, variant.transform);
            cached.cullfaceBits = transformCullfaceBits(face.cullfaceBits, variant.transform);
            cached.tintIndex = face.tintIndex;
            cached.ambientOcclusion = model.ambientOcclusion;
            geometry->faces.push_back(std::move(cached));
        }
    }

    return geometry;
}

const CachedModelGeometry& getCachedModelGeometry(const ModelVariant& variant) {
    if (variant.model == nullptr) {
        throw std::runtime_error("Model geometry cache requires a model");
    }

    const ModelGeometryCacheKey key{
        variant.model,
        variant.transform.rotX,
        variant.transform.rotY,
        variant.transform.rotZ,
        variant.transform.uvLock,
    };

    {
        std::lock_guard<std::mutex> lock(g_modelGeometryCacheMutex);
        const auto it = g_modelGeometryCache.find(key);
        if (it != g_modelGeometryCache.end()) {
            return *it->second;
        }
    }

    std::shared_ptr<const CachedModelGeometry> built = buildCachedModelGeometry(variant);

    std::lock_guard<std::mutex> lock(g_modelGeometryCacheMutex);
    auto [it, inserted] = g_modelGeometryCache.emplace(key, built);
    (void)inserted;
    return *it->second;
}

std::array<VertexLightData, 4> computeModelFaceVertexData(const SubChunkMeshingSnapshot& snapshot,
                                                          const int x,
                                                          const int y,
                                                          const int z,
                                                          const int face,
                                                          const std::array<glm::vec3, 4>& localCorners,
                                                          const bool ambientOcclusion) {
    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    const int bx = x + normal.x;
    const int by = y + normal.y;
    const int bz = z + normal.z;

    int axis0 = 0;
    int axis1 = 0;
    if (normal.y != 0) {
        axis0 = 0;
        axis1 = 2;
    } else if (normal.z != 0) {
        axis0 = 0;
        axis1 = 1;
    } else {
        axis0 = 2;
        axis1 = 1;
    }

    std::array<VertexLightData, 4> data{};
    const uint8_t basePacked = getResolvedLightSC(snapshot, bx, by, bz);
    const uint8_t baseSun = static_cast<uint8_t>((basePacked >> 4) & 0x0F);
    const uint8_t baseBlock = static_cast<uint8_t>(basePacked & 0x0F);

    for (size_t i = 0; i < data.size(); ++i) {
        const glm::vec3& corner = localCorners[i];
        const int d0 = (corner[static_cast<size_t>(axis0)] > 0.5f) ? 1 : -1;
        const int d1 = (corner[static_cast<size_t>(axis1)] > 0.5f) ? 1 : -1;

        int s1[3] = {bx, by, bz};
        int s2[3] = {bx, by, bz};
        int cn[3] = {bx, by, bz};
        s1[axis0] += d0;
        s2[axis1] += d1;
        cn[axis0] += d0;
        cn[axis1] += d1;

        const bool side1 = isSolidForAO(snapshot, s1[0], s1[1], s1[2]);
        const bool side2 = isSolidForAO(snapshot, s2[0], s2[1], s2[2]);
        const bool cornerSolid = isSolidForAO(snapshot, cn[0], cn[1], cn[2]);

        data[i].ao = ambientOcclusion ? computeVertexAO(side1, side2, cornerSolid) : 3;

        const uint8_t s1Packed = getSafePackedLightForAO(snapshot, s1[0], s1[1], s1[2], side1, basePacked);
        const uint8_t s2Packed = getSafePackedLightForAO(snapshot, s2[0], s2[1], s2[2], side2, basePacked);
        const uint8_t cnPacked = getSafePackedLightForAO(snapshot, cn[0], cn[1], cn[2], cornerSolid, basePacked);

        const uint8_t s1Sun = static_cast<uint8_t>((s1Packed >> 4) & 0x0F);
        const uint8_t s2Sun = static_cast<uint8_t>((s2Packed >> 4) & 0x0F);
        const uint8_t cnSun = static_cast<uint8_t>((cnPacked >> 4) & 0x0F);
        const uint8_t s1Block = static_cast<uint8_t>(s1Packed & 0x0F);
        const uint8_t s2Block = static_cast<uint8_t>(s2Packed & 0x0F);
        const uint8_t cnBlock = static_cast<uint8_t>(cnPacked & 0x0F);

        data[i].sunNormalized = computeVertexNormalized(baseSun, s1Sun, s2Sun, cnSun, side1, side2);
        data[i].blockNormalized = computeVertexNormalized(baseBlock, s1Block, s2Block, cnBlock, side1, side2);
        data[i].sunKey = computeVertexLightKey(baseSun, s1Sun, s2Sun, cnSun, side1, side2);
        data[i].blockKey = computeVertexLightKey(baseBlock, s1Block, s2Block, cnBlock, side1, side2);
    }
    return data;
}

std::vector<BlockVertex>& selectModelVertexTarget(ChunkMeshData& meshData, const BlockDef& def) {
    switch (def.renderLayer) {
        case BlockRenderLayer::Opaque:
            return meshData.opaqueVertices;
        case BlockRenderLayer::Cutout:
            return def.cutoutDistanceCull ? meshData.cutoutDistanceVertices : meshData.cutoutVertices;
        case BlockRenderLayer::Transparent:
            return meshData.transparentVertices;
    }
    throw std::runtime_error("Unknown render layer for model block");
}

FaceRenderData buildCachedModelFaceRenderData(const SubChunkMeshingSnapshot& snapshot,
                                              const BlockDef& def,
                                              const CachedModelFace& face,
                                              const int x,
                                              const int y,
                                              const int z) {
    const AnimatedTextureRef textureRef = BlockModelRegistry::resolveTextureRef(face.textureName);

    FaceRenderData renderData;
    renderData.tileIndex = std::max(0, textureRef.firstLayer);
    renderData.layer = static_cast<float>(textureRef.firstLayer);
    renderData.animationFrameCount = static_cast<float>(std::max<uint16_t>(1, textureRef.frameCount));
    renderData.animationFps = textureRef.isAnimated ? textureRef.fps : 0.0f;
    renderData.animated = textureRef.isAnimated ? 1.0f : 0.0f;
    renderData.derivativeMaterialId = def.derivativeMaterialId;
    renderData.tintKind = face.tintIndex >= 0 ? blockTintKindFromBiomeTint(def.biomeTint) : BlockTintKinds::NONE;
    if (renderData.tintKind != BlockTintKinds::NONE) {
        computeTintMapPosition(snapshot, x, z, renderData.tintU, renderData.tintV);
    }
    renderData.vertices = computeModelFaceVertexData(
        snapshot,
        x,
        y,
        z,
        face.transformedFace,
        face.localCorners,
        face.ambientOcclusion);

    int metric02 = 0;
    int metric13 = 0;
    for (const int index : {0, 2}) {
        metric02 += renderData.vertices[static_cast<size_t>(index)].ao;
        metric02 += renderData.vertices[static_cast<size_t>(index)].sunKey;
        metric02 += renderData.vertices[static_cast<size_t>(index)].blockKey;
    }
    for (const int index : {1, 3}) {
        metric13 += renderData.vertices[static_cast<size_t>(index)].ao;
        metric13 += renderData.vertices[static_cast<size_t>(index)].sunKey;
        metric13 += renderData.vertices[static_cast<size_t>(index)].blockKey;
    }
    renderData.flipDiagonal = metric02 < metric13;
    return renderData;
}

bool shouldRenderWaterFace(const SubChunkMeshingSnapshot& snapshot,
                           const int nx,
                           const int ny,
                           const int nz,
                           const BlockStateId currentState) {
    const BlockStateId neighborState = getResolvedBlockSC(snapshot, nx, ny, nz);
    const DecodedFluid currentFluid = FluidState::decode(currentState);
    if (currentFluid.kind != FluidKind::None &&
        FluidState::decode(neighborState).kind == currentFluid.kind) {
        return false;
    }
    // Check fluid layer for waterlogged neighbors
    const BlockStateId neighborFluidState = getResolvedFluidSC(snapshot, nx, ny, nz);
    if (currentFluid.kind != FluidKind::None &&
        FluidState::decode(neighborFluidState).kind == currentFluid.kind) {
        return false;
    }
    if (neighborState == NULL_BLOCK_STATE && neighborFluidState == NULL_BLOCK_STATE) {
        return true;
    }

    const BlockDef& neighborDef = *getMeshBlockInfo(neighborState).def;
    if (!neighborDef.isSolid) {
        return true;
    }

    return neighborDef.isTransparent;
}

float sampleWaterColumnSurfaceHeight(const SubChunkMeshingSnapshot& snapshot,
                                     const int x,
                                     const int y,
                                     const int z) {
    const BlockStateId aboveState = getResolvedFluidSC(snapshot, x, y + 1, z);
    const BlockStateId stateId = getResolvedFluidSC(snapshot, x, y, z);
    const DecodedFluid fluid = FluidState::decode(stateId);
    if (fluid.kind == FluidKind::None) {
        return 0.0f;
    }
    if (FluidState::decode(aboveState).kind == fluid.kind) {
        return 1.0f;
    }
    return FluidState::surfaceHeight(stateId);
}

bool isOpenWaterSurfaceSample(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return true;
    }

    if (FluidState::isWater(stateId)) {
        return false;
    }

    return !getMeshBlockInfo(stateId).isSolid;
}

float computeWaterCornerHeight(const SubChunkMeshingSnapshot& snapshot,
                               const BlockStateId currentState,
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
        const BlockStateId aboveState = getResolvedFluidSC(snapshot, sample.x, y + 1, sample.y);
        if (FluidState::isWater(aboveState)) {
            return 1.0f;
        }

        const BlockStateId sampleState = getResolvedFluidSC(snapshot, sample.x, y, sample.y);
        if (FluidState::isWater(sampleState)) {
            const float liquidPercent = static_cast<float>(FluidState::level(sampleState) + 1) / 9.0f;
            const int weight = (FluidState::level(sampleState) == 0) ? 10 : 1;
            liquidPercentSum += liquidPercent * static_cast<float>(weight);
            weightSum += weight;

            liquidPercentSum += liquidPercent;
            ++weightSum;
            continue;
        }

        const BlockStateId sampleBlockState = getResolvedBlockSC(snapshot, sample.x, y, sample.y);
        if (isOpenWaterSurfaceSample(sampleBlockState)) {
            liquidPercentSum += 1.0f;
            ++weightSum;
        }
    }

    if (weightSum == 0) {
        return sampleWaterColumnSurfaceHeight(snapshot, x1, y, z1);
    }
    static_cast<void>(currentState);
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

void applyTextureRef(FaceRenderData& renderData, const AnimatedTextureRef& texture) {
    renderData.tileIndex = std::max(0, texture.firstLayer);
    renderData.layer = static_cast<float>(texture.firstLayer);
    renderData.animationFrameCount = static_cast<float>(std::max<uint16_t>(1, texture.frameCount));
    renderData.animationFps = texture.isAnimated ? texture.fps : 0.0f;
    renderData.animated = texture.isAnimated ? 1.0f : 0.0f;
}

void addWaterFacesImpl(ChunkMeshData& meshData,
                       const SubChunkMeshingSnapshot& snapshot,
                       const BlockStateId stateId,
                       const BlockDef& def,
                       const int x,
                       const int y,
                       const int z,
                       const bool skipTopFace = false) {
    const float frontLeft = computeWaterCornerHeight(snapshot, stateId, x - 1, y, z, x, z, x - 1, z + 1, x, z + 1);
    const float frontRight = computeWaterCornerHeight(snapshot, stateId, x, y, z, x + 1, z, x, z + 1, x + 1, z + 1);
    const float backRight = computeWaterCornerHeight(snapshot, stateId, x, y, z - 1, x + 1, z - 1, x, z, x + 1, z);
    const float backLeft = computeWaterCornerHeight(snapshot, stateId, x - 1, y, z - 1, x, z - 1, x - 1, z, x, z);

    const glm::vec3 flow = computeFluidFlowVector(snapshot, x, y, z, FluidKind::Water);
    const bool flowing = isFlowingWaterVector(flow);
    const uint8_t flowQuarterTurns = computeWaterTopQuarterTurns(flow);
    const AnimatedTextureRef* waterTexture = findNamedWaterTexture(def, flowing ? "flow" : "still");

    const glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    const MeshBlockInfo& info = getMeshBlockInfo(stateId);
    const auto emitWaterFace = [&](const int face, const std::array<glm::vec3, 4>& corners) {
        FaceRenderData renderData = buildFaceRenderData(snapshot, stateId, def, info, x, y, z, face);
        if (waterTexture != nullptr) {
            applyWaterTextureRef(renderData, *waterTexture);
        }
        if (face == FACE_TOP) {
            renderData.uvQuarterTurns = flowing ? flowQuarterTurns : 0;
        } else if (flow.y < -0.001f && (face == FACE_BACK || face == FACE_RIGHT)) {
            renderData.uvQuarterTurns = 2;
        }
        emitCustomFace(meshData.waterVertices, corners, face, renderData);
        expandBoundsForCorners(meshData, corners);
        ++meshData.transparentFaceCountBeforeGreedy;
        ++meshData.transparentFaceCountAfterGreedy;
    };

    if (!skipTopFace && shouldRenderWaterFace(snapshot, x, y + 1, z, stateId)) {
        emitWaterFace(FACE_TOP, {{
            pos + glm::vec3(0.0f, frontLeft, 1.0f),
            pos + glm::vec3(1.0f, frontRight, 1.0f),
            pos + glm::vec3(1.0f, backRight, 0.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y - 1, z, stateId)) {
        emitWaterFace(FACE_BOTTOM, {{
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(1.0f, 0.0f, 0.0f),
            pos + glm::vec3(1.0f, 0.0f, 1.0f),
            pos + glm::vec3(0.0f, 0.0f, 1.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y, z + 1, stateId) &&
        (frontLeft > 0.0f || frontRight > 0.0f)) {
        emitWaterFace(FACE_FRONT, {{
            pos + glm::vec3(0.0f, 0.0f, 1.0f),
            pos + glm::vec3(1.0f, 0.0f, 1.0f),
            pos + glm::vec3(1.0f, frontRight, 1.0f),
            pos + glm::vec3(0.0f, frontLeft, 1.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x, y, z - 1, stateId) &&
        (backLeft > 0.0f || backRight > 0.0f)) {
        emitWaterFace(FACE_BACK, {{
            pos + glm::vec3(1.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f),
            pos + glm::vec3(1.0f, backRight, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x - 1, y, z, stateId) &&
        (frontLeft > 0.0f || backLeft > 0.0f)) {
        emitWaterFace(FACE_LEFT, {{
            pos + glm::vec3(0.0f, 0.0f, 0.0f),
            pos + glm::vec3(0.0f, 0.0f, 1.0f),
            pos + glm::vec3(0.0f, frontLeft, 1.0f),
            pos + glm::vec3(0.0f, backLeft, 0.0f)
        }});
    }

    if (shouldRenderWaterFace(snapshot, x + 1, y, z, stateId) &&
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
    return def.renderShape == BlockRenderShape::Cube && def.renderLayer == BlockRenderLayer::Opaque;
}

bool isCutoutCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && def.renderLayer == BlockRenderLayer::Cutout;
}

bool isTransparentCubeCandidate(const BlockDef& def) {
    return def.renderShape == BlockRenderShape::Cube && def.renderLayer == BlockRenderLayer::Transparent;
}

std::vector<BlockVertex>& cutoutTargetFor(ChunkMeshData& meshData, const BlockDef& def) {
    return def.cutoutDistanceCull ? meshData.cutoutDistanceVertices : meshData.cutoutVertices;
}

bool populateTransparentFaceCellForTarget(const SubChunkMeshingSnapshot& snapshot,
                                          const int face,
                                          const int x,
                                          const int y,
                                          const int z,
                                          const bool waterTarget,
                                          FaceCell& outCell) {
    const BlockStateId stateId = snapshot.blocks[scToIndex(x, y, z)];
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const MeshBlockInfo& info = getMeshBlockInfo(stateId);
    const MeshCubeClass expectedClass = waterTarget ? MeshCubeClass::Water : MeshCubeClass::Transparent;
    if (info.cubeClass != expectedClass) {
        return false;
    }
    const BlockDef& def = *info.def;

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, stateId, def)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, stateId, def, info, x, y, z, face);
    outCell.key = buildFaceMergeKey(stateId, outCell.renderData);
    return true;
}

bool populateTransparentFaceCell(const SubChunkMeshingSnapshot& snapshot,
                                 const int face,
                                 const int x,
                                 const int y,
                                 const int z,
                                 FaceCell& outCell) {
    return populateTransparentFaceCellForTarget(snapshot, face, x, y, z, false, outCell);
}

bool populateWaterMaterialFaceCell(const SubChunkMeshingSnapshot& snapshot,
                                   const int face,
                                   const int x,
                                   const int y,
                                   const int z,
                                   FaceCell& outCell) {
    return populateTransparentFaceCellForTarget(snapshot, face, x, y, z, true, outCell);
}

bool populateCutoutFaceCell(const SubChunkMeshingSnapshot& snapshot,
                            const int face,
                            const int x,
                            const int y,
                            const int z,
                            FaceCell& outCell) {
    const BlockStateId stateId = snapshot.blocks[scToIndex(x, y, z)];
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const MeshBlockInfo& info = getMeshBlockInfo(stateId);
    if (info.cubeClass != MeshCubeClass::Cutout) {
        return false;
    }
    const BlockDef& def = *info.def;

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, stateId, def)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, stateId, def, info, x, y, z, face);
    outCell.key = buildFaceMergeKey(stateId, outCell.renderData);
    return true;
}

bool populateCutoutDistanceFaceCell(const SubChunkMeshingSnapshot& snapshot,
                                    const int face,
                                    const int x,
                                    const int y,
                                    const int z,
                                    FaceCell& outCell) {
    const BlockStateId stateId = snapshot.blocks[scToIndex(x, y, z)];
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const MeshBlockInfo& info = getMeshBlockInfo(stateId);
    if (info.cubeClass != MeshCubeClass::CutoutDistance) {
        return false;
    }
    const BlockDef& def = *info.def;

    const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
    if (!shouldRenderFaceImpl(snapshot, x + normal.x, y + normal.y, z + normal.z, stateId, def)) {
        return false;
    }

    outCell.valid = true;
    outCell.x = x;
    outCell.y = y;
    outCell.z = z;
    outCell.renderData = buildFaceRenderData(snapshot, stateId, def, info, x, y, z, face);
    outCell.key = buildFaceMergeKey(stateId, outCell.renderData);
    return true;
}

void emitGreedyPlaneRuns(ChunkMeshData& meshData,
                         std::vector<BlockVertex>& targetVertices,
                         const int face,
                         const int width,
                         const int height,
                         const std::array<FaceCell, kMaxGreedyPlaneSize>& plane,
                         std::array<uint8_t, kMaxGreedyPlaneSize>& consumed,
                         uint32_t& faceCountAfterGreedy) {
    const bool debugDisableGreedy = g_debugDisableGreedyMeshing.load(std::memory_order_relaxed);

    for (int v = 0; v < height; ++v) {
        for (int u = 0; u < width; ++u) {
            const size_t startIndex = static_cast<size_t>(u) + static_cast<size_t>(v) * static_cast<size_t>(width);
            if (consumed[startIndex] != 0 || !plane[startIndex].valid) {
                continue;
            }

            const FaceCell& startCell = plane[startIndex];

            int runWidth = 1;
            while (u + runWidth < width) {
                const size_t nextIndex = static_cast<size_t>(u + runWidth) + static_cast<size_t>(v) * static_cast<size_t>(width);
                if (consumed[nextIndex] != 0 || !samePlaneFaceCell(startCell, plane[nextIndex])) {
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
                    if (consumed[candidateIndex] != 0 || !samePlaneFaceCell(startCell, plane[candidateIndex])) {
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
                             static_cast<size_t>(v + dy) * static_cast<size_t>(width)] = 1;
                }
            }

            if (debugDisableGreedy) {
                for (int dy = 0; dy < runHeight; ++dy) {
                    for (int dx = 0; dx < runWidth; ++dx) {
                        const size_t cellIndex = static_cast<size_t>(u + dx) +
                                                 static_cast<size_t>(v + dy) * static_cast<size_t>(width);
                        const FaceCell& unitCell = plane[cellIndex];
                        const glm::vec3 unitPos(unitCell.x, unitCell.y, unitCell.z);
                        emitUnitFace(targetVertices,
                                     unitPos,
                                     face,
                                     unitCell.renderData);
                        expandBounds(meshData, unitPos, unitPos + glm::vec3(1.0f));
                        ++faceCountAfterGreedy;
                    }
                }
            } else {
                emitGreedyFace(targetVertices, meshData, startCell, face, runWidth, runHeight);
                ++faceCountAfterGreedy;
            }
        }
    }
}

template <typename PopulateCellFn>
void buildCubeGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                          ChunkMeshData& meshData,
                          std::vector<BlockVertex>& targetVertices,
                          uint32_t& faceCountBeforeGreedy,
                          uint32_t& faceCountAfterGreedy,
                          PopulateCellFn&& populateCell) {
    std::array<FaceCell, kMaxGreedyPlaneSize> plane{};
    std::array<uint8_t, kMaxGreedyPlaneSize> consumed{};

    auto buildPlane = [&](const int face, const int width, const int height, const int slices, auto&& mapper) {
        const size_t planeSize = static_cast<size_t>(width) * static_cast<size_t>(height);

        for (int slice = 0; slice < slices; ++slice) {
            for (size_t i = 0; i < planeSize; ++i) {
                plane[i].valid = false;
                consumed[i] = 0;
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

            emitGreedyPlaneRuns(meshData,
                                targetVertices,
                                face,
                                width,
                                height,
                                plane,
                                consumed,
                                faceCountAfterGreedy);
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

template <int Face, int NormalX, int NormalY, int NormalZ>
void buildOpaqueGreedyPlane(const SubChunkMeshingSnapshot& snapshot,
                            ChunkMeshData& meshData,
                            std::array<FaceCell, kMaxGreedyPlaneSize>& plane,
                            std::array<uint8_t, kMaxGreedyPlaneSize>& consumed) {
    constexpr int S = SubChunk::SIZE;
    constexpr size_t planeSize = static_cast<size_t>(S) * S;

    for (int slice = 0; slice < S; ++slice) {
        for (size_t i = 0; i < planeSize; ++i) {
            plane[i].valid = false;
            consumed[i] = 0;
        }

        for (int v = 0; v < S; ++v) {
            for (int u = 0; u < S; ++u) {
                int x = 0;
                int y = 0;
                int z = 0;
                if constexpr (Face == FACE_TOP || Face == FACE_BOTTOM) {
                    x = u;
                    y = slice;
                    z = v;
                } else if constexpr (Face == FACE_FRONT || Face == FACE_BACK) {
                    x = u;
                    y = v;
                    z = slice;
                } else {
                    x = slice;
                    y = v;
                    z = u;
                }

                const BlockStateId stateId = snapshot.blocks[scToIndex(x, y, z)];
                if (stateId == NULL_BLOCK_STATE) {
                    continue;
                }

                const MeshBlockInfo& info = getMeshBlockInfo(stateId);
                if (info.cubeClass != MeshCubeClass::Opaque) {
                    continue;
                }

                if (!shouldRenderOpaqueCubeFace(snapshot, x + NormalX, y + NormalY, z + NormalZ)) {
                    continue;
                }

                FaceCell& cell = plane[static_cast<size_t>(u) + static_cast<size_t>(v) * S];
                cell.valid = true;
                cell.x = x;
                cell.y = y;
                cell.z = z;
                cell.renderData = buildFaceRenderData(snapshot, stateId, *info.def, info, x, y, z, Face);
                cell.key = buildFaceMergeKey(stateId, cell.renderData);
                ++meshData.opaqueFaceCountBeforeGreedy;
            }
        }

        emitGreedyPlaneRuns(meshData,
                            meshData.opaqueVertices,
                            Face,
                            S,
                            S,
                            plane,
                            consumed,
                            meshData.opaqueFaceCountAfterGreedy);
    }
}

void buildOpaqueGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                            ChunkMeshData& meshData,
                            const SubChunkMeshClassPresence& presence) {
    if (!presence.hasOpaqueCube) {
        return;
    }

    std::array<FaceCell, kMaxGreedyPlaneSize> plane{};
    std::array<uint8_t, kMaxGreedyPlaneSize> consumed{};

    buildOpaqueGreedyPlane<FACE_TOP, 0, 1, 0>(snapshot, meshData, plane, consumed);
    buildOpaqueGreedyPlane<FACE_BOTTOM, 0, -1, 0>(snapshot, meshData, plane, consumed);
    buildOpaqueGreedyPlane<FACE_FRONT, 0, 0, 1>(snapshot, meshData, plane, consumed);
    buildOpaqueGreedyPlane<FACE_BACK, 0, 0, -1>(snapshot, meshData, plane, consumed);
    buildOpaqueGreedyPlane<FACE_LEFT, -1, 0, 0>(snapshot, meshData, plane, consumed);
    buildOpaqueGreedyPlane<FACE_RIGHT, 1, 0, 0>(snapshot, meshData, plane, consumed);
}

void buildTransparentGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                                 ChunkMeshData& meshData,
                                 const SubChunkMeshClassPresence& presence) {
    if (presence.hasTransparentCube) {
        buildCubeGreedyFaces(snapshot,
                             meshData,
                             meshData.transparentVertices,
                             meshData.transparentFaceCountBeforeGreedy,
                             meshData.transparentFaceCountAfterGreedy,
                             populateTransparentFaceCell);
    }

    if (presence.hasWaterCube) {
        buildCubeGreedyFaces(snapshot,
                             meshData,
                             meshData.waterVertices,
                             meshData.transparentFaceCountBeforeGreedy,
                             meshData.transparentFaceCountAfterGreedy,
                             populateWaterMaterialFaceCell);
    }
}

void buildCutoutGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                            ChunkMeshData& meshData,
                            const SubChunkMeshClassPresence& presence) {
    if (presence.hasCutoutCube) {
        buildCubeGreedyFaces(snapshot,
                             meshData,
                             meshData.cutoutVertices,
                             meshData.transparentFaceCountBeforeGreedy,
                             meshData.transparentFaceCountAfterGreedy,
                             populateCutoutFaceCell);
    }

    if (presence.hasCutoutDistanceCube) {
        buildCubeGreedyFaces(snapshot,
                             meshData,
                             meshData.cutoutDistanceVertices,
                             meshData.transparentFaceCountBeforeGreedy,
                             meshData.transparentFaceCountAfterGreedy,
                             populateCutoutDistanceFaceCell);
    }
}

using WaterTopMask = std::array<bool, SC_BLOCK_COUNT>;

struct WaterTopCell {
    bool valid = false;
    int x = 0;
    int y = 0;
    int z = 0;
    float height = 0.0f;
    uint16_t heightKey = 0;
    FaceRenderData renderData{};
    FaceMergeKey key{};
};

bool isMergeableStillWaterTop(const SubChunkMeshingSnapshot& snapshot,
                              const int x,
                              const int y,
                              const int z,
                              BlockStateId& outStateId,
                              const BlockDef*& outDef,
                              float& outHeight,
                              uint16_t& outHeightKey) {
    const BlockStateId stateId = getResolvedFluidSC(snapshot, x, y, z);
    if (!FluidState::isWater(stateId) || !FluidState::isSource(stateId) || FluidState::isFalling(stateId)) {
        return false;
    }
    if (!shouldRenderWaterFace(snapshot, x, y + 1, z, stateId)) {
        return false;
    }
    if (isFlowingWaterVector(computeFluidFlowVector(snapshot, x, y, z, FluidKind::Water))) {
        return false;
    }

    const float frontLeft = computeWaterCornerHeight(snapshot, stateId, x - 1, y, z, x, z, x - 1, z + 1, x, z + 1);
    const float frontRight = computeWaterCornerHeight(snapshot, stateId, x, y, z, x + 1, z, x, z + 1, x + 1, z + 1);
    const float backRight = computeWaterCornerHeight(snapshot, stateId, x, y, z - 1, x + 1, z - 1, x, z, x + 1, z);
    const float backLeft = computeWaterCornerHeight(snapshot, stateId, x - 1, y, z - 1, x, z - 1, x - 1, z, x, z);
    constexpr float kHeightEpsilon = 1.0f / 1024.0f;
    if (std::abs(frontLeft - frontRight) > kHeightEpsilon ||
        std::abs(frontLeft - backRight) > kHeightEpsilon ||
        std::abs(frontLeft - backLeft) > kHeightEpsilon) {
        return false;
    }

    const BlockDef& def = *getMeshBlockInfo(stateId).def;
    outStateId = stateId;
    outDef = &def;
    outHeight = frontLeft;
    outHeightKey = static_cast<uint16_t>(std::clamp(frontLeft, 0.0f, 1.0f) * 1024.0f + 0.5f);
    return true;
}

void buildStillWaterTopGreedyFaces(const SubChunkMeshingSnapshot& snapshot,
                                   ChunkMeshData& meshData,
                                   WaterTopMask& mergedTopFaces) {
    mergedTopFaces.fill(false);

    constexpr int S = SubChunk::SIZE;
    std::array<WaterTopCell, static_cast<size_t>(S) * S> plane{};
    std::array<bool, static_cast<size_t>(S) * S> consumed{};

    for (int y = 0; y < S; ++y) {
        for (WaterTopCell& cell : plane) {
            cell.valid = false;
        }
        consumed.fill(false);

        for (int z = 0; z < S; ++z) {
            for (int x = 0; x < S; ++x) {
                BlockStateId stateId = NULL_BLOCK_STATE;
                const BlockDef* def = nullptr;
                float height = 0.0f;
                uint16_t heightKey = 0;
                if (!isMergeableStillWaterTop(snapshot, x, y, z, stateId, def, height, heightKey)) {
                    continue;
                }

                WaterTopCell& cell = plane[static_cast<size_t>(x) + static_cast<size_t>(z) * S];
                cell.valid = true;
                cell.x = x;
                cell.y = y;
                cell.z = z;
                cell.height = height;
                cell.heightKey = heightKey;
                cell.renderData = buildFaceRenderData(snapshot, stateId, *def, getMeshBlockInfo(stateId), x, y, z, FACE_TOP);
                if (const AnimatedTextureRef* waterTexture = findNamedWaterTexture(*def, "still")) {
                    applyWaterTextureRef(cell.renderData, *waterTexture);
                }
                cell.renderData.uvQuarterTurns = 0;
                cell.key = buildFaceMergeKey(stateId, cell.renderData);
                ++meshData.transparentFaceCountBeforeGreedy;
            }
        }

        for (int z = 0; z < S; ++z) {
            for (int x = 0; x < S; ++x) {
                const size_t startIndex = static_cast<size_t>(x) + static_cast<size_t>(z) * S;
                if (consumed[startIndex] || !plane[startIndex].valid) {
                    continue;
                }

                const uint64_t startHash = plane[startIndex].key.hash;
                int runWidth = 1;
                while (x + runWidth < S) {
                    const size_t nextIndex = static_cast<size_t>(x + runWidth) + static_cast<size_t>(z) * S;
                    if (consumed[nextIndex] || !plane[nextIndex].valid ||
                        plane[nextIndex].key.hash != startHash ||
                        plane[nextIndex].heightKey != plane[startIndex].heightKey ||
                        !sameMergeKey(plane[startIndex].key, plane[nextIndex].key)) {
                        break;
                    }
                    ++runWidth;
                }

                int runHeight = 1;
                bool canGrow = true;
                while (z + runHeight < S && canGrow) {
                    for (int dx = 0; dx < runWidth; ++dx) {
                        const size_t candidateIndex = static_cast<size_t>(x + dx) +
                                                      static_cast<size_t>(z + runHeight) * S;
                        if (consumed[candidateIndex] || !plane[candidateIndex].valid ||
                            plane[candidateIndex].key.hash != startHash ||
                            plane[candidateIndex].heightKey != plane[startIndex].heightKey ||
                            !sameMergeKey(plane[startIndex].key, plane[candidateIndex].key)) {
                            canGrow = false;
                            break;
                        }
                    }
                    if (canGrow) {
                        ++runHeight;
                    }
                }

                for (int dz = 0; dz < runHeight; ++dz) {
                    for (int dx = 0; dx < runWidth; ++dx) {
                        const size_t index = static_cast<size_t>(x + dx) + static_cast<size_t>(z + dz) * S;
                        consumed[index] = true;
                        mergedTopFaces[scToIndex(x + dx, y, z + dz)] = true;
                    }
                }

                const WaterTopCell& start = plane[startIndex];
                const float x0 = static_cast<float>(start.x);
                const float x1 = static_cast<float>(start.x + runWidth);
                const float z0 = static_cast<float>(start.z);
                const float z1 = static_cast<float>(start.z + runHeight);
                const float topY = static_cast<float>(start.y) + start.height;
                const std::array<glm::vec3, 4> corners = {{
                    {x0, topY, z1},
                    {x1, topY, z1},
                    {x1, topY, z0},
                    {x0, topY, z0}
                }};
                const std::array<glm::vec2, 4> faceUV = buildFaceUv(
                    static_cast<float>(runWidth),
                    static_cast<float>(runHeight),
                    start.renderData.uvQuarterTurns);
                appendFaceVertices(meshData.waterVertices, corners, faceUV, FACE_TOP, start.renderData);
                expandBoundsForCorners(meshData, corners);
                ++meshData.transparentFaceCountAfterGreedy;
            }
        }
    }
}

void addCrossedQuadsImpl(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const BlockStateId stateId,
                          const BlockDef& def,
                          const int x,
                          const int y,
                          const int z,
                          const SubChunkMeshingSnapshot& snapshot) {
    static_cast<void>(def);
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(stateId);
    const float layer = static_cast<float>(textures.faceTop.firstLayer);

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
    uint8_t tintU = 0;
    uint8_t tintV = 0;
    const uint8_t tintKind = blockTintKindFromBiomeTint(def.biomeTint);
    if (tintKind != BlockTintKinds::NONE) {
        computeTintMapPosition(snapshot, x, z, tintU, tintV);
    }
    const float crossMarker = tintKind != BlockTintKinds::NONE ? CROSS_BIOME_TINT_MARKER : CROSS_FLOWER_MARKER;

    const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
        for (const int index : indices) {
            vertices.push_back(makeBlockVertex(
                pos.x + corners[static_cast<size_t>(index)].x,
                pos.y + corners[static_cast<size_t>(index)].y,
                pos.z + corners[static_cast<size_t>(index)].z,
                quadUV[static_cast<size_t>(index)].x,
                quadUV[static_cast<size_t>(index)].y,
                crossMarker,
                sunNormalized,
                blockNormalized,
                3.0f,
                layer,
                1.0f,
                0.0f,
                0.0f,
                tintKind,
                tintU,
                tintV,
                def.derivativeMaterialId
            ));
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
                        const BlockStateId stateId,
                        const int x,
                        const int y,
                        const int z,
                        const SubChunkMeshingSnapshot& snapshot) {
    const BlockDef& def = *getMeshBlockInfo(stateId).def;
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(stateId);
    int tileIndex = textures.faceTop.firstLayer;
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
        facingValue = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
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
            vertices.push_back(makeBlockVertex(
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
                0.0f,
                BlockTintKinds::NONE,
                0,
                0,
                def.derivativeMaterialId
            ));
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
                       const BlockStateId stateId,
                       const int x,
                       const int y,
                       const int z,
                       const SubChunkMeshingSnapshot& snapshot) {
    const BlockDef& def = *getMeshBlockInfo(stateId).def;
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(stateId);
    const float layer = static_cast<float>(textures.faceTop.firstLayer);

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
        facingValue = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
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
            vertices.push_back(makeBlockVertex(
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
                0.0f,
                BlockTintKinds::NONE,
                0,
                0,
                def.derivativeMaterialId
            ));
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
                        const uint8_t derivativeMaterialId,
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
        vertices.push_back(makeBlockVertex(
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
            0.0f,
            BlockTintKinds::NONE,
            0,
            0,
            derivativeMaterialId
        ));
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
                               const TorchModelUvRect& rightUv,
                               const uint8_t derivativeMaterialId) {
    if (emitTop) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_TOP, {{
            {from.x, to.y, to.z},
            {to.x, to.y, to.z},
            {to.x, to.y, from.z},
            {from.x, to.y, from.z}
        }}, topUv, derivativeMaterialId, transform);
    }
    if (emitBottom) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_BOTTOM, {{
            {from.x, from.y, from.z},
            {to.x, from.y, from.z},
            {to.x, from.y, to.z},
            {from.x, from.y, to.z}
        }}, bottomUv, derivativeMaterialId, transform);
    }
    if (emitFront) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_FRONT, {{
            {from.x, from.y, to.z},
            {to.x, from.y, to.z},
            {to.x, to.y, to.z},
            {from.x, to.y, to.z}
        }}, frontUv, derivativeMaterialId, transform);
    }
    if (emitBack) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_BACK, {{
            {to.x, from.y, from.z},
            {from.x, from.y, from.z},
            {from.x, to.y, from.z},
            {to.x, to.y, from.z}
        }}, backUv, derivativeMaterialId, transform);
    }
    if (emitLeft) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_LEFT, {{
            {from.x, from.y, from.z},
            {from.x, from.y, to.z},
            {from.x, to.y, to.z},
            {from.x, to.y, from.z}
        }}, leftUv, derivativeMaterialId, transform);
    }
    if (emitRight) {
        emitTorchModelFace(vertices, pos, layer, sunNorm, blockNorm, FACE_RIGHT, {{
            {to.x, from.y, to.z},
            {to.x, from.y, from.z},
            {to.x, to.y, from.z},
            {to.x, to.y, to.z}
        }}, rightUv, derivativeMaterialId, transform);
    }
}

void addTorchTemplateImpl(std::vector<BlockVertex>& vertices,
                          const glm::vec3& pos,
                          const BlockStateId stateId,
                          const int x,
                          const int y,
                          const int z,
                          const SubChunkMeshingSnapshot& snapshot) {
    const BlockDef& def = *getMeshBlockInfo(stateId).def;
    const StateTextureIndices& textures = BlockStateRegistry::getStateTextures(stateId);
    const float layer = static_cast<float>(textures.faceTop.firstLayer);

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
        facingValue = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
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
                                  false, kTorchFullUv,
                                  def.derivativeMaterialId);
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
                                  true, kTorchFullUv,
                                  def.derivativeMaterialId);
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
                                  false, kTorchFullUv,
                                  def.derivativeMaterialId);
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
                              false, kTorchFullUv,
                              def.derivativeMaterialId);
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
                              true, kTorchFullUv,
                              def.derivativeMaterialId);
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
                              false, kTorchFullUv,
                              def.derivativeMaterialId);
}

constexpr float kFacePlaneSurfaceOffset = 1.0f / 128.0f;

uint16_t requireFacePlaneFacing(const BlockStateId stateId) {
    if (PropIndices::FACING == PropIndices::INVALID) {
        throw std::runtime_error("Face plane mesh requires the facing property");
    }
    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (facing == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error("Face plane mesh requires a facing state value");
    }
    return facing;
}

int facePlaneRenderFace(const uint16_t facing) {
    if (facing == PropIndices::FACING_FLOOR) {
        return FACE_TOP;
    }
    if (facing == PropIndices::FACING_CEILING) {
        return FACE_BOTTOM;
    }
    if (facing == PropIndices::FACING_NORTH) {
        return FACE_BACK;
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return FACE_FRONT;
    }
    if (facing == PropIndices::FACING_EAST) {
        return FACE_RIGHT;
    }
    if (facing == PropIndices::FACING_WEST) {
        return FACE_LEFT;
    }
    throw std::runtime_error("Face plane mesh received an unsupported facing value");
}

std::array<glm::vec3, 4> buildFacePlaneCorners(const glm::vec3& pos, const uint16_t facing) {
    const float nearMin = kFacePlaneSurfaceOffset;
    const float nearMax = 1.0f - kFacePlaneSurfaceOffset;

    if (facing == PropIndices::FACING_FLOOR) {
        const float y = nearMin;
        return {{{pos.x + 0.0f, pos.y + y, pos.z + 1.0f},
                 {pos.x + 1.0f, pos.y + y, pos.z + 1.0f},
                 {pos.x + 1.0f, pos.y + y, pos.z + 0.0f},
                 {pos.x + 0.0f, pos.y + y, pos.z + 0.0f}}};
    }
    if (facing == PropIndices::FACING_CEILING) {
        const float y = nearMax;
        return {{{pos.x + 0.0f, pos.y + y, pos.z + 0.0f},
                 {pos.x + 1.0f, pos.y + y, pos.z + 0.0f},
                 {pos.x + 1.0f, pos.y + y, pos.z + 1.0f},
                 {pos.x + 0.0f, pos.y + y, pos.z + 1.0f}}};
    }
    if (facing == PropIndices::FACING_NORTH) {
        const float z = nearMax;
        return {{{pos.x + 1.0f, pos.y + 0.0f, pos.z + z},
                 {pos.x + 0.0f, pos.y + 0.0f, pos.z + z},
                 {pos.x + 0.0f, pos.y + 1.0f, pos.z + z},
                 {pos.x + 1.0f, pos.y + 1.0f, pos.z + z}}};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        const float z = nearMin;
        return {{{pos.x + 0.0f, pos.y + 0.0f, pos.z + z},
                 {pos.x + 1.0f, pos.y + 0.0f, pos.z + z},
                 {pos.x + 1.0f, pos.y + 1.0f, pos.z + z},
                 {pos.x + 0.0f, pos.y + 1.0f, pos.z + z}}};
    }
    if (facing == PropIndices::FACING_EAST) {
        const float x = nearMin;
        return {{{pos.x + x, pos.y + 0.0f, pos.z + 1.0f},
                 {pos.x + x, pos.y + 0.0f, pos.z + 0.0f},
                 {pos.x + x, pos.y + 1.0f, pos.z + 0.0f},
                 {pos.x + x, pos.y + 1.0f, pos.z + 1.0f}}};
    }
    if (facing == PropIndices::FACING_WEST) {
        const float x = nearMax;
        return {{{pos.x + x, pos.y + 0.0f, pos.z + 0.0f},
                 {pos.x + x, pos.y + 0.0f, pos.z + 1.0f},
                 {pos.x + x, pos.y + 1.0f, pos.z + 1.0f},
                 {pos.x + x, pos.y + 1.0f, pos.z + 0.0f}}};
    }
    throw std::runtime_error("Face plane mesh received an unsupported facing value");
}

void requireRedstoneWireMeshProperties() {
    if (PropIndices::FACING == PropIndices::INVALID ||
        PropIndices::POWER == PropIndices::INVALID ||
        PropIndices::NORTH == PropIndices::INVALID ||
        PropIndices::SOUTH == PropIndices::INVALID ||
        PropIndices::EAST == PropIndices::INVALID ||
        PropIndices::WEST == PropIndices::INVALID ||
        PropIndices::FACING_FLOOR == PropIndices::INVALID ||
        PropIndices::FACING_CEILING == PropIndices::INVALID ||
        PropIndices::NORTH_NONE == PropIndices::INVALID ||
        PropIndices::NORTH_SIDE == PropIndices::INVALID ||
        PropIndices::SOUTH_NONE == PropIndices::INVALID ||
        PropIndices::SOUTH_SIDE == PropIndices::INVALID ||
        PropIndices::EAST_NONE == PropIndices::INVALID ||
        PropIndices::EAST_SIDE == PropIndices::INVALID ||
        PropIndices::WEST_NONE == PropIndices::INVALID ||
        PropIndices::WEST_SIDE == PropIndices::INVALID ||
        PropIndices::POWER_0 == PropIndices::INVALID ||
        PropIndices::POWER_1 == PropIndices::INVALID ||
        PropIndices::POWER_2 == PropIndices::INVALID ||
        PropIndices::POWER_3 == PropIndices::INVALID ||
        PropIndices::POWER_4 == PropIndices::INVALID ||
        PropIndices::POWER_5 == PropIndices::INVALID ||
        PropIndices::POWER_6 == PropIndices::INVALID ||
        PropIndices::POWER_7 == PropIndices::INVALID ||
        PropIndices::POWER_8 == PropIndices::INVALID ||
        PropIndices::POWER_9 == PropIndices::INVALID ||
        PropIndices::POWER_10 == PropIndices::INVALID ||
        PropIndices::POWER_11 == PropIndices::INVALID ||
        PropIndices::POWER_12 == PropIndices::INVALID ||
        PropIndices::POWER_13 == PropIndices::INVALID ||
        PropIndices::POWER_14 == PropIndices::INVALID ||
        PropIndices::POWER_15 == PropIndices::INVALID) {
        throw std::runtime_error("Redstone wire mesh requires facing, power, and horizontal connection properties");
    }
}

uint8_t redstonePowerLevel(const BlockStateId stateId) {
    const uint16_t powerValue = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::POWER);
    static const std::array<uint16_t, 16> kPowerValues = {{
        PropIndices::POWER_0,
        PropIndices::POWER_1,
        PropIndices::POWER_2,
        PropIndices::POWER_3,
        PropIndices::POWER_4,
        PropIndices::POWER_5,
        PropIndices::POWER_6,
        PropIndices::POWER_7,
        PropIndices::POWER_8,
        PropIndices::POWER_9,
        PropIndices::POWER_10,
        PropIndices::POWER_11,
        PropIndices::POWER_12,
        PropIndices::POWER_13,
        PropIndices::POWER_14,
        PropIndices::POWER_15
    }};

    for (size_t i = 0; i < kPowerValues.size(); ++i) {
        if (powerValue == kPowerValues[i]) {
            return static_cast<uint8_t>(i);
        }
    }

    throw std::runtime_error("Redstone wire mesh received an unsupported power value");
}

// Returns 0 for "none" and 1 for "side".
// Throws if the property value is not one of the recognized connection states.
uint8_t redstoneWireConnectionLevel(const BlockStateId stateId,
                                     const uint16_t property,
                                     const uint16_t noneValue,
                                     const uint16_t sideValue,
                                     const char* propertyName) {
    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == noneValue) {
        return 0;
    }
    if (value == sideValue) {
        return 1;
    }
    throw std::runtime_error(std::string("Redstone wire mesh received an unsupported ") + propertyName + " value");
}

const AnimatedTextureRef& requireNamedTextureRef(const BlockDef& def, const char* name) {
    const auto it = def.namedTextureRefs.find(name);
    if (it == def.namedTextureRefs.end()) {
        throw std::runtime_error("Redstone wire mesh requires named texture: " + std::string(name));
    }
    return it->second;
}

enum class PlanarWireSegmentDirection : uint8_t {
    NegativeA,
    PositiveA,
    NegativeB,
    PositiveB
};

struct PlanarWireSegmentRect {
    float a0 = 0.0f;
    float b0 = 0.0f;
    float a1 = 1.0f;
    float b1 = 1.0f;
};

PlanarWireSegmentDirection planarWireSegmentDirection(const uint16_t facing,
                                                      const uint16_t property) {
    const bool horizontalFace = facing == PropIndices::FACING_FLOOR ||
                                facing == PropIndices::FACING_CEILING;
    if (property == PropIndices::NORTH) {
        return horizontalFace
            ? PlanarWireSegmentDirection::NegativeB
            : PlanarWireSegmentDirection::PositiveB;
    }
    if (property == PropIndices::SOUTH) {
        return horizontalFace
            ? PlanarWireSegmentDirection::PositiveB
            : PlanarWireSegmentDirection::NegativeB;
    }
    if (property == PropIndices::EAST) {
        return PlanarWireSegmentDirection::PositiveA;
    }
    if (property == PropIndices::WEST) {
        return PlanarWireSegmentDirection::NegativeA;
    }
    throw std::runtime_error("Redstone wire segment received an unsupported connection property");
}

PlanarWireSegmentRect planarWireSegmentRect(const PlanarWireSegmentDirection direction) {
    switch (direction) {
        case PlanarWireSegmentDirection::NegativeA:
            return {0.0f, 0.0f, 0.5f, 1.0f};
        case PlanarWireSegmentDirection::PositiveA:
            return {0.5f, 0.0f, 1.0f, 1.0f};
        case PlanarWireSegmentDirection::NegativeB:
            return {0.0f, 0.0f, 1.0f, 0.5f};
        case PlanarWireSegmentDirection::PositiveB:
            return {0.0f, 0.5f, 1.0f, 1.0f};
    }
    throw std::runtime_error("Redstone wire segment received an unsupported planar direction");
}

std::array<glm::vec2, 4> faceWireQuadLocalCoords(const uint16_t facing,
                                                  const PlanarWireSegmentRect& rect) {
    if (facing == PropIndices::FACING_FLOOR ||
        facing == PropIndices::FACING_NORTH) {
        return {{{rect.a0, rect.b1}, {rect.a1, rect.b1}, {rect.a1, rect.b0}, {rect.a0, rect.b0}}};
    }
    if (facing == PropIndices::FACING_CEILING ||
        facing == PropIndices::FACING_SOUTH ||
        facing == PropIndices::FACING_WEST) {
        return {{{rect.a0, rect.b0}, {rect.a1, rect.b0}, {rect.a1, rect.b1}, {rect.a0, rect.b1}}};
    }
    if (facing == PropIndices::FACING_EAST) {
        return {{{rect.a1, rect.b0}, {rect.a0, rect.b0}, {rect.a0, rect.b1}, {rect.a1, rect.b1}}};
    }
    throw std::runtime_error("Redstone wire segment received an unsupported facing value");
}

glm::vec2 planarWireSegmentUv(const PlanarWireSegmentDirection direction,
                              const glm::vec2& local) {
    switch (direction) {
        case PlanarWireSegmentDirection::NegativeA:
            return {1.0f - local.y, 0.5f - local.x};
        case PlanarWireSegmentDirection::PositiveA:
            return {local.y, local.x - 0.5f};
        case PlanarWireSegmentDirection::NegativeB:
            return {local.x, 0.5f - local.y};
        case PlanarWireSegmentDirection::PositiveB:
            return {local.x, 1.5f - local.y};
    }
    throw std::runtime_error("Redstone wire segment received an unsupported planar direction");
}

std::array<glm::vec2, 4> buildPlanarWireSegmentUv(const uint16_t facing,
                                                  const uint16_t property) {
    const PlanarWireSegmentDirection direction = planarWireSegmentDirection(facing, property);
    const PlanarWireSegmentRect rect = planarWireSegmentRect(direction);
    const std::array<glm::vec2, 4> localCoords = faceWireQuadLocalCoords(facing, rect);
    return {{
        planarWireSegmentUv(direction, localCoords[0]),
        planarWireSegmentUv(direction, localCoords[1]),
        planarWireSegmentUv(direction, localCoords[2]),
        planarWireSegmentUv(direction, localCoords[3]),
    }};
}

std::array<glm::vec3, 4> buildFloorWireQuad(const glm::vec3& pos,
                                            const float x0,
                                            const float z0,
                                            const float x1,
                                            const float z1) {
    const float y = pos.y + kFacePlaneSurfaceOffset;
    return {{{pos.x + x0, y, pos.z + z1},
             {pos.x + x1, y, pos.z + z1},
             {pos.x + x1, y, pos.z + z0},
             {pos.x + x0, y, pos.z + z0}}};
}

// Builds a wire quad on any attachable face. The first coordinate maps to X on
// floor/ceiling/north/south faces and to Z on east/west faces. The second
// coordinate maps to Z on horizontal faces and to Y on wall faces.
std::array<glm::vec3, 4> buildFaceWireQuad(const glm::vec3& pos,
                                           const uint16_t facing,
                                           const float a0,
                                           const float b0,
                                           const float a1,
                                           const float b1) {
    const float nearMin = kFacePlaneSurfaceOffset;
    const float nearMax = 1.0f - kFacePlaneSurfaceOffset;

    if (facing == PropIndices::FACING_FLOOR) {
        return buildFloorWireQuad(pos, a0, b0, a1, b1);
    }
    if (facing == PropIndices::FACING_CEILING) {
        const float y = pos.y + nearMax;
        return {{{pos.x + a0, y, pos.z + b0},
                 {pos.x + a1, y, pos.z + b0},
                 {pos.x + a1, y, pos.z + b1},
                 {pos.x + a0, y, pos.z + b1}}};
    }
    if (facing == PropIndices::FACING_NORTH) {
        const float z = pos.z + nearMax;
        return {{{pos.x + a0, pos.y + b1, z},
                 {pos.x + a1, pos.y + b1, z},
                 {pos.x + a1, pos.y + b0, z},
                 {pos.x + a0, pos.y + b0, z}}};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        const float z = pos.z + nearMin;
        return {{{pos.x + a0, pos.y + b0, z},
                 {pos.x + a1, pos.y + b0, z},
                 {pos.x + a1, pos.y + b1, z},
                 {pos.x + a0, pos.y + b1, z}}};
    }
    if (facing == PropIndices::FACING_EAST) {
        const float x = pos.x + nearMin;
        return {{{x, pos.y + b0, pos.z + a1},
                 {x, pos.y + b0, pos.z + a0},
                 {x, pos.y + b1, pos.z + a0},
                 {x, pos.y + b1, pos.z + a1}}};
    }
    if (facing == PropIndices::FACING_WEST) {
        const float x = pos.x + nearMax;
        return {{{x, pos.y + b0, pos.z + a0},
                 {x, pos.y + b0, pos.z + a1},
                 {x, pos.y + b1, pos.z + a1},
                 {x, pos.y + b1, pos.z + a0}}};
    }
    throw std::runtime_error("Redstone wire quad received an unsupported facing value");
}

std::array<glm::vec3, 4> buildPlanarWireSegment(const glm::vec3& pos,
                                                const uint16_t facing,
                                                const uint16_t property) {
    const PlanarWireSegmentRect rect = planarWireSegmentRect(planarWireSegmentDirection(facing, property));
    return buildFaceWireQuad(pos, facing, rect.a0, rect.b0, rect.a1, rect.b1);
}

std::array<glm::vec3, 4> buildRotatedFloorWireSegment(const glm::vec3& pos,
                                                      const uint16_t rotY) {
    const float y = kFacePlaneSurfaceOffset;
    std::array<glm::vec3, 4> corners = {{{0.0f, y, 0.5f},
                                          {1.0f, y, 0.5f},
                                          {1.0f, y, 0.0f},
                                          {0.0f, y, 0.0f}}};
    for (glm::vec3& corner : corners) {
        corner = rotatePointY90(corner, rotY) + pos;
    }
    return corners;
}

std::array<glm::vec2, 4> buildUvRect(const float u0,
                                     const float v0,
                                     const float u1,
                                     const float v1) {
    return {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
}

uint16_t redstonePowerPropertyValue(const uint8_t power) {
    static const std::array<uint16_t, 16> kPowerValues = {{
        PropIndices::POWER_0,
        PropIndices::POWER_1,
        PropIndices::POWER_2,
        PropIndices::POWER_3,
        PropIndices::POWER_4,
        PropIndices::POWER_5,
        PropIndices::POWER_6,
        PropIndices::POWER_7,
        PropIndices::POWER_8,
        PropIndices::POWER_9,
        PropIndices::POWER_10,
        PropIndices::POWER_11,
        PropIndices::POWER_12,
        PropIndices::POWER_13,
        PropIndices::POWER_14,
        PropIndices::POWER_15
    }};
    if (power >= kPowerValues.size()) {
        throw std::runtime_error("Wire container mesh received an unsupported power level");
    }
    return kPowerValues[power];
}

const std::unordered_map<uint16_t, BlockID>& redstoneWireBlockIdsByChannel() {
    static const std::unordered_map<uint16_t, BlockID> kWireBlockIdsByChannel = [] {
        std::unordered_map<uint16_t, BlockID> mapping;
        const std::size_t blockCount = BlockRegistry::getBlockCount();
        for (std::size_t index = 0; index < blockCount; ++index) {
            const BlockID blockId = static_cast<BlockID>(index);
            const BlockDef& def = BlockRegistry::getFast(blockId);
            if (def.redstoneBehavior != "wire" || def.redstoneWireChannelId == 0) {
                continue;
            }
            const auto [it, inserted] = mapping.emplace(def.redstoneWireChannelId, blockId);
            static_cast<void>(it);
            if (!inserted) {
                throw std::runtime_error("Wire container mesh found multiple wire blocks for one channel");
            }
        }
        return mapping;
    }();
    return kWireBlockIdsByChannel;
}

BlockID blockIdForWireChannel(const uint16_t channelId) {
    if (channelId == 0) {
        throw std::runtime_error("Wire container mesh received an empty wire channel");
    }

    const auto& mapping = redstoneWireBlockIdsByChannel();
    const auto it = mapping.find(channelId);
    if (it == mapping.end()) {
        throw std::runtime_error("Wire container mesh could not resolve a wire block for the channel");
    }
    return it->second;
}

bool hasConnectionBit(const WirePart& part, const uint8_t bit) {
    return (part.connections & bit) != 0;
}

uint16_t northConnectionValueForPart(const WirePart& part) {
    if (part.facing == PropIndices::FACING_FLOOR || part.facing == PropIndices::FACING_CEILING) {
        return hasConnectionBit(part, WireConnectionBits::AXIS2_NEG)
            ? PropIndices::NORTH_SIDE
            : PropIndices::NORTH_NONE;
    }
    return hasConnectionBit(part, WireConnectionBits::AXIS2_POS)
        ? PropIndices::NORTH_SIDE
        : PropIndices::NORTH_NONE;
}

uint16_t southConnectionValueForPart(const WirePart& part) {
    if (part.facing == PropIndices::FACING_FLOOR || part.facing == PropIndices::FACING_CEILING) {
        return hasConnectionBit(part, WireConnectionBits::AXIS2_POS)
            ? PropIndices::SOUTH_SIDE
            : PropIndices::SOUTH_NONE;
    }
    return hasConnectionBit(part, WireConnectionBits::AXIS2_NEG)
        ? PropIndices::SOUTH_SIDE
        : PropIndices::SOUTH_NONE;
}

uint16_t eastConnectionValueForPart(const WirePart& part) {
    return hasConnectionBit(part, WireConnectionBits::AXIS1_POS)
        ? PropIndices::EAST_SIDE
        : PropIndices::EAST_NONE;
}

uint16_t westConnectionValueForPart(const WirePart& part) {
    return hasConnectionBit(part, WireConnectionBits::AXIS1_NEG)
        ? PropIndices::WEST_SIDE
        : PropIndices::WEST_NONE;
}

BlockStateId stateForWirePart(const WirePart& part) {
    if (!WireFaceGeometry::isWireFacing(part.facing)) {
        throw std::runtime_error("Wire container mesh received an unsupported wire facing");
    }
    const BlockID blockId = blockIdForWireChannel(part.channelId);
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, part.facing},
            {PropIndices::POWER, redstonePowerPropertyValue(part.power)},
            {PropIndices::NORTH, northConnectionValueForPart(part)},
            {PropIndices::SOUTH, southConnectionValueForPart(part)},
            {PropIndices::EAST, eastConnectionValueForPart(part)},
            {PropIndices::WEST, westConnectionValueForPart(part)}
        });
}

const WireContainerParts* findWireContainerParts(const SubChunkMeshingSnapshot& snapshot,
                                                 const int x,
                                                 const int y,
                                                 const int z) {
    const uint16_t localIndex = static_cast<uint16_t>(scToIndex(x, y, z));
    for (const WireContainerMeshingEntry& entry : snapshot.wireContainers) {
        if (entry.localIndex == localIndex) {
            return &entry.parts;
        }
    }
    return nullptr;
}

bool isWireContainerMeshState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).isWireContainer;
}

void captureWireContainerParts(const IWorldView* worldView, SubChunkMeshingSnapshot& snapshot) {
    if (worldView == nullptr) {
        return;
    }

    for (int y = 0; y < SubChunk::SIZE; ++y) {
        for (int z = 0; z < SubChunk::SIZE; ++z) {
            for (int x = 0; x < SubChunk::SIZE; ++x) {
                const BlockStateId stateId = snapshot.blocks[scToIndex(x, y, z)];
                if (!isWireContainerMeshState(stateId)) {
                    continue;
                }

                WireContainerParts parts;
                const glm::ivec3 position(snapshot.worldOffsetX + x, snapshot.yBase + y, snapshot.worldOffsetZ + z);
                if (!worldView->copyWireContainerParts(position, parts) || parts.empty()) {
                    continue;
                }

                WireContainerMeshingEntry entry;
                entry.localIndex = static_cast<uint16_t>(scToIndex(x, y, z));
                entry.parts = parts;
                snapshot.wireContainers.push_back(entry);
            }
        }
    }
}

} // anonymous namespace

void ChunkMesher::setDebugDisableGreedyMeshing(const bool disabled) {
    g_debugDisableGreedyMeshing.store(disabled, std::memory_order_relaxed);
}

bool ChunkMesher::debugDisableGreedyMeshing() {
    return g_debugDisableGreedyMeshing.load(std::memory_order_relaxed);
}

void ChunkMeshBuilders::buildCross(ChunkMeshData& meshData,
                                   const SubChunkMeshingSnapshot& snapshot,
                                   const BlockStateId stateId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z) {
    addCrossedQuadsImpl(cutoutTargetFor(meshData, def),
                        glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                        stateId, def, x, y, z, snapshot);
    expandBounds(meshData,
                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
}

void ChunkMeshBuilders::buildTorch(ChunkMeshData& meshData,
                                    const SubChunkMeshingSnapshot& snapshot,
                                    const BlockStateId stateId,
                                    const BlockDef& def,
                                    const int x,
                                    const int y,
                                    const int z) {
    addTorchTemplateImpl(cutoutTargetFor(meshData, def),
                      glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                      stateId, x, y, z, snapshot);
    expandBounds(meshData,
                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
}

void ChunkMeshBuilders::buildWater(ChunkMeshData& meshData,
                                   const SubChunkMeshingSnapshot& snapshot,
                                   const BlockStateId stateId,
                                   const BlockDef& def,
                                   const int x,
                                   const int y,
                                   const int z) {
    addWaterFacesImpl(meshData, snapshot, stateId, def, x, y, z);
}

void ChunkMeshBuilders::buildModelBlock(ChunkMeshData& meshData,
                                        const SubChunkMeshingSnapshot& snapshot,
                                        const BlockStateId stateId,
                                        const BlockDef& def,
                                        const int x,
                                        const int y,
                                        const int z) {
    const ModelVariant* variant = BlockStateRegistry::getModelVariant(stateId);
    if (variant == nullptr || variant->model == nullptr) {
        throw std::runtime_error("Model block is missing a model variant: " +
                                 BlockRegistry::getNamespacedId(BlockStateRegistry::getBlockId(stateId)).full());
    }

    const CachedModelGeometry& geometry = getCachedModelGeometry(*variant);
    std::vector<BlockVertex>& target = selectModelVertexTarget(meshData, def);

    for (const CachedModelFace& face : geometry.faces) {
        if (shouldCullModelFace(face.cullfaceBits, snapshot, x, y, z)) {
            continue;
        }

        std::array<glm::vec3, 4> worldCorners = face.localCorners;
        const glm::vec3 blockOffset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        for (glm::vec3& corner : worldCorners) {
            corner += blockOffset;
        }

        FaceRenderData renderData = buildCachedModelFaceRenderData(snapshot, def, face, x, y, z);
        appendFaceVertices(target, worldCorners, face.uv, face.transformedFace, renderData);
        expandBoundsForCorners(meshData, worldCorners);
    }
}

void ChunkMeshBuilders::buildBlockEntity(ChunkMeshData& meshData,
                                         const SubChunkMeshingSnapshot& snapshot,
                                         const BlockStateId stateId,
                                         const BlockDef& def,
                                         const int x,
                                         const int y,
                                         const int z) {
    static_cast<void>(meshData);
    static_cast<void>(snapshot);
    static_cast<void>(stateId);
    static_cast<void>(def);
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(z);
}

void ChunkMeshBuilders::buildWireContainer(ChunkMeshData& meshData,
                                           const SubChunkMeshingSnapshot& snapshot,
                                           const BlockStateId stateId,
                                           const BlockDef& def,
                                           const int x,
                                           const int y,
                                           const int z) {
    static_cast<void>(stateId);
    static_cast<void>(def);

    const WireContainerParts* parts = findWireContainerParts(snapshot, x, y, z);
    if (parts == nullptr || parts->empty()) {
        return;
    }

    parts->forEach([&](const WirePart& part) {
        const BlockStateId partState = stateForWirePart(part);
        const BlockDef& wireDef = BlockRegistry::getFast(BlockStateRegistry::getBlockId(partState));
        ChunkMeshBuilders::buildRedstoneWire(meshData, snapshot, partState, wireDef, x, y, z);
    });
}

void ChunkMeshBuilders::buildFacePlane(ChunkMeshData& meshData,
                                       const SubChunkMeshingSnapshot& snapshot,
                                       const BlockStateId stateId,
                                       const BlockDef& def,
                                       const int x,
                                       const int y,
                                       const int z) {
    const uint16_t facing = requireFacePlaneFacing(stateId);
    const int face = facePlaneRenderFace(facing);
    const glm::vec3 blockOffset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    const std::array<glm::vec3, 4> corners = buildFacePlaneCorners(blockOffset, facing);

    FaceRenderData renderData = buildFaceRenderData(snapshot, stateId, def, x, y, z, face);
    const std::array<glm::vec2, 4> faceUV = buildFaceUv(1.0f, 1.0f, renderData.uvQuarterTurns);
    appendFaceVertices(selectModelVertexTarget(meshData, def), corners, faceUV, face, renderData);
    expandBoundsForCorners(meshData, corners);
}

void ChunkMeshBuilders::buildRedstoneWire(ChunkMeshData& meshData,
                                          const SubChunkMeshingSnapshot& snapshot,
                                          const BlockStateId stateId,
                                          const BlockDef& def,
                                          const int x,
                                          const int y,
                                          const int z) {
    requireRedstoneWireMeshProperties();

    const uint16_t facing = BlockStateRegistry::getPropertyIndex(stateId, PropIndices::FACING);
    if (!WireFaceGeometry::isWireFacing(facing)) {
        throw std::runtime_error("Redstone wire mesh received an unsupported facing value");
    }

    // Connection level per horizontal direction: 0 = none, 1 = side.
    const uint8_t north = redstoneWireConnectionLevel(stateId,
                                                       PropIndices::NORTH,
                                                        PropIndices::NORTH_NONE,
                                                        PropIndices::NORTH_SIDE,
                                                        "north");
    const uint8_t south = redstoneWireConnectionLevel(stateId,
                                                       PropIndices::SOUTH,
                                                        PropIndices::SOUTH_NONE,
                                                        PropIndices::SOUTH_SIDE,
                                                        "south");
    const uint8_t east = redstoneWireConnectionLevel(stateId,
                                                      PropIndices::EAST,
                                                      PropIndices::EAST_NONE,
                                                      PropIndices::EAST_SIDE,
                                                      "east");
    const uint8_t west = redstoneWireConnectionLevel(stateId,
                                                      PropIndices::WEST,
                                                      PropIndices::WEST_NONE,
                                                      PropIndices::WEST_SIDE,
                                                      "west");
    const bool isolated = north == 0 && south == 0 && east == 0 && west == 0;
    const uint8_t power = redstonePowerLevel(stateId);

    const AnimatedTextureRef& dotTexture = requireNamedTextureRef(def, "dot");
    const AnimatedTextureRef& lineTexture = requireNamedTextureRef(def, "line0");
    std::vector<BlockVertex>& target = selectModelVertexTarget(meshData, def);
    const glm::vec3 blockOffset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

    auto renderDataFor = [&](const AnimatedTextureRef& texture, const int face) {
        FaceRenderData renderData = buildFaceRenderData(snapshot, stateId, def, x, y, z, face);
        applyTextureRef(renderData, texture);
        renderData.tintKind = BlockTintKinds::REDSTONE;
        renderData.tintU = static_cast<uint8_t>(power << 4U);
        renderData.tintV = static_cast<uint8_t>(def.redstoneWireTint << 4U);
        renderData.flipDiagonal = false;
        return renderData;
    };

    auto emitWireQuad = [&](const AnimatedTextureRef& texture,
                            const std::array<glm::vec3, 4>& corners,
                            const std::array<glm::vec2, 4>& uv,
                            const int face) {
        const FaceRenderData renderData = renderDataFor(texture, face);
        appendFaceVertices(target, corners, uv, face, renderData);
        expandBoundsForCorners(meshData, corners);
    };

    auto emitFloorWireQuad = [&](const AnimatedTextureRef& texture,
                                 const std::array<glm::vec3, 4>& corners,
                                 const std::array<glm::vec2, 4>& uv) {
        emitWireQuad(texture, corners, uv, FACE_TOP);
    };

    if (facing != PropIndices::FACING_FLOOR) {
        const int face = facePlaneRenderFace(facing);
        const auto emitPlanarSegment = [&](const AnimatedTextureRef& texture,
                                           const uint16_t property) {
            emitWireQuad(
                texture,
                buildPlanarWireSegment(blockOffset, facing, property),
                buildPlanarWireSegmentUv(facing, property),
                face);
        };

        if (isolated) {
            emitWireQuad(dotTexture,
                         buildFaceWireQuad(blockOffset, facing, 0.0f, 0.0f, 1.0f, 1.0f),
                         buildUvRect(0.0f, 0.0f, 1.0f, 1.0f),
                         face);
            return;
        }
        if (north >= 1) {
            emitPlanarSegment(lineTexture, PropIndices::NORTH);
        }
        if (south >= 1) {
            emitPlanarSegment(lineTexture, PropIndices::SOUTH);
        }
        if (east >= 1) {
            emitPlanarSegment(lineTexture, PropIndices::EAST);
        }
        if (west >= 1) {
            emitPlanarSegment(lineTexture, PropIndices::WEST);
        }
        return;
    }

    if (isolated) {
        emitFloorWireQuad(dotTexture,
                          buildFloorWireQuad(blockOffset, 0.0f, 0.0f, 1.0f, 1.0f),
                          buildUvRect(0.0f, 0.0f, 1.0f, 1.0f));
        return;
    }

    // Flat floor segments for side connections.
    if (north >= 1) {
        emitFloorWireQuad(lineTexture,
                          buildFloorWireQuad(blockOffset, 0.0f, 0.0f, 1.0f, 0.5f),
                          buildUvRect(0.0f, 0.0f, 1.0f, 0.5f));
    }
    if (south >= 1) {
        emitFloorWireQuad(lineTexture,
                          buildFloorWireQuad(blockOffset, 0.0f, 0.5f, 1.0f, 1.0f),
                          buildUvRect(0.0f, 0.5f, 1.0f, 1.0f));
    }
    if (east >= 1) {
        emitFloorWireQuad(lineTexture,
                          buildRotatedFloorWireSegment(blockOffset, 90),
                          buildUvRect(0.0f, 0.0f, 1.0f, 0.5f));
    }
    if (west >= 1) {
        emitFloorWireQuad(lineTexture,
                          buildRotatedFloorWireSegment(blockOffset, 270),
                          buildUvRect(0.0f, 0.0f, 1.0f, 0.5f));
    }

}

void buildWaterSkippingTop(ChunkMeshData& meshData,
                           const SubChunkMeshingSnapshot& snapshot,
                           const BlockStateId stateId,
                           const BlockDef& def,
                           const int x,
                           const int y,
                           const int z,
                           const bool skipTopFace) {
    addWaterFacesImpl(meshData, snapshot, stateId, def, x, y, z, skipTopFace);
}

void ChunkMeshBuilders::buildUnitFaces(ChunkMeshData& meshData,
                                       const SubChunkMeshingSnapshot& snapshot,
                                       const BlockStateId stateId,
                                       const BlockDef& def,
                                       const int x,
                                       const int y,
                                       const int z) {
    for (int face = 0; face < 6; ++face) {
        const IVec3 normal = kFaceNormals[static_cast<size_t>(face)];
        const int nx = x + normal.x;
        const int ny = y + normal.y;
        const int nz = z + normal.z;

        if (!shouldRenderFaceImpl(snapshot, nx, ny, nz, stateId, def)) {
            continue;
        }

        auto& target = def.renderLayer == BlockRenderLayer::Transparent
            ? (usesWaterRendering(def) ? meshData.waterVertices : meshData.transparentVertices)
            : (def.renderLayer == BlockRenderLayer::Cutout
                ? cutoutTargetFor(meshData, def)
                : meshData.opaqueVertices);
        FaceRenderData renderData = buildFaceRenderData(snapshot, stateId, def, x, y, z, face);
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
    const IWorldView* worldView) {
    auto snapshot = std::make_shared<SubChunkMeshingSnapshot>();
    snapshot->scy = scy;
    snapshot->yBase = scy * SubChunk::SIZE;
    snapshot->isTopSection = (scy == Chunk::NUM_SUB_CHUNKS - 1);
    snapshot->isBottomSection = (scy == 0);
    snapshot->worldView = worldView;
    const glm::ivec3 chunkOffset = chunk.getWorldOffset();
    snapshot->worldOffsetX = chunkOffset.x;
    snapshot->worldOffsetZ = chunkOffset.z;

    const SubChunk* sc = chunk.getSubChunk(scy);
    if (!sc) {
        // All-air sub-chunk — blocks and lightMap default to 0
        // Still capture borders for completeness
        captureSubChunkBorders(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, worldView);
        captureSubChunkHalo(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, worldView);
        captureWireContainerParts(worldView, *snapshot);
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
    captureSubChunkBorders(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, worldView);
    captureSubChunkHalo(chunk, scy, *snapshot, neighborPosX, neighborNegX, neighborPosZ, neighborNegZ, worldView);
    captureWireContainerParts(worldView, *snapshot);

    return snapshot;
}

SubChunkMeshingSnapshotPtr ChunkMesher::captureSubChunkSnapshot(
    const Chunk& chunk,
    const int scy,
    const IWorldView* worldView) {
    return captureSubChunkSnapshot(chunk, scy,
                                   chunk.neighbors[0], chunk.neighbors[1],
                                   chunk.neighbors[2], chunk.neighbors[3],
                                   worldView);
}

ChunkMeshData ChunkMesher::buildSubChunkMeshData(const SubChunkMeshingSnapshot& snapshot) {
    ensureMeshBlockInfoCache();

    const auto startTime = std::chrono::steady_clock::now();

    ChunkMeshData meshData;
    meshData.opaqueVertices.reserve(2048);
    meshData.cutoutVertices.reserve(512);
    meshData.cutoutDistanceVertices.reserve(512);
    meshData.transparentVertices.reserve(1024);

    const SubChunkMeshClassPresence presence = scanMeshClassPresence(snapshot);

    buildOpaqueGreedyFaces(snapshot, meshData, presence);
    buildCutoutGreedyFaces(snapshot, meshData, presence);
    buildTransparentGreedyFaces(snapshot, meshData, presence);
    WaterTopMask mergedWaterTopFaces{};
    if (presence.hasAnyWater) {
        buildStillWaterTopGreedyFaces(snapshot, meshData, mergedWaterTopFaces);
    } else {
        mergedWaterTopFaces.fill(false);
    }

    if (presence.hasCustomBlock || presence.hasFluidLayer) {
        // Non-cube blocks (cross shapes, etc.) and waterlogged fluid rendering
        constexpr int S = SubChunk::SIZE;
        for (int y = 0; y < S; ++y) {
            for (int z = 0; z < S; ++z) {
                for (int x = 0; x < S; ++x) {
                    const std::size_t index = scToIndex(x, y, z);
                    const BlockStateId stateId = snapshot.blocks[index];
                    const BlockStateId fluidState = snapshot.fluidBlocks[index];

                    // Render the block (if any)
                    if (stateId != NULL_BLOCK_STATE) {
                        const MeshBlockInfo& info = getMeshBlockInfo(stateId);

                        if (info.cubeClass == MeshCubeClass::Other) {
                            const BlockDef& def = *info.def;
                            MeshBuilderFn builder = MeshBuilderRegistry::getBuilder(def.renderShapeTag);
                            if (builder == nullptr) {
                                builder = &ChunkMeshBuilders::buildUnitFaces;
                            }
                            if (FluidState::isWater(stateId)) {
                                buildWaterSkippingTop(meshData,
                                                      snapshot,
                                                      stateId,
                                                      def,
                                                      x,
                                                      y,
                                                      z,
                                                      mergedWaterTopFaces[index]);
                            } else {
                                builder(meshData, snapshot, stateId, def, x, y, z);
                            }
                        }
                    }

                    // Render waterlogged fluid overlay
                    if (fluidState != NULL_BLOCK_STATE && FluidState::decode(fluidState).kind != FluidKind::None) {
                        // Render water for waterlogged blocks and tolerate fluid-only cells.
                        // Pure water in the block layer is already handled by the water builder above.
                        if (FluidState::decode(stateId).kind == FluidKind::None) {
                            const BlockDef& fluidDef = *getMeshBlockInfo(fluidState).def;
                            buildWaterSkippingTop(meshData,
                                                  snapshot,
                                                  fluidState,
                                                  fluidDef,
                                                  x,
                                                  y,
                                                  z,
                                                  mergedWaterTopFaces[index]);
                        }
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
    mesh.uploadCutoutDistance(meshData.cutoutDistanceVertices);
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
