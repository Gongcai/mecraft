#include "Chunk.h"

#include <algorithm>
#include <cstddef>

#include "../fluid/FluidState.h"
#include "../../renderer/mesh/ChunkMesher.h"

namespace {
int wrapToChunkAxis(const int value) {
    const int mod = value % Chunk::SIZE_X;
    return mod < 0 ? mod + Chunk::SIZE_X : mod;
}

uint8_t clampLight(const uint8_t level) {
    return static_cast<uint8_t>(std::min<int>(level, 15));
}

void expandBounds(glm::vec3& minBounds,
                  glm::vec3& maxBounds,
                  bool& hasBounds,
                  const glm::vec3& candidateMin,
                  const glm::vec3& candidateMax) {
    if (!hasBounds) {
        minBounds = candidateMin;
        maxBounds = candidateMax;
        hasBounds = true;
        return;
    }

    minBounds.x = std::min(minBounds.x, candidateMin.x);
    minBounds.y = std::min(minBounds.y, candidateMin.y);
    minBounds.z = std::min(minBounds.z, candidateMin.z);
    maxBounds.x = std::max(maxBounds.x, candidateMax.x);
    maxBounds.y = std::max(maxBounds.y, candidateMax.y);
    maxBounds.z = std::max(maxBounds.z, candidateMax.z);
}

constexpr int OPPOSITE_SUB_CHUNK_NEIGHBOR[6] = {1, 0, 3, 2, 5, 4};

int chunkDirectionToSubChunkDirection(const int direction) {
    switch (direction) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 4;
        case 3: return 5;
        default: return -1;
    }
}

void markSubChunkAndVerticalNeighborsDirty(Chunk& chunk, const int scy, const int localY) {
    chunk.markSubChunkDirty(scy);
    if (localY == 0) {
        chunk.markSubChunkDirty(scy - 1);
    }
    if (localY == Chunk::SUB_CHUNK_SIZE - 1) {
        chunk.markSubChunkDirty(scy + 1);
    }
}
} // namespace

Chunk::Chunk(const int chunkX, const int chunkZ) : m_chunkX(chunkX), m_chunkZ(chunkZ) {
    // Sub-chunks are lazily created on first write.
    // All start as nullptr (= all-air, SubChunkType::Air with zero storage).
    m_heightMap.fill(0);
}

Chunk::~Chunk() {
    m_columnMesh.destroy();
}

bool Chunk::isInBounds(const int x, const int y, const int z) {
    return x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z;
}

size_t Chunk::toIndex(const int x, const int y, const int z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(z) * SIZE_X +
           static_cast<size_t>(y) * SIZE_X * SIZE_Z;
}

uint8_t Chunk::getImplicitSunlight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    return static_cast<uint8_t>(y >= getHeightMap(x, z) ? 15 : 0);
}

uint8_t Chunk::getImplicitPackedLight(const int x, const int y, const int z) const {
    return static_cast<uint8_t>(getImplicitSunlight(x, y, z) << 4);
}

void Chunk::initializeSubChunkLightDefaults(SubChunk& subChunk) const {
    const int yBase = subChunk.m_subChunkY * SUB_CHUNK_SIZE;
    for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
        for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                subChunk.m_lightMap[SubChunk::toIndex(lx, ly, lz)] = getImplicitPackedLight(lx, yBase + ly, lz);
            }
        }
    }
}

bool Chunk::canRecycleSubChunk(const SubChunk& subChunk) const {
    if (subChunk.getType() != SubChunkType::Air) {
        return false;
    }

    const int yBase = subChunk.m_subChunkY * SUB_CHUNK_SIZE;
    for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
        for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                if (subChunk.m_lightMap[SubChunk::toIndex(lx, ly, lz)] != getImplicitPackedLight(lx, yBase + ly, lz)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void Chunk::recycleSubChunk(const int scy) {
    SubChunk* subChunk = getSubChunk(scy);
    if (!subChunk) {
        return;
    }

    for (int direction = 0; direction < 6; ++direction) {
        SubChunk* neighbor = subChunk->neighbors[direction];
        if (!neighbor) {
            continue;
        }

        const int opposite = OPPOSITE_SUB_CHUNK_NEIGHBOR[direction];
        if (neighbor->neighbors[opposite] == subChunk) {
            neighbor->neighbors[opposite] = nullptr;
        }
        subChunk->neighbors[direction] = nullptr;
    }

    ColumnAggregateSlice& slice = m_columnAggregateSlices[scy];
    slice.opaqueVertices.clear();
    slice.cutoutVertices.clear();
    slice.hasBounds = false;
    slice.boundsMin = glm::vec3(0.0f);
    slice.boundsMax = glm::vec3(0.0f);
    m_columnMeshDirty = true;
    m_dirtySubChunkMask &= ~(1u << scy);
    m_dirty = m_dirtySubChunkMask != 0u;

    m_subChunks[scy].reset();
}

void Chunk::tryRecycleSubChunk(const int scy) {
    SubChunk* subChunk = getSubChunk(scy);
    if (!subChunk || !canRecycleSubChunk(*subChunk)) {
        return;
    }
    recycleSubChunk(scy);
}

// --- Sub-chunk access ---

SubChunk* Chunk::getOrCreateSubChunk(const int scy) {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
    if (!m_subChunks[scy]) {
        m_subChunks[scy] = std::make_unique<SubChunk>();
        SubChunk* sc = m_subChunks[scy].get();
        sc->m_subChunkY = scy;
        initializeSubChunkLightDefaults(*sc);
        m_dirtySubChunkMask |= (1u << scy);
        m_dirty = true;

        if (SubChunk* above = getSubChunk(scy + 1)) {
            sc->neighbors[2] = above;
            above->neighbors[3] = sc;
        }
        if (SubChunk* below = getSubChunk(scy - 1)) {
            sc->neighbors[3] = below;
            below->neighbors[2] = sc;
        }
        if (Chunk* posXChunk = neighbors[0]) {
            if (SubChunk* posX = posXChunk->getSubChunk(scy)) {
                sc->neighbors[0] = posX;
                posX->neighbors[1] = sc;
            }
        }
        if (Chunk* negXChunk = neighbors[1]) {
            if (SubChunk* negX = negXChunk->getSubChunk(scy)) {
                sc->neighbors[1] = negX;
                negX->neighbors[0] = sc;
            }
        }
        if (Chunk* posZChunk = neighbors[2]) {
            if (SubChunk* posZ = posZChunk->getSubChunk(scy)) {
                sc->neighbors[4] = posZ;
                posZ->neighbors[5] = sc;
            }
        }
        if (Chunk* negZChunk = neighbors[3]) {
            if (SubChunk* negZ = negZChunk->getSubChunk(scy)) {
                sc->neighbors[5] = negZ;
                negZ->neighbors[4] = sc;
            }
        }
    }
    return m_subChunks[scy].get();
}

uint8_t Chunk::getPackedLight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return getImplicitPackedLight(x, y, z);
    }
    return sc->m_lightMap[SubChunk::toIndex(x, toSubChunkLocalY(y), z)];
}

bool Chunk::replacePackedLight(const uint8_t* data, const size_t size, uint32_t* outDirtySubChunkMask) {
    if (data == nullptr || size != BLOCK_COUNT) {
        return false;
    }

    uint32_t dirtyMask = 0;
    uint32_t meshDirtyMask = 0;

    for (int y = 0; y < SIZE_Y; ++y) {
        const int scy = toSubChunkIndex(y);
        SubChunk* sc = getSubChunk(scy);
        for (int z = 0; z < SIZE_Z; ++z) {
            for (int x = 0; x < SIZE_X; ++x) {
                const size_t index = toIndex(x, y, z);
                const uint8_t packed = data[index];
                const uint8_t implicit = getImplicitPackedLight(x, y, z);
                const uint8_t currentPacked = sc
                    ? sc->m_lightMap[SubChunk::toIndex(x, toSubChunkLocalY(y), z)]
                    : implicit;

                if (currentPacked == packed) {
                    continue;
                }

                if (!sc) {
                    sc = getOrCreateSubChunk(scy);
                }
                sc->m_lightMap[SubChunk::toIndex(x, toSubChunkLocalY(y), z)] = packed;
                dirtyMask |= (1u << scy);
            }
        }
    }

    if (outDirtySubChunkMask) {
        *outDirtySubChunkMask = dirtyMask;
    }

    if (dirtyMask != 0) {
        ++m_lightRevision;
        for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
            if ((dirtyMask & (1u << scy)) != 0u) {
                meshDirtyMask |= (1u << scy);
                if (scy > 0) {
                    meshDirtyMask |= (1u << (scy - 1));
                }
                if (scy + 1 < NUM_SUB_CHUNKS) {
                    meshDirtyMask |= (1u << (scy + 1));
                }
            }
        }

        for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
            if ((meshDirtyMask & (1u << scy)) != 0u) {
                markSubChunkDirty(scy);
            }
        }
    }

    return true;
}

bool Chunk::replacePackedLightSection(const int scy, const uint8_t* data, const size_t size) {
    if (data == nullptr || size != SubChunk::BLOCK_COUNT ||
        scy < 0 || scy >= NUM_SUB_CHUNKS) {
        return false;
    }

    bool changed = false;
    SubChunk* sc = getSubChunk(scy);
    const int yBase = scy * SubChunk::SIZE;
    size_t index = 0;
    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
        const int y = yBase + ly;
        for (int z = 0; z < SIZE_Z; ++z) {
            for (int x = 0; x < SIZE_X; ++x) {
                const uint8_t packed = data[index++];
                const uint8_t currentPacked = sc
                    ? sc->m_lightMap[SubChunk::toIndex(x, ly, z)]
                    : getImplicitPackedLight(x, y, z);

                if (currentPacked == packed) {
                    continue;
                }

                if (!sc) {
                    sc = getOrCreateSubChunk(scy);
                }
                sc->m_lightMap[SubChunk::toIndex(x, ly, z)] = packed;
                changed = true;
            }
        }
    }

    if (changed) {
        ++m_lightRevision;
        markSubChunkDirty(scy);
        if (scy > 0) {
            markSubChunkDirty(scy - 1);
        }
        if (scy + 1 < NUM_SUB_CHUNKS) {
            markSubChunkDirty(scy + 1);
        }
    }

    return true;
}

// --- Block access (delegates to sub-chunks) ---

BlockStateId Chunk::getBlock(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return NULL_BLOCK_STATE;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return NULL_BLOCK_STATE;
    }
    return sc->getBlockUnchecked(x, toSubChunkLocalY(y), z);
}

BlockStateId Chunk::getFluidState(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return NULL_BLOCK_STATE;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return NULL_BLOCK_STATE;
    }
    const int localY = toSubChunkLocalY(y);
    const BlockStateId fluidState = sc->getFluidLayerUnchecked(x, localY, z);
    if (fluidState != NULL_BLOCK_STATE) {
        return fluidState;
    }
    const BlockStateId blockState = sc->getBlockUnchecked(x, localY, z);
    if (FluidState::decode(blockState).kind != FluidKind::None) {
        return blockState;
    }
    return NULL_BLOCK_STATE;
}

void Chunk::setBlockImpl(const int x,
                         const int y,
                         const int z,
                         const BlockStateId stateId,
                         const bool markMeshDirty) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const int scy = toSubChunkIndex(y);
    const int localY = toSubChunkLocalY(y);

    if (stateId == NULL_BLOCK_STATE && !m_subChunks[scy]) {
        return;
    }

    SubChunk* sc = getOrCreateSubChunk(scy);
    if (markMeshDirty) {
        sc->setBlock(x, localY, z, stateId);
        m_dirtySubChunkMask |= (1u << scy);
        m_dirty = true;
    } else {
        sc->setBlockWithoutMeshDirty(x, localY, z, stateId);
    }
    tryRecycleSubChunk(scy);

    if (!markMeshDirty) {
        return;
    }

    markSubChunkAndVerticalNeighborsDirty(*this, scy, localY);
    if (x == 0 && neighbors[1]) {
        markSubChunkAndVerticalNeighborsDirty(*neighbors[1], scy, localY);
    }
    if (x == SIZE_X - 1 && neighbors[0]) {
        markSubChunkAndVerticalNeighborsDirty(*neighbors[0], scy, localY);
    }
    if (z == 0 && neighbors[3]) {
        markSubChunkAndVerticalNeighborsDirty(*neighbors[3], scy, localY);
    }
    if (z == SIZE_Z - 1 && neighbors[2]) {
        markSubChunkAndVerticalNeighborsDirty(*neighbors[2], scy, localY);
    }

    m_dirty = m_dirtySubChunkMask != 0u;
}

void Chunk::setBlock(const int x, const int y, const int z, const BlockStateId stateId) {
    setBlockImpl(x, y, z, stateId, true);
}

void Chunk::setBlockWithoutMeshDirty(const int x, const int y, const int z, const BlockStateId stateId) {
    setBlockImpl(x, y, z, stateId, false);
}

void Chunk::setBlockFast(const int x, const int y, const int z, const BlockStateId stateId) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const int scy = toSubChunkIndex(y);
    if (stateId == NULL_BLOCK_STATE && !m_subChunks[scy]) {
        return;
    }

    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->setBlockFast(x, toSubChunkLocalY(y), z, stateId);
}

void Chunk::markExistingSubChunksDirty() {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            markSubChunkDirty(scy);
        }
    }
}

void Chunk::linkExistingSubChunksWithNeighbor(const int direction) {
    if (direction < 0 || direction >= 4) {
        return;
    }

    Chunk* neighborChunk = neighbors[direction];
    if (!neighborChunk) {
        return;
    }

    const int selfSubDir = chunkDirectionToSubChunkDirection(direction);
    const int neighborSubDir = OPPOSITE_SUB_CHUNK_NEIGHBOR[selfSubDir];
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        SubChunk* self = getSubChunk(scy);
        SubChunk* neighbor = neighborChunk->getSubChunk(scy);
        if (!self || !neighbor) {
            continue;
        }
        self->neighbors[selfSubDir] = neighbor;
        neighbor->neighbors[neighborSubDir] = self;
    }
}

void Chunk::unlinkExistingSubChunksFromNeighbor(const int direction) {
    if (direction < 0 || direction >= 4) {
        return;
    }

    Chunk* neighborChunk = neighbors[direction];
    const int selfSubDir = chunkDirectionToSubChunkDirection(direction);
    const int neighborSubDir = OPPOSITE_SUB_CHUNK_NEIGHBOR[selfSubDir];
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (SubChunk* self = getSubChunk(scy)) {
            if (self->neighbors[selfSubDir]) {
                self->neighbors[selfSubDir] = nullptr;
            }
        }
        if (neighborChunk) {
            if (SubChunk* neighbor = neighborChunk->getSubChunk(scy)) {
                if (neighbor->neighbors[neighborSubDir]) {
                    neighbor->neighbors[neighborSubDir] = nullptr;
                }
            }
        }
    }
}

void Chunk::optimizePalette() {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            m_subChunks[scy]->optimizePalette();
        }
    }
}

void Chunk::seedInitialLightMap() {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (SubChunk* sc = getSubChunk(scy)) {
            sc->m_lightMap.fill(0);
        }
    }

    for (int z = 0; z < SIZE_Z; ++z) {
        for (int x = 0; x < SIZE_X; ++x) {
            uint8_t skyLevel = 15;
            for (int y = SIZE_Y - 1; y >= 0; --y) {
                const int scy = toSubChunkIndex(y);
                SubChunk* sc = getSubChunk(scy);

                BlockStateId stateId = NULL_BLOCK_STATE;
                if (sc) {
                    stateId = sc->getBlockUnchecked(x, toSubChunkLocalY(y), z);
                }

                const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
                const BlockDef& def = BlockRegistry::getFast(blockId);
                if (def.opacity >= 15) {
                    skyLevel = 0;
                } else if (skyLevel > 0) {
                    skyLevel = (skyLevel > def.opacity)
                        ? static_cast<uint8_t>(skyLevel - def.opacity)
                        : 0;
                }

                if (!sc) {
                    continue;
                }

                const uint8_t blockLight = (def.isLightSource && def.lightLevel > 0)
                    ? clampLight(def.lightLevel)
                    : 0;
                sc->m_lightMap[SubChunk::toIndex(x, toSubChunkLocalY(y), z)] =
                    static_cast<uint8_t>((skyLevel << 4) | blockLight);
            }
        }
    }
}

glm::ivec3 Chunk::worldToLocal(const int wx, const int wy, const int wz) {
    return {wrapToChunkAxis(wx), wy, wrapToChunkAxis(wz)};
}

glm::ivec3 Chunk::getWorldOffset() const {
    return {m_chunkX * SIZE_X, 0, m_chunkZ * SIZE_Z};
}

// --- Dirty tracking ---

bool Chunk::isDirty() const {
    return m_dirty || m_dirtySubChunkMask != 0u;
}

// --- Per sub-chunk mesh ---

namespace {
static SubChunkMesh s_emptyMesh;
}

const SubChunkMesh& Chunk::getSubChunkMesh(const int scy) const {
    const SubChunk* sc = getSubChunk(scy);
    return sc ? sc->getMesh() : s_emptyMesh;
}

SubChunkMesh& Chunk::getSubChunkMesh(const int scy) {
    SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        // Cannot return reference to null — fallback to static empty (read-only path)
        return s_emptyMesh;
    }
    return sc->getMesh();
}

void Chunk::setSubChunkMesh(const int scy, const SubChunkMesh& mesh) {
    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->setMesh(mesh);
    ++m_renderStateRevision;
    m_dirtySubChunkMask &= ~(1u << scy);
    m_dirty = m_dirtySubChunkMask != 0u;
}

const SubChunkMesh& Chunk::getColumnMesh() const {
    return m_columnMesh;
}

SubChunkMesh& Chunk::getColumnMesh() {
    return m_columnMesh;
}

void Chunk::updateColumnAggregateData(const int scy, const ChunkMeshData& meshData, const bool skipGlUpload) {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) {
        return;
    }

    ColumnAggregateSlice& slice = m_columnAggregateSlices[scy];
    slice.opaqueVertices = meshData.opaqueVertices;
    slice.cutoutVertices = meshData.cutoutVertices;
    slice.cutoutDistanceVertices = meshData.cutoutDistanceVertices;

    const glm::ivec3 worldOffset = getWorldOffset();
    const float xOff = static_cast<float>(worldOffset.x);
    const float yOff = static_cast<float>(scy * SubChunk::SIZE + worldOffset.y);
    const float zOff = static_cast<float>(worldOffset.z);
    const glm::vec3 boundsOffset(xOff, static_cast<float>(worldOffset.y), zOff);
    for (BlockVertex& vertex : slice.opaqueVertices) {
        vertex.x += xOff;
        vertex.y += yOff;
        vertex.z += zOff;
    }
    for (BlockVertex& vertex : slice.cutoutVertices) {
        vertex.x += xOff;
        vertex.y += yOff;
        vertex.z += zOff;
    }
    for (BlockVertex& vertex : slice.cutoutDistanceVertices) {
        vertex.x += xOff;
        vertex.y += yOff;
        vertex.z += zOff;
    }

    slice.hasBounds = meshData.hasBounds &&
        (!slice.opaqueVertices.empty() ||
         !slice.cutoutVertices.empty() ||
         !slice.cutoutDistanceVertices.empty());
    if (slice.hasBounds) {
        slice.boundsMin = meshData.boundsMin + boundsOffset;
        slice.boundsMax = meshData.boundsMax + boundsOffset;
    } else {
        slice.boundsMin = glm::vec3(0.0f);
        slice.boundsMax = glm::vec3(0.0f);
    }

    m_columnMeshDirty = true;
    if (skipGlUpload) {
        // In MDI mode, skip VBO rebuild but still update bounds for culling.
        bool hasBounds = false;
        glm::vec3 boundsMin(0.0f);
        glm::vec3 boundsMax(0.0f);
        for (const ColumnAggregateSlice& s : m_columnAggregateSlices) {
            if (s.hasBounds) {
                expandBounds(boundsMin, boundsMax, hasBounds, s.boundsMin, s.boundsMax);
            }
        }
        m_columnMesh.vertexCount = 0;
        m_columnMesh.cutoutVertexCount = 0;
        m_columnMesh.cutoutDistanceVertexCount = 0;
        m_columnMesh.hasBounds = hasBounds;
        m_columnMesh.boundsMin = hasBounds ? boundsMin : glm::vec3(0.0f);
        m_columnMesh.boundsMax = hasBounds ? boundsMax : glm::vec3(0.0f);
        m_columnMeshDirty = false;
        return;
    }
    rebuildColumnMesh();
}

void Chunk::updateColumnAggregateBoundsOnly(const int scy,
                                            const ChunkMeshData& meshData,
                                            const bool hasRenderableVertices) {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) {
        return;
    }

    ColumnAggregateSlice& slice = m_columnAggregateSlices[scy];
    slice.opaqueVertices.clear();
    slice.cutoutVertices.clear();
    slice.cutoutDistanceVertices.clear();

    slice.hasBounds = meshData.hasBounds && hasRenderableVertices;
    if (slice.hasBounds) {
        const glm::ivec3 worldOffset = getWorldOffset();
        const glm::vec3 boundsOffset(
            static_cast<float>(worldOffset.x),
            static_cast<float>(worldOffset.y),
            static_cast<float>(worldOffset.z));
        slice.boundsMin = meshData.boundsMin + boundsOffset;
        slice.boundsMax = meshData.boundsMax + boundsOffset;
    } else {
        slice.boundsMin = glm::vec3(0.0f);
        slice.boundsMax = glm::vec3(0.0f);
    }

    bool hasBounds = false;
    glm::vec3 boundsMin(0.0f);
    glm::vec3 boundsMax(0.0f);
    for (const ColumnAggregateSlice& s : m_columnAggregateSlices) {
        if (s.hasBounds) {
            expandBounds(boundsMin, boundsMax, hasBounds, s.boundsMin, s.boundsMax);
        }
    }

    m_columnMesh.vertexCount = 0;
    m_columnMesh.cutoutVertexCount = 0;
    m_columnMesh.cutoutDistanceVertexCount = 0;
    m_columnMesh.hasBounds = hasBounds;
    m_columnMesh.boundsMin = hasBounds ? boundsMin : glm::vec3(0.0f);
    m_columnMesh.boundsMax = hasBounds ? boundsMax : glm::vec3(0.0f);
    m_columnMeshDirty = false;
}

void Chunk::ensureColumnMeshBuilt() {
    if (!m_columnMeshDirty) {
        return;
    }
    rebuildColumnMesh();
}

void Chunk::rebuildColumnMesh() {
    size_t totalOpaqueVertices = 0;
    size_t totalCutoutVertices = 0;
    size_t totalCutoutDistanceVertices = 0;
    bool hasBounds = false;
    glm::vec3 boundsMin(0.0f);
    glm::vec3 boundsMax(0.0f);

    for (const ColumnAggregateSlice& slice : m_columnAggregateSlices) {
        totalOpaqueVertices += slice.opaqueVertices.size();
        totalCutoutVertices += slice.cutoutVertices.size();
        totalCutoutDistanceVertices += slice.cutoutDistanceVertices.size();
        if (slice.hasBounds) {
            expandBounds(boundsMin, boundsMax, hasBounds, slice.boundsMin, slice.boundsMax);
        }
    }

    std::vector<BlockVertex> opaqueVertices;
    opaqueVertices.reserve(totalOpaqueVertices);
    std::vector<BlockVertex> cutoutVertices;
    cutoutVertices.reserve(totalCutoutVertices);
    std::vector<BlockVertex> cutoutDistanceVertices;
    cutoutDistanceVertices.reserve(totalCutoutDistanceVertices);

    for (const ColumnAggregateSlice& slice : m_columnAggregateSlices) {
        opaqueVertices.insert(opaqueVertices.end(), slice.opaqueVertices.begin(), slice.opaqueVertices.end());
        cutoutVertices.insert(cutoutVertices.end(), slice.cutoutVertices.begin(), slice.cutoutVertices.end());
        cutoutDistanceVertices.insert(cutoutDistanceVertices.end(),
                                      slice.cutoutDistanceVertices.begin(),
                                      slice.cutoutDistanceVertices.end());
    }

    m_columnMesh.upload(opaqueVertices);
    m_columnMesh.uploadCutout(cutoutVertices);
    m_columnMesh.uploadCutoutDistance(cutoutDistanceVertices);
    m_columnMesh.hasBounds = hasBounds;
    m_columnMesh.boundsMin = hasBounds ? boundsMin : glm::vec3(0.0f);
    m_columnMesh.boundsMax = hasBounds ? boundsMax : glm::vec3(0.0f);
    m_columnMeshDirty = false;
}

void Chunk::markMeshClean() {
    m_dirty = false;
    m_dirtySubChunkMask = 0;
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            m_subChunks[scy]->markMeshClean();
        }
    }
}

void Chunk::markSubChunkDirty(const int scy) {
    SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return;
    }

    sc->markDirty();
    ++m_renderStateRevision;
    if ((m_dirtySubChunkMask & (1u << scy)) != 0u) {
        return;
    }

    m_dirtySubChunkMask |= (1u << scy);
    m_dirty = true;
}

uint64_t Chunk::getSubChunkMeshRevision(const int scy) const {
    const SubChunk* sc = getSubChunk(scy);
    return sc ? sc->getMeshRevision() : 0;
}

// --- Light access (delegates to sub-chunks) ---

uint8_t Chunk::getSunlight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return getImplicitSunlight(x, y, z);
    }
    return sc->getSunlight(x, toSubChunkLocalY(y), z);
}

void Chunk::setSunlight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }
    const int scy = toSubChunkIndex(y);
    const uint8_t clamped = clampLight(level);
    SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        if (clamped == getImplicitSunlight(x, y, z)) {
            return;
        }
        sc = getOrCreateSubChunk(scy);
    }
    sc->setSunlight(x, toSubChunkLocalY(y), z, clamped);
    tryRecycleSubChunk(scy);
}

uint8_t Chunk::getBlockLight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return 0;
    }
    return sc->getBlockLight(x, toSubChunkLocalY(y), z);
}

void Chunk::setBlockLight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }
    const int scy = toSubChunkIndex(y);
    const uint8_t clamped = clampLight(level);
    SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        if (clamped == 0) {
            return;
        }
        sc = getOrCreateSubChunk(scy);
    }
    sc->setBlockLight(x, toSubChunkLocalY(y), z, clamped);
    tryRecycleSubChunk(scy);
}

// --- Height map (remains column-level) ---

int Chunk::getHeightMap(const int x, const int z) const {
    if (x < 0 || x >= SIZE_X || z < 0 || z >= SIZE_Z) {
        return 0;
    }
    return m_heightMap[static_cast<size_t>(x) + static_cast<size_t>(z) * SIZE_X];
}

void Chunk::setHeightMap(const int x, const int z, const int height) {
    if (x < 0 || x >= SIZE_X || z < 0 || z >= SIZE_Z) {
        return;
    }
    m_heightMap[static_cast<size_t>(x) + static_cast<size_t>(z) * SIZE_X] = height;
}

void Chunk::recalcHeightMap(const int x, const int z) {
    if (x < 0 || x >= SIZE_X || z < 0 || z >= SIZE_Z) {
        return;
    }
    int height = 0;
    for (int y = SIZE_Y - 1; y >= 0; --y) {
        if (BlockRegistry::getOpacityFast(BlockStateRegistry::getBlockId(getBlock(x, y, z))) >= 15) {
            height = y;
            break;
        }
    }
    m_heightMap[static_cast<size_t>(x) + static_cast<size_t>(z) * SIZE_X] = height;
}
