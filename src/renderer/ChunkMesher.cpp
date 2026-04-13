#include "ChunkMesher.h"

#include <array>
#include <algorithm>
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
constexpr float CROSS_GRASS_MARKER = -1.0f;
constexpr float CROSS_FLOWER_MARKER = -2.0f;

constexpr std::array<IVec3, 6> kFaceNormals = {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}}};

constexpr std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}}, // top
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // bottom
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // front (+z)
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // back (-z)
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // left (-x)
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}  // right (+x)
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
    return (getNeighborAwareLight(snapshot, x, y, z) >> 4) & 0x0F;
}

uint8_t getNeighborBlockLight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    return getNeighborAwareLight(snapshot, x, y, z) & 0x0F;
}

// Get face-center light: use the light from the neighbor air/transparent block
// that this face is exposed to
uint8_t getFaceSunlight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z, int face) {
    const int nx = x + kFaceNormals[face].x;
    const int ny = y + kFaceNormals[face].y;
    const int nz = z + kFaceNormals[face].z;
    return getNeighborSunlight(snapshot, nx, ny, nz);
}

uint8_t getFaceBlockLight(const ChunkMeshingSnapshot& snapshot, int x, int y, int z, int face) {
    const int nx = x + kFaceNormals[face].x;
    const int ny = y + kFaceNormals[face].y;
    const int nz = z + kFaceNormals[face].z;
    return getNeighborBlockLight(snapshot, nx, ny, nz);
}

// Ambient Occlusion: compute per-vertex AO value (0-3)
uint8_t computeVertexAO(bool side1, bool side2, bool corner) {
    if (side1 && side2) return 0;
    return 3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
}

// Check if a block at the given position is solid for AO purposes
bool isSolidForAO(const ChunkMeshingSnapshot& snapshot, int x, int y, int z) {
    const BlockID id = getNeighborAwareBlock(snapshot, x, y, z);
    return BlockRegistry::get(id).isSolid;
}

// Compute AO values for the 4 vertices of a face.
// For each vertex, we check 3 neighbors in the plane one step along the face normal:
//   side1: along the first tangent axis
//   side2: along the second tangent axis
//   corner: diagonal (both tangent axes)
std::array<uint8_t, 4> computeFaceAO(const ChunkMeshingSnapshot& snapshot,
                                       int x, int y, int z, int face) {
    // Face normal direction
    const int nx = kFaceNormals[face].x;
    const int ny = kFaceNormals[face].y;
    const int nz = kFaceNormals[face].z;

    // Base position: one step along the normal (the face's exterior side)
    const int bx = x + nx;
    const int by = y + ny;
    const int bz = z + nz;

    // Determine the two tangent axes for each face
    // axis0, axis1: the two axes perpendicular to the face normal
    // We encode directions as offsets along these axes for each vertex corner.
    // kFaceCorners gives the 4 corners of each face in order.
    // Each corner is either 0 or 1 along each tangent axis.
    // We need to convert corner position to direction: 0 -> -1, 1 -> +1

    // For each face, identify which world axes are the two tangent axes:
    int a0, a1; // world axis indices (0=x, 1=y, 2=z)
    if (ny != 0) { a0 = 0; a1 = 2; } // top/bottom: tangent = x, z
    else if (nz != 0) { a0 = 0; a1 = 1; } // front/back: tangent = x, y
    else { a0 = 2; a1 = 1; } // left/right: tangent = z, y

    std::array<uint8_t, 4> ao{};
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 c = kFaceCorners[face][i];

        // Convert corner position to direction along each tangent axis
        // corner value 0 -> direction -1, corner value 1 -> direction +1
        const int d0 = (c[static_cast<size_t>(a0)] > 0.5f) ? 1 : -1;
        const int d1 = (c[static_cast<size_t>(a1)] > 0.5f) ? 1 : -1;

        // Build the 3 neighbor positions to check
        int s1[3] = {bx, by, bz}; s1[a0] += d0;
        int s2[3] = {bx, by, bz}; s2[a1] += d1;
        int cn[3] = {bx, by, bz}; cn[a0] += d0; cn[a1] += d1;

        const bool side1 = isSolidForAO(snapshot, s1[0], s1[1], s1[2]);
        const bool side2 = isSolidForAO(snapshot, s2[0], s2[1], s2[2]);
        const bool corner = isSolidForAO(snapshot, cn[0], cn[1], cn[2]);

        ao[i] = computeVertexAO(side1, side2, corner);
    }

    return ao;
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
            // Light borders
            snapshot.posXLightBorder[index] = posX ? posX->m_lightMap[posX->toIndex(0, y, z)] : 0;
            snapshot.negXLightBorder[index] = negX ? negX->m_lightMap[negX->toIndex(Chunk::SIZE_X - 1, y, z)] : 0;
        }
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            const std::size_t index = toBorderYXIndex(y, x);
            const Chunk* posZ = chunk.neighbors[2];
            const Chunk* negZ = chunk.neighbors[3];
            snapshot.posZBorder[index] = posZ ? posZ->getBlock(x, y, 0) : BlockType::AIR;
            snapshot.negZBorder[index] = negZ ? negZ->getBlock(x, y, Chunk::SIZE_Z - 1) : BlockType::AIR;
            // Light borders
            snapshot.posZLightBorder[index] = posZ ? posZ->m_lightMap[posZ->toIndex(x, y, 0)] : 0;
            snapshot.negZLightBorder[index] = negZ ? negZ->m_lightMap[negZ->toIndex(x, y, Chunk::SIZE_Z - 1)] : 0;
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
}

ChunkMeshingSnapshot ChunkMesher::captureSnapshot(const Chunk& chunk) {
    ChunkMeshingSnapshot snapshot;

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const std::size_t index = toIndex(x, y, z);
                snapshot.blocks[index] = chunk.getBlock(x, y, z);
                snapshot.lightMap[index] = chunk.m_lightMap[index];
            }
        }
    }

    captureBorders(chunk, snapshot);
    return snapshot;
}

ChunkMeshData ChunkMesher::buildMeshData(const ChunkMeshingSnapshot& snapshot) {
    ChunkMeshData meshData;
    meshData.opaqueVertices.reserve(Chunk::SIZE_X * Chunk::SIZE_Z * 256);
    meshData.cutoutVertices.reserve(Chunk::SIZE_X * Chunk::SIZE_Z * 64);
    meshData.transparentVertices.reserve(Chunk::SIZE_X * Chunk::SIZE_Z * 64);

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID blockId = snapshot.blocks[toIndex(x, y, z)];
                if (blockId == BlockType::AIR) {
                    continue;
                }

                const BlockDef& def = BlockRegistry::get(blockId);
                if (def.renderShape == BlockRenderShape::Cross) {
                    addCrossedQuads(meshData.cutoutVertices,
                                    glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                    def, x, y, z, snapshot);
                    expandBounds(meshData,
                                 glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                 glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
                    continue;
                }

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
                            def, x, y, z, snapshot);

                    if (!meshData.hasBounds) {
                        expandBounds(meshData,
                                     glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                                     glm::vec3(static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)));
                    } else {
                        meshData.boundsMin.x = std::min(meshData.boundsMin.x, static_cast<float>(x));
                        meshData.boundsMin.y = std::min(meshData.boundsMin.y, static_cast<float>(y));
                        meshData.boundsMin.z = std::min(meshData.boundsMin.z, static_cast<float>(z));
                        meshData.boundsMax.x = std::max(meshData.boundsMax.x, static_cast<float>(x + 1));
                        meshData.boundsMax.y = std::max(meshData.boundsMax.y, static_cast<float>(y + 1));
                        meshData.boundsMax.z = std::max(meshData.boundsMax.z, static_cast<float>(z + 1));
                    }
                }
            }
        }
    }

    return meshData;
}

void ChunkMesher::generateMesh(Chunk& chunk) {
    const ChunkMeshingSnapshot snapshot = captureSnapshot(chunk);
    ChunkMeshData meshData = buildMeshData(snapshot);
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
                          int x, int y, int z,
                          const ChunkMeshingSnapshot& snapshot) {
    int tileIndex = getFaceTextureIndex(def, face);
    if (tileIndex < 0) {
        tileIndex = 0;
    }

    const float layer = static_cast<float>(tileIndex);

    // Get face lighting from the neighbor direction
    const uint8_t sunLight = getFaceSunlight(snapshot, x, y, z, face);
    const uint8_t blockLight = getFaceBlockLight(snapshot, x, y, z, face);

    // Compute per-vertex AO
    const std::array<uint8_t, 4> ao = computeFaceAO(snapshot, x, y, z, face);

    // Normalized [0,1] UV coordinates
    const std::array<glm::vec2, 4> faceUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
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
            static_cast<float>(sunLight),
            static_cast<float>(blockLight),
            static_cast<float>(ao[index]),
            layer
        });
    }
}

void ChunkMesher::addCrossedQuads(std::vector<BlockVertex>& vertices,
                                  const glm::vec3& pos,
                                  const BlockDef& def,
                                  int x, int y, int z,
                                  const ChunkMeshingSnapshot& snapshot) {
    int tileIndex = def.texTop;
    if (tileIndex < 0) {
        tileIndex = 0;
    }

    const float layer = static_cast<float>(tileIndex);

    // Cross quads use the block's own position light (average from all neighbors)
    // Use the maximum sunlight/blocklight from all 6 neighbors
    uint8_t sunLight = getNeighborSunlight(snapshot, x, y, z);
    uint8_t blockLight = getNeighborBlockLight(snapshot, x, y, z);
    for (int d = 0; d < 6; ++d) {
        const int nx = x + kFaceNormals[d].x;
        const int ny = y + kFaceNormals[d].y;
        const int nz = z + kFaceNormals[d].z;
        sunLight = std::max(sunLight, getNeighborSunlight(snapshot, nx, ny, nz));
        blockLight = std::max(blockLight, getNeighborBlockLight(snapshot, nx, ny, nz));
    }

    const std::array<glm::vec2, 4> quadUV = {{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const std::array<int, 6> indices = {{0, 1, 2, 0, 2, 3}};

    const float crossMarker = def.useGrassTint ? CROSS_GRASS_MARKER : CROSS_FLOWER_MARKER;

    const auto emitQuad = [&](const std::array<glm::vec3, 4>& corners) {
        for (const int index : indices) {
            const glm::vec3 local = corners[index];
            const glm::vec2 uvCoord = quadUV[index];
            vertices.push_back({
                pos.x + local.x,
                pos.y + local.y,
                pos.z + local.z,
                uvCoord.x,
                uvCoord.y,
                crossMarker,
                static_cast<float>(sunLight),
                static_cast<float>(blockLight),
                3.0f, // No AO for cross quads (always full brightness)
                layer
            });
        }
    };

    emitQuad(kCrossQuadA);
    emitQuad(kCrossQuadB);
}




