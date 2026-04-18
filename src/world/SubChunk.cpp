#include "SubChunk.h"

#include <algorithm>
#include <unordered_set>

#include <glad/glad.h>

namespace {
uint8_t clampLight(const uint8_t level) {
    return static_cast<uint8_t>(std::min<int>(level, 15));
}
} // namespace

SubChunk::SubChunk() {
    m_palette.getOrCreateIndex(0);  // AIR = RuntimeId 0
    m_blockData = BitPackedArray(BLOCK_COUNT, 2);
    m_blockData.fill(0);  // All air
    m_lightMap.fill(0);
    m_blockCounts.emplace(0, static_cast<uint16_t>(BLOCK_COUNT));
}

SubChunk::~SubChunk() {
    m_mesh.destroy();
}

SubChunk::SubChunk(SubChunk&& other) noexcept
    : m_palette(std::move(other.m_palette))
    , m_blockData(std::move(other.m_blockData))
    , m_blockCounts(std::move(other.m_blockCounts))
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

BlockID SubChunk::getBlock(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;  // AIR
    }
    const size_t idx = toIndex(x, y, z);
    const uint8_t paletteIdx = static_cast<uint8_t>(m_blockData.get(idx));
    return m_palette.getRuntimeId(paletteIdx);
}

void SubChunk::setBlockImpl(const int x, const int y, const int z, const BlockID id, const bool markMeshDirty) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t oldPaletteIdx = static_cast<uint8_t>(m_blockData.get(index));
    const BlockID oldId = m_palette.getRuntimeId(oldPaletteIdx);
    if (oldId == id) {
        return;
    }

    const uint8_t paletteIdx = m_palette.getOrCreateIndex(id);
    const uint8_t neededBits = m_palette.bitsPerEntry();
    if (neededBits > m_blockData.bitsPerEntry()) {
        m_blockData.resize(neededBits);
    }

    m_blockData.set(index, paletteIdx);

    auto decrementCount = [&](const BlockID blockId) {
        auto it = m_blockCounts.find(blockId);
        if (it == m_blockCounts.end()) {
            return;
        }
        if (it->second <= 1) {
            m_blockCounts.erase(it);
        } else {
            --it->second;
        }
    };
    decrementCount(oldId);
    ++m_blockCounts[id];

    inferType();
    if (markMeshDirty) {
        m_dirty = true;
        ++m_meshRevision;
    }
}

void SubChunk::setBlock(const int x, const int y, const int z, const BlockID id) {
    setBlockImpl(x, y, z, id, true);
}

void SubChunk::setBlockWithoutMeshDirty(const int x, const int y, const int z, const BlockID id) {
    setBlockImpl(x, y, z, id, false);
}

void SubChunk::setBlockFast(const int x, const int y, const int z, const BlockID id) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t oldPaletteIdx = static_cast<uint8_t>(m_blockData.get(index));
    const BlockID oldId = m_palette.getRuntimeId(oldPaletteIdx);
    if (oldId == id) {
        return;
    }

    const uint8_t paletteIdx = m_palette.getOrCreateIndex(id);
    const uint8_t neededBits = m_palette.bitsPerEntry();
    if (neededBits > m_blockData.bitsPerEntry()) {
        m_blockData.resize(neededBits);
    }

    m_blockData.set(index, paletteIdx);

    auto decrementCount = [&](const BlockID blockId) {
        auto it = m_blockCounts.find(blockId);
        if (it == m_blockCounts.end()) {
            return;
        }
        if (it->second <= 1) {
            m_blockCounts.erase(it);
        } else {
            --it->second;
        }
    };
    decrementCount(oldId);
    ++m_blockCounts[id];
}

void SubChunk::initializeFromBlocks(const std::array<BlockID, BLOCK_COUNT>& blocks) {
    m_palette.clear();
    m_blockCounts.clear();

    std::array<uint8_t, BLOCK_COUNT> paletteIndices{};
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        const BlockID id = blocks[index];
        paletteIndices[index] = m_palette.getOrCreateIndex(id);
        ++m_blockCounts[id];
    }

    m_blockData = BitPackedArray(BLOCK_COUNT, m_palette.bitsPerEntry());
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        m_blockData.set(index, paletteIndices[index]);
    }

    inferType();
    m_dirty = true;
}

void SubChunk::copyBlocksTo(std::array<BlockID, BLOCK_COUNT>& out) const {
    for (size_t index = 0; index < BLOCK_COUNT; ++index) {
        const uint8_t paletteIdx = static_cast<uint8_t>(m_blockData.get(index));
        out[index] = m_palette.getRuntimeId(paletteIdx);
    }
}

void SubChunk::optimizePalette() {
    std::vector<RuntimeId> usedIds;
    std::unordered_set<RuntimeId> seen;
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        uint8_t paletteIdx = static_cast<uint8_t>(m_blockData.get(i));
        RuntimeId runtimeId = m_palette.getRuntimeId(paletteIdx);
        if (seen.insert(runtimeId).second) {
            usedIds.push_back(runtimeId);
        }
    }

    std::vector<uint8_t> oldToNew = m_palette.compact(usedIds);

    const uint8_t newBits = m_palette.bitsPerEntry();
    if (newBits != m_blockData.bitsPerEntry()) {
        std::vector<uint32_t> oldValues(BLOCK_COUNT);
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            uint8_t oldIdx = static_cast<uint8_t>(m_blockData.get(i));
            oldValues[i] = oldToNew[oldIdx];
        }
        m_blockData.resize(newBits);
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            m_blockData.set(i, oldValues[i]);
        }
    } else {
        for (size_t i = 0; i < BLOCK_COUNT; ++i) {
            uint8_t oldIdx = static_cast<uint8_t>(m_blockData.get(i));
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
    if (m_blockCounts.empty()) {
        m_type = SubChunkType::Air;
        return;
    }

    if (m_blockCounts.size() == 1) {
        const auto only = m_blockCounts.begin();
        const BlockID blockId = only->first;
        if (blockId == 0) {
            m_type = SubChunkType::Air;
            return;
        }

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
    m_dirty = false;
}

// --- SubChunkMesh upload/destroy ---

namespace {
void setupSubChunkVertexLayout() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, u)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, sunlight)));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, blockLight)));

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, ao)));

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, layer)));
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

void SubChunkMesh::destroy() {
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

    vertexCount = 0;
    transparentVertexCount = 0;
    cutoutVertexCount = 0;
    hasBounds = false;
    boundsMin = glm::vec3(0.0f);
    boundsMax = glm::vec3(0.0f);
}
