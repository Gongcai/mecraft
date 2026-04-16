#include "Chunk.h"

#include <algorithm>
#include <cstddef>

namespace {
int wrapToChunkAxis(const int value) {
    const int mod = value % Chunk::SIZE_X;
    return mod < 0 ? mod + Chunk::SIZE_X : mod;
}

uint8_t clampLight(const uint8_t level) {
    return static_cast<uint8_t>(std::min<int>(level, 15));
}
} // namespace

Chunk::Chunk(const int chunkX, const int chunkZ) : m_chunkX(chunkX), m_chunkZ(chunkZ) {
    // Sub-chunks are lazily created on first write.
    // All start as nullptr (= all-air, SubChunkType::Air with zero storage).
    m_heightMap.fill(0);
}

Chunk::~Chunk() {
    // unique_ptr<SubChunk> auto-destructs, which destroys SubChunk and its mesh
}

bool Chunk::isInBounds(const int x, const int y, const int z) {
    return x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z;
}

size_t Chunk::toIndex(const int x, const int y, const int z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(z) * SIZE_X +
           static_cast<size_t>(y) * SIZE_X * SIZE_Z;
}

// --- Sub-chunk access ---

SubChunk* Chunk::getSubChunk(const int scy) {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
    return m_subChunks[scy].get();
}

const SubChunk* Chunk::getSubChunk(const int scy) const {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
    return m_subChunks[scy].get();
}

SubChunk* Chunk::getOrCreateSubChunk(const int scy) {
    if (scy < 0 || scy >= NUM_SUB_CHUNKS) return nullptr;
    if (!m_subChunks[scy]) {
        m_subChunks[scy] = std::make_unique<SubChunk>();
        m_subChunks[scy]->m_subChunkY = scy;
    }
    return m_subChunks[scy].get();
}

// --- Light map copy (for mesher snapshot compatibility) ---

void Chunk::copyLightMapTo(std::array<uint8_t, BLOCK_COUNT>& out) const {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        const int yBase = scy * SUB_CHUNK_SIZE;
        const SubChunk* sc = getSubChunk(scy);
        if (!sc) {
            for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
                for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
                    for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                        out[toIndex(lx, yBase + ly, lz)] = 0;
                    }
                }
            }
        } else {
            for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
                for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
                    for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                        out[toIndex(lx, yBase + ly, lz)] = sc->m_lightMap[SubChunk::toIndex(lx, ly, lz)];
                    }
                }
            }
        }
    }
}

uint8_t Chunk::getLightByFlatIndex(std::size_t flatIndex) const {
    // Decode flat index back to (x, y, z), then delegate to sub-chunk
    const int z = static_cast<int>((flatIndex / SIZE_X) % SIZE_Z);
    const int y = static_cast<int>(flatIndex / (SIZE_X * SIZE_Z));
    const int x = static_cast<int>(flatIndex % SIZE_X);

    if (!isInBounds(x, y, z)) {
        return 0;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        return 0;
    }
    return sc->m_lightMap[SubChunk::toIndex(x, toSubChunkLocalY(y), z)];
}

// --- Block access (delegates to sub-chunks) ---

BlockID Chunk::getBlock(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    const int scy = toSubChunkIndex(y);
    const SubChunk* sc = getSubChunk(scy);
    if (!sc) {
        // Null sub-chunk = all air
        return 0;
    }
    return sc->getBlock(x, toSubChunkLocalY(y), z);
}

void Chunk::setBlock(const int x, const int y, const int z, const BlockID id) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const int scy = toSubChunkIndex(y);
    const int localY = toSubChunkLocalY(y);

    // If setting to air and sub-chunk doesn't exist, no-op
    if (id == 0 && !m_subChunks[scy]) {
        return;
    }

    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->setBlock(x, localY, z, id);
    // Also mark column-level dirty for meshing
    m_dirty = true;
    ++m_meshRevision;
}

void Chunk::copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const {
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        const int yBase = scy * SUB_CHUNK_SIZE;
        const SubChunk* sc = getSubChunk(scy);
        if (!sc) {
            // All-air sub-chunk
            for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
                for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
                    for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                        out[toIndex(lx, yBase + ly, lz)] = 0;
                    }
                }
            }
        } else {
            std::array<BlockID, SubChunk::BLOCK_COUNT> subBlocks{};
            sc->copyBlocksTo(subBlocks);
            for (int ly = 0; ly < SUB_CHUNK_SIZE; ++ly) {
                for (int lz = 0; lz < SUB_CHUNK_SIZE; ++lz) {
                    for (int lx = 0; lx < SUB_CHUNK_SIZE; ++lx) {
                        out[toIndex(lx, yBase + ly, lz)] = subBlocks[SubChunk::toIndex(lx, ly, lz)];
                    }
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

glm::ivec3 Chunk::worldToLocal(const int wx, const int wy, const int wz) {
    return {wrapToChunkAxis(wx), wy, wrapToChunkAxis(wz)};
}

glm::ivec3 Chunk::getWorldOffset() const {
    return {m_chunkX * SIZE_X, 0, m_chunkZ * SIZE_Z};
}

// --- Dirty tracking ---

bool Chunk::isDirty() const {
    if (m_dirty) return true;
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy] && m_subChunks[scy]->isDirty()) {
            return true;
        }
    }
    return false;
}

bool Chunk::isSubChunkDirty(const int scy) const {
    const SubChunk* sc = getSubChunk(scy);
    return sc ? sc->isDirty() : false;
}

void Chunk::markDirty() {
    m_dirty = true;
    ++m_meshRevision;
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            m_subChunks[scy]->markDirty();
        }
    }
}

uint64_t Chunk::getMeshRevision() const {
    return m_meshRevision;
}

// --- Column-level mesh (DEPRECATED — for transition) ---

namespace {
// Static empty mesh for fallback
static SubChunkMesh s_emptyMesh;
}

const SubChunkMesh& Chunk::getMesh() const {
    // Return the first non-empty sub-chunk mesh for backward compat
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            const SubChunkMesh& m = m_subChunks[scy]->getMesh();
            if (m.vertexCount > 0 || m.transparentVertexCount > 0 || m.cutoutVertexCount > 0) {
                return m;
            }
        }
    }
    return s_emptyMesh;
}

// --- Per sub-chunk mesh ---

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
}

void Chunk::markMeshClean() {
    m_dirty = false;
    for (int scy = 0; scy < NUM_SUB_CHUNKS; ++scy) {
        if (m_subChunks[scy]) {
            m_subChunks[scy]->markMeshClean();
        }
    }
}

void Chunk::markSubChunkMeshClean(const int scy) {
    SubChunk* sc = getSubChunk(scy);
    if (sc) {
        sc->markMeshClean();
    }
}

void Chunk::markSubChunkDirty(const int scy) {
    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->markDirty();
    m_dirty = true;
    ++m_meshRevision;
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
        // Null sub-chunk = all air, sky light is 15 (full sun) above height map
        // But we don't know height map here, so return 0 as safe default.
        // LightEngine will handle this correctly via world-coordinate access.
        return 0;
    }
    return sc->getSunlight(x, toSubChunkLocalY(y), z);
}

void Chunk::setSunlight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }
    const int scy = toSubChunkIndex(y);
    // Optimization: don't create a sub-chunk just to write 0 — nullptr sub-chunks default to 0
    if (level == 0 && !m_subChunks[scy]) {
        return;
    }
    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->setSunlight(x, toSubChunkLocalY(y), z, level);
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
    // Optimization: don't create a sub-chunk just to write 0 — nullptr sub-chunks default to 0
    if (level == 0 && !m_subChunks[scy]) {
        return;
    }
    SubChunk* sc = getOrCreateSubChunk(scy);
    sc->setBlockLight(x, toSubChunkLocalY(y), z, level);
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
        if (BlockRegistry::get(getBlock(x, y, z)).opacity >= 15) {
            height = y;
            break;
        }
    }
    m_heightMap[static_cast<size_t>(x) + static_cast<size_t>(z) * SIZE_X] = height;
}
