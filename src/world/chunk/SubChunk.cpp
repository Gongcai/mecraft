#include "SubChunk.h"

#include <algorithm>
#include <unordered_set>

#include <glad/glad.h>

#include "../fluid/FluidState.h"
#include "../fluid/FluidRegistry.h"

namespace {
uint8_t clampLight(const uint8_t level) {
    return static_cast<uint8_t>(std::min<int>(level, 15));
}

template <typename Map>
std::size_t estimateUnorderedMapBytes(const Map& map) {
    return map.bucket_count() * sizeof(void*) +
           map.size() * (sizeof(typename Map::value_type) + sizeof(void*) * 2);
}
} // namespace

SubChunk::SubChunk()
    : m_blockData(BLOCK_COUNT, 2)
    , m_fluidData(BLOCK_COUNT, 1) {
    m_palette.getOrCreateIndex(NULL_BLOCK_STATE);
    m_blockData.fill(0);
    m_fluidPalette.getOrCreateIndex(NULL_BLOCK_STATE);
    m_fluidData.fill(0);
    m_lightMap.fill(0);
    m_blockCounts.emplace(NULL_BLOCK_STATE, static_cast<uint32_t>(BLOCK_COUNT));
    m_fluidCounts.emplace(NULL_BLOCK_STATE, static_cast<uint32_t>(BLOCK_COUNT));
}

SubChunk::~SubChunk() {
    m_mesh.destroy();
}

SubChunk::SubChunk(SubChunk&& other) noexcept
    : m_palette(std::move(other.m_palette))
    , m_blockData(std::move(other.m_blockData))
    , m_blockCounts(std::move(other.m_blockCounts))
    , m_fluidPalette(std::move(other.m_fluidPalette))
    , m_fluidData(std::move(other.m_fluidData))
    , m_fluidCounts(std::move(other.m_fluidCounts))
    , m_lightMap(std::move(other.m_lightMap))
    , m_type(other.m_type)
    , m_dirty(other.m_dirty)
    , m_meshRevision(other.m_meshRevision)
    , m_mesh(std::move(other.m_mesh))
    , m_subChunkY(other.m_subChunkY) {
    for (int i = 0; i < 6; ++i) {
        neighbors[i] = other.neighbors[i];
        other.neighbors[i] = nullptr;
    }
}

SubChunk& SubChunk::operator=(SubChunk&& other) noexcept {
    if (this != &other) {
        m_mesh.destroy();
        m_palette = std::move(other.m_palette);
        m_blockData = std::move(other.m_blockData);
        m_blockCounts = std::move(other.m_blockCounts);
        m_fluidPalette = std::move(other.m_fluidPalette);
        m_fluidData = std::move(other.m_fluidData);
        m_fluidCounts = std::move(other.m_fluidCounts);
        m_lightMap = std::move(other.m_lightMap);
        m_type = other.m_type;
        m_dirty = other.m_dirty;
        m_meshRevision = other.m_meshRevision;
        m_mesh = std::move(other.m_mesh);
        m_subChunkY = other.m_subChunkY;
        for (int i = 0; i < 6; ++i) {
            neighbors[i] = other.neighbors[i];
            other.neighbors[i] = nullptr;
        }
    }
    return *this;
}

bool SubChunk::isInBounds(const int x, const int y, const int z) {
    return x >= 0 && x < SIZE && y >= 0 && y < SIZE && z >= 0 && z < SIZE;
}

size_t SubChunk::toIndex(const int x, const int y, const int z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(z) * SIZE +
           static_cast<size_t>(y) * SIZE * SIZE;
}

BlockStateId SubChunk::getBlock(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return NULL_BLOCK_STATE;
    }
    const size_t idx = toIndex(x, y, z);
    const uint32_t paletteIdx = m_blockData.get(idx);
    return m_palette.getStateId(paletteIdx);
}

void SubChunk::setBlockImpl(const int x,
                            const int y,
                            const int z,
                            const BlockStateId stateId,
                            const bool markMeshDirty) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint32_t oldPaletteIdx = m_blockData.get(index);
    const BlockStateId oldStateId = m_palette.getStateId(oldPaletteIdx);
    if (oldStateId == stateId) {
        return;
    }

    const uint32_t paletteIdx = m_palette.getOrCreateIndex(stateId);
    const uint8_t neededBits = m_palette.bitsPerEntry();
    if (neededBits > m_blockData.bitsPerEntry()) {
        m_blockData.resize(neededBits);
    }

    m_blockData.set(index, paletteIdx);

    auto decrementCount = [&](const BlockStateId countedStateId) {
        auto it = m_blockCounts.find(countedStateId);
        if (it == m_blockCounts.end()) {
            return;
        }
        if (it->second <= 1) {
            m_blockCounts.erase(it);
        } else {
            --it->second;
        }
    };
    decrementCount(oldStateId);
    ++m_blockCounts[stateId];

    inferType();
    if (markMeshDirty) {
        m_dirty = true;
        ++m_meshRevision;
    }
}

void SubChunk::setBlock(const int x, const int y, const int z, const BlockStateId stateId) {
    setBlockImpl(x, y, z, stateId, true);
}

void SubChunk::setBlockWithoutMeshDirty(const int x, const int y, const int z, const BlockStateId stateId) {
    setBlockImpl(x, y, z, stateId, false);
}

void SubChunk::setBlockFast(const int x, const int y, const int z, const BlockStateId stateId) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint32_t oldPaletteIdx = m_blockData.get(index);
    const BlockStateId oldStateId = m_palette.getStateId(oldPaletteIdx);
    if (oldStateId == stateId) {
        return;
    }

    const uint32_t paletteIdx = m_palette.getOrCreateIndex(stateId);
    const uint8_t neededBits = m_palette.bitsPerEntry();
    if (neededBits > m_blockData.bitsPerEntry()) {
        m_blockData.resize(neededBits);
    }

    m_blockData.set(index, paletteIdx);

    auto decrementCount = [&](const BlockStateId countedStateId) {
        auto it = m_blockCounts.find(countedStateId);
        if (it == m_blockCounts.end()) {
            return;
        }
        if (it->second <= 1) {
            m_blockCounts.erase(it);
        } else {
            --it->second;
        }
    };
    decrementCount(oldStateId);
    ++m_blockCounts[stateId];

    inferType();
}

void SubChunk::initializeFromBlocks(const std::array<BlockStateId, BLOCK_COUNT>& blocks) {
    m_palette.clear();
    m_blockCounts.clear();
    m_fluidPalette.clear();
    m_fluidCounts.clear();

    std::array<uint32_t, BLOCK_COUNT> paletteIndices{};
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        const BlockStateId stateId = blocks[index];
        paletteIndices[index] = m_palette.getOrCreateIndex(stateId);
        ++m_blockCounts[stateId];
    }

    m_blockData = BitPackedArray(BLOCK_COUNT, m_palette.bitsPerEntry());
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        m_blockData.set(index, paletteIndices[index]);
    }

    m_fluidPalette.getOrCreateIndex(NULL_BLOCK_STATE);
    m_fluidData = BitPackedArray(BLOCK_COUNT, 1);
    m_fluidData.fill(0);
    m_fluidCounts.emplace(NULL_BLOCK_STATE, static_cast<uint32_t>(BLOCK_COUNT));

    inferType();
    m_dirty = true;
}

void SubChunk::copyBlocksTo(std::array<BlockStateId, BLOCK_COUNT>& out) const {
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        const uint32_t paletteIdx = m_blockData.get(index);
        out[index] = m_palette.getStateId(paletteIdx);
    }
}

BlockStateId SubChunk::getFluidLayer(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return NULL_BLOCK_STATE;
    }
    const size_t idx = toIndex(x, y, z);
    const uint32_t paletteIdx = m_fluidData.get(idx);
    return m_fluidPalette.getStateId(paletteIdx);
}

void SubChunk::setFluidLayer(const int x, const int y, const int z, const BlockStateId stateId) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint32_t oldPaletteIdx = m_fluidData.get(index);
    const BlockStateId oldStateId = m_fluidPalette.getStateId(oldPaletteIdx);
    if (oldStateId == stateId) {
        return;
    }

    const uint32_t paletteIdx = m_fluidPalette.getOrCreateIndex(stateId);
    const uint8_t neededBits = m_fluidPalette.bitsPerEntry();
    if (neededBits > m_fluidData.bitsPerEntry()) {
        m_fluidData.resize(neededBits);
    }

    m_fluidData.set(index, paletteIdx);

    auto decrementCount = [&](const BlockStateId countedStateId) {
        auto it = m_fluidCounts.find(countedStateId);
        if (it == m_fluidCounts.end()) {
            return;
        }
        if (it->second <= 1) {
            m_fluidCounts.erase(it);
        } else {
            --it->second;
        }
    };
    decrementCount(oldStateId);
    ++m_fluidCounts[stateId];

    inferType();

    // Fluid changes also require remesh
    m_dirty = true;
    ++m_meshRevision;
}

void SubChunk::optimizePalette() {
    std::vector<BlockStateId> usedIds;
    std::unordered_set<BlockStateId> seen;
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        const uint32_t paletteIdx = m_blockData.get(i);
        const BlockStateId stateId = m_palette.getStateId(paletteIdx);
        if (seen.insert(stateId).second) {
            usedIds.push_back(stateId);
        }
    }

    std::vector<uint32_t> oldToNew = m_palette.compact(usedIds);

    const uint8_t newBits = m_palette.bitsPerEntry();
    if (newBits != m_blockData.bitsPerEntry()) {
        std::vector<uint32_t> oldValues(BLOCK_COUNT);
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            const uint32_t oldIdx = m_blockData.get(i);
            oldValues[i] = oldToNew[oldIdx];
        }
        m_blockData.resize(newBits);
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            m_blockData.set(i, oldValues[i]);
        }
    } else {
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            const uint32_t oldIdx = m_blockData.get(i);
            m_blockData.set(i, oldToNew[oldIdx]);
        }
    }
}

void SubChunk::markDirty() {
    m_dirty = true;
    ++m_meshRevision;
}

uint64_t SubChunk::getMeshRevision() const {
    return m_meshRevision;
}

void SubChunk::markMeshClean() {
    m_dirty = false;
}

uint8_t SubChunk::getSunlight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    return static_cast<uint8_t>((m_lightMap[toIndex(x, y, z)] >> 4) & 0x0F);
}

void SubChunk::setSunlight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t clamped = clampLight(level);
    m_lightMap[index] = static_cast<uint8_t>((clamped << 4) | (m_lightMap[index] & 0x0F));
}

uint8_t SubChunk::getBlockLight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    return static_cast<uint8_t>(m_lightMap[toIndex(x, y, z)] & 0x0F);
}

void SubChunk::setBlockLight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t clamped = clampLight(level);
    m_lightMap[index] = static_cast<uint8_t>((m_lightMap[index] & 0xF0) | clamped);
}

void SubChunk::inferType() {
    if (m_fluidCounts.size() > 1 || m_fluidCounts.find(NULL_BLOCK_STATE) == m_fluidCounts.end()) {
        m_type = SubChunkType::Normal;
        return;
    }

    if (m_blockCounts.empty()) {
        m_type = SubChunkType::Air;
        return;
    }

    if (m_blockCounts.size() == 1) {
        const auto only = m_blockCounts.begin();
        const BlockStateId stateId = only->first;
        if (stateId == NULL_BLOCK_STATE) {
            m_type = SubChunkType::Air;
            return;
        }

        const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
        const BlockDef& def = BlockRegistry::getFast(blockId);
        if (only->second == BLOCK_COUNT && def.renderShape == BlockRenderShape::Cube && def.isSolid && !def.isTransparent) {
            m_type = SubChunkType::Solid;
            return;
        }
    }

    m_type = SubChunkType::Normal;
}

const SubChunkMesh& SubChunk::getMesh() const {
    return m_mesh;
}

SubChunkMesh& SubChunk::getMesh() {
    return m_mesh;
}

void SubChunk::setMesh(const SubChunkMesh& mesh) {
    m_mesh.destroy();
    m_mesh = mesh;
    m_mesh.metadataFingerprint = m_mesh.computeMetadataFingerprint();
    m_dirty = false;
}

std::size_t SubChunk::estimatedMemoryBytes() const {
    return sizeof(SubChunk) +
           m_blockData.allocatedByteSize() +
           m_fluidData.allocatedByteSize() +
           m_palette.dynamicMemoryBytes() +
           m_fluidPalette.dynamicMemoryBytes() +
           estimateUnorderedMapBytes(m_blockCounts) +
           estimateUnorderedMapBytes(m_fluidCounts);
}

// --- SubChunkMesh upload/destroy ---

namespace {
void setupSubChunkVertexLayout() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, u)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, sunlight)));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, blockLight)));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, ao)));

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));

    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFrameCount)));

    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animationFps)));

    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, animated)));

    glEnableVertexAttribArray(10);
    glVertexAttribIPointer(10, 1, GL_UNSIGNED_SHORT, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, tintPacked)));

    for (GLuint attrib = 11; attrib <= 14; ++attrib) {
        glDisableVertexAttribArray(attrib);
    }
}
} // namespace

void SubChunkMesh::upload(const std::vector<BlockVertex>& vertices) {
    vertexCount = static_cast<uint32_t>(vertices.size());

    if (vao == 0) {
        glGenVertexArrays(1, &vao);
    }
    if (vbo == 0) {
        glGenBuffers(1, &vbo);
    }

    const GLsizeiptr dataSize = static_cast<GLsizeiptr>(vertices.size() * sizeof(BlockVertex));

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    if (dataSize <= vboCapacity) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize,
                        vertices.empty() ? nullptr : vertices.data());
    } else {
        vboCapacity = dataSize + dataSize / 2;
        glBufferData(GL_ARRAY_BUFFER, vboCapacity, nullptr, GL_STATIC_DRAW);
        if (!vertices.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
        }
    }
    setupSubChunkVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SubChunkMesh::uploadCutout(const std::vector<BlockVertex>& cutoutVerts) {
    cutoutVertexCount = static_cast<uint32_t>(cutoutVerts.size());

    if (cutoutVao == 0) {
        glGenVertexArrays(1, &cutoutVao);
    }
    if (cutoutVbo == 0) {
        glGenBuffers(1, &cutoutVbo);
    }

    const GLsizeiptr dataSize = static_cast<GLsizeiptr>(cutoutVerts.size() * sizeof(BlockVertex));

    glBindVertexArray(cutoutVao);
    glBindBuffer(GL_ARRAY_BUFFER, cutoutVbo);

    if (dataSize <= cutoutVboCapacity) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize,
                        cutoutVerts.empty() ? nullptr : cutoutVerts.data());
    } else {
        cutoutVboCapacity = dataSize + dataSize / 2;
        glBufferData(GL_ARRAY_BUFFER, cutoutVboCapacity, nullptr, GL_STATIC_DRAW);
        if (!cutoutVerts.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, cutoutVerts.data());
        }
    }
    setupSubChunkVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SubChunkMesh::uploadCutoutDistance(const std::vector<BlockVertex>& cutoutDistanceVerts) {
    cutoutDistanceVertexCount = static_cast<uint32_t>(cutoutDistanceVerts.size());

    if (cutoutDistanceVao == 0) {
        glGenVertexArrays(1, &cutoutDistanceVao);
    }
    if (cutoutDistanceVbo == 0) {
        glGenBuffers(1, &cutoutDistanceVbo);
    }

    const GLsizeiptr dataSize = static_cast<GLsizeiptr>(cutoutDistanceVerts.size() * sizeof(BlockVertex));

    glBindVertexArray(cutoutDistanceVao);
    glBindBuffer(GL_ARRAY_BUFFER, cutoutDistanceVbo);

    if (dataSize <= cutoutDistanceVboCapacity) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize,
                        cutoutDistanceVerts.empty() ? nullptr : cutoutDistanceVerts.data());
    } else {
        cutoutDistanceVboCapacity = dataSize + dataSize / 2;
        glBufferData(GL_ARRAY_BUFFER, cutoutDistanceVboCapacity, nullptr, GL_STATIC_DRAW);
        if (!cutoutDistanceVerts.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, cutoutDistanceVerts.data());
        }
    }
    setupSubChunkVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SubChunkMesh::uploadTransparent(const std::vector<BlockVertex>& transparentVerts) {
    transparentVertexCount = static_cast<uint32_t>(transparentVerts.size());

    if (transparentVao == 0) {
        glGenVertexArrays(1, &transparentVao);
    }
    if (transparentVbo == 0) {
        glGenBuffers(1, &transparentVbo);
    }

    const GLsizeiptr dataSize = static_cast<GLsizeiptr>(transparentVerts.size() * sizeof(BlockVertex));

    glBindVertexArray(transparentVao);
    glBindBuffer(GL_ARRAY_BUFFER, transparentVbo);

    if (dataSize <= transparentVboCapacity) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize,
                        transparentVerts.empty() ? nullptr : transparentVerts.data());
    } else {
        transparentVboCapacity = dataSize + dataSize / 2;
        glBufferData(GL_ARRAY_BUFFER, transparentVboCapacity, nullptr, GL_STATIC_DRAW);
        if (!transparentVerts.empty()) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, transparentVerts.data());
        }
    }
    setupSubChunkVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

uint64_t SubChunkMesh::computeMetadataFingerprint() const {
    auto combine = [](uint64_t seed, const uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        return seed;
    };

    uint64_t hash = 1469598103934665603ULL;
    hash = combine(hash, vertexCount);
    hash = combine(hash, cutoutVertexCount);
    hash = combine(hash, cutoutDistanceVertexCount);
    hash = combine(hash, transparentVertexCount);
    hash = combine(hash, waterVertexCount);
    hash = combine(hash, opaqueRange.generation);
    hash = combine(hash, cutoutRange.generation);
    hash = combine(hash, cutoutDistanceRange.generation);
    hash = combine(hash, transparentRange.generation);
    hash = combine(hash, waterRange.generation);
    hash = combine(hash, hasBounds ? 1ULL : 0ULL);
    return hash;
}

void SubChunkMesh::destroy() {
    if (inGlobalPool) {
        // GPU memory is owned by WorldRenderBuffer — just clear the handles.
        opaqueRange = {};
        cutoutRange = {};
        cutoutDistanceRange = {};
        transparentRange = {};
        waterRange = {};
        vertexCount = 0;
        cutoutVertexCount = 0;
        cutoutDistanceVertexCount = 0;
        transparentVertexCount = 0;
        waterVertexCount = 0;
        hasBounds = false;
        inGlobalPool = false;
        metadataFingerprint = 0;
        return;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    vboCapacity = 0;
    if (transparentVbo != 0) {
        glDeleteBuffers(1, &transparentVbo);
        transparentVbo = 0;
    }
    if (transparentVao != 0) {
        glDeleteVertexArrays(1, &transparentVao);
        transparentVao = 0;
    }
    transparentVboCapacity = 0;
    if (cutoutVbo != 0) {
        glDeleteBuffers(1, &cutoutVbo);
        cutoutVbo = 0;
    }
    if (cutoutVao != 0) {
        glDeleteVertexArrays(1, &cutoutVao);
        cutoutVao = 0;
    }
    cutoutVboCapacity = 0;
    if (cutoutDistanceVbo != 0) {
        glDeleteBuffers(1, &cutoutDistanceVbo);
        cutoutDistanceVbo = 0;
    }
    if (cutoutDistanceVao != 0) {
        glDeleteVertexArrays(1, &cutoutDistanceVao);
        cutoutDistanceVao = 0;
    }
    cutoutDistanceVboCapacity = 0;

    vertexCount = 0;
    transparentVertexCount = 0;
    waterVertexCount = 0;
    cutoutVertexCount = 0;
    cutoutDistanceVertexCount = 0;
    waterRange = {};
    hasBounds = false;
    boundsMin = glm::vec3(0.0f);
    boundsMax = glm::vec3(0.0f);
    metadataFingerprint = 0;
}
