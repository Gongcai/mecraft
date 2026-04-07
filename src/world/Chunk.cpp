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

void setupVertexLayout() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, u)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(BlockVertex), reinterpret_cast<void*>(offsetof(BlockVertex, normal)));
}
}

Chunk::Chunk(const int chunkX, const int chunkZ) : m_chunkX(chunkX), m_chunkZ(chunkZ) {
    m_blocks.fill(BlockType::AIR);
    m_lightMap.fill(0);
}

Chunk::~Chunk() {
    m_mesh.destroy();
}

bool Chunk::isInBounds(const int x, const int y, const int z) {
    return x >= 0 && x < SIZE_X && y >= 0 && y < SIZE_Y && z >= 0 && z < SIZE_Z;
}

size_t Chunk::toIndex(const int x, const int y, const int z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(z) * SIZE_X +
           static_cast<size_t>(y) * SIZE_X * SIZE_Z;
}

BlockID Chunk::getBlock(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return BlockType::AIR;
    }
    return m_blocks[toIndex(x, y, z)];
}

void Chunk::setBlock(const int x, const int y, const int z, const BlockID id) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    if (m_blocks[index] == id) {
        return;
    }

    m_blocks[index] = id;
    m_dirty = true;
    ++m_meshRevision;
}

glm::ivec3 Chunk::worldToLocal(const int wx, const int wy, const int wz) {
    return {wrapToChunkAxis(wx), wy, wrapToChunkAxis(wz)};
}

glm::ivec3 Chunk::getWorldOffset() const {
    return {m_chunkX * SIZE_X, 0, m_chunkZ * SIZE_Z};
}

bool Chunk::isDirty() const {
    return m_dirty;
}

void Chunk::markDirty() {
    m_dirty = true;
    ++m_meshRevision;
}

uint64_t Chunk::getMeshRevision() const {
    return m_meshRevision;
}

void Chunk::setMesh(const ChunkMesh& mesh) {
    m_mesh.destroy();
    m_mesh = mesh;
    m_dirty = false;
}

const ChunkMesh& Chunk::getMesh() const {
    return m_mesh;
}

ChunkMesh& Chunk::getMesh() {
    return m_mesh;
}

uint8_t Chunk::getSunlight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    return static_cast<uint8_t>((m_lightMap[toIndex(x, y, z)] >> 4) & 0x0F);
}

void Chunk::setSunlight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t clamped = clampLight(level);
    m_lightMap[index] = static_cast<uint8_t>((clamped << 4) | (m_lightMap[index] & 0x0F));
}

uint8_t Chunk::getBlockLight(const int x, const int y, const int z) const {
    if (!isInBounds(x, y, z)) {
        return 0;
    }
    return static_cast<uint8_t>(m_lightMap[toIndex(x, y, z)] & 0x0F);
}

void Chunk::setBlockLight(const int x, const int y, const int z, const uint8_t level) {
    if (!isInBounds(x, y, z)) {
        return;
    }

    const size_t index = toIndex(x, y, z);
    const uint8_t clamped = clampLight(level);
    m_lightMap[index] = static_cast<uint8_t>((m_lightMap[index] & 0xF0) | clamped);
}

void ChunkMesh::upload(const std::vector<BlockVertex>& vertices) {
    vertexCount = static_cast<uint32_t>(vertices.size());

    if (vao == 0) {
        glGenVertexArrays(1, &vao);
    }
    if (vbo == 0) {
        glGenBuffers(1, &vbo);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(BlockVertex)),
                 vertices.empty() ? nullptr : vertices.data(),
                 GL_STATIC_DRAW);
    setupVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ChunkMesh::uploadTransparent(const std::vector<BlockVertex>& transparentVerts) {
    transparentVertexCount = static_cast<uint32_t>(transparentVerts.size());

    if (transparentVao == 0) {
        glGenVertexArrays(1, &transparentVao);
    }
    if (transparentVbo == 0) {
        glGenBuffers(1, &transparentVbo);
    }

    glBindVertexArray(transparentVao);
    glBindBuffer(GL_ARRAY_BUFFER, transparentVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(transparentVerts.size() * sizeof(BlockVertex)),
                 transparentVerts.empty() ? nullptr : transparentVerts.data(),
                 GL_STATIC_DRAW);
    setupVertexLayout();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ChunkMesh::destroy() {
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (transparentVbo != 0) {
        glDeleteBuffers(1, &transparentVbo);
        transparentVbo = 0;
    }
    if (transparentVao != 0) {
        glDeleteVertexArrays(1, &transparentVao);
        transparentVao = 0;
    }

    vertexCount = 0;
    transparentVertexCount = 0;
    hasBounds = false;
    boundsMin = glm::vec3(0.0f);
    boundsMax = glm::vec3(0.0f);
}

