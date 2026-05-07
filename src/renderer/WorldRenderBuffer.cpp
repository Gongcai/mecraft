#include "WorldRenderBuffer.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// VertexPoolAllocator
// ---------------------------------------------------------------------------

VertexPoolAllocator::VertexPoolAllocator() = default;

VertexPoolAllocator::~VertexPoolAllocator() {
    shutdown();
}

void VertexPoolAllocator::init(const size_t initialCapacityVertices) {
    shutdown();

    const size_t bytes = initialCapacityVertices * sizeof(BlockVertex);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_capacityVertices = initialCapacityVertices;
    m_usedVertices = 0;
    m_generationCounter = 1;

    m_freeBlocks.resize(kFreeBlockPoolSize);
    m_freeBlockFreeList.clear();
    for (size_t i = 0; i < kFreeBlockPoolSize; ++i) {
        m_freeBlockFreeList.push_back(static_cast<int>(kFreeBlockPoolSize - 1 - i));
    }

    // Initial free block covering the whole buffer
    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {0, static_cast<uint32_t>(initialCapacityVertices), -1};
    m_freeHead = node;
}

void VertexPoolAllocator::shutdown() {
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_freeBlocks.clear();
    m_freeBlockFreeList.clear();
    m_freeHead = -1;
    m_capacityVertices = 0;
    m_usedVertices = 0;
    m_activeRanges.clear();
    m_liveAllocations.clear();
}

int VertexPoolAllocator::allocFreeBlockNode() {
    if (m_freeBlockFreeList.empty()) {
        const int idx = static_cast<int>(m_freeBlocks.size());
        m_freeBlocks.emplace_back();
        return idx;
    }
    const int idx = m_freeBlockFreeList.back();
    m_freeBlockFreeList.pop_back();
    return idx;
}

void VertexPoolAllocator::returnFreeBlockNode(const int nodeIdx) {
    m_freeBlockFreeList.push_back(nodeIdx);
}

void VertexPoolAllocator::coalesceAt(const int nodeIdx) {
    (void)nodeIdx;

    std::vector<FreeBlock> blocks;
    int curr = m_freeHead;
    while (curr != -1) {
        blocks.push_back(m_freeBlocks[curr]);
        const int next = m_freeBlocks[curr].next;
        returnFreeBlockNode(curr);
        curr = next;
    }
    m_freeHead = -1;

    std::sort(blocks.begin(), blocks.end(),
              [](const FreeBlock& a, const FreeBlock& b) {
                  return a.offset < b.offset;
              });

    std::vector<FreeBlock> merged;
    for (const FreeBlock& block : blocks) {
        if (block.size == 0) {
            continue;
        }
        if (!merged.empty() && merged.back().offset + merged.back().size >= block.offset) {
            const uint32_t blockEnd = block.offset + block.size;
            const uint32_t mergedEnd = merged.back().offset + merged.back().size;
            merged.back().size = std::max(mergedEnd, blockEnd) - merged.back().offset;
        } else {
            merged.push_back({block.offset, block.size, -1});
        }
    }

    for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
        const int node = allocFreeBlockNode();
        m_freeBlocks[node] = {it->offset, it->size, m_freeHead};
        m_freeHead = node;
    }
}

bool VertexPoolAllocator::allocate(const uint32_t vertexCount, GpuMeshRange& outRange) {
    if (vertexCount == 0) {
        outRange = {};
        return true;
    }

    // First-fit search
    int prev = -1;
    int curr = m_freeHead;
    while (curr != -1) {
        FreeBlock& block = m_freeBlocks[curr];
        if (block.size >= vertexCount) {
            const uint32_t offset = block.offset;
            block.offset += vertexCount;
            block.size -= vertexCount;

            if (block.size == 0) {
                if (prev == -1) {
                    m_freeHead = block.next;
                } else {
                    m_freeBlocks[prev].next = block.next;
                }
                returnFreeBlockNode(curr);
            }

            m_usedVertices += vertexCount;
            outRange = {offset, vertexCount, m_generationCounter++};
            m_liveAllocations.insert(outRange.generation);
            return true;
        }
        prev = curr;
        curr = block.next;
    }

    // No block fits. Only compact when active ranges are registered; otherwise
    // resetting the free list would let new uploads overwrite meshes still in use.
    if (!m_activeRanges.empty() || m_usedVertices == 0) {
        defragment();
    }
    if (m_freeHead != -1) {
        FreeBlock& head = m_freeBlocks[m_freeHead];
        if (head.size >= vertexCount) {
            const uint32_t offset = head.offset;
            head.offset += vertexCount;
            head.size -= vertexCount;
            if (head.size == 0) {
                const int next = head.next;
                returnFreeBlockNode(m_freeHead);
                m_freeHead = next;
            }
            m_usedVertices += vertexCount;
            outRange = {offset, vertexCount, m_generationCounter++};
            m_liveAllocations.insert(outRange.generation);
            return true;
        }
    }

    // Expand
    const size_t newCapacity = std::max<size_t>(
        m_capacityVertices + vertexCount,
        m_capacityVertices == 0 ? vertexCount : m_capacityVertices * 2);
    expand(newCapacity);

    if (m_freeHead != -1) {
        FreeBlock& head = m_freeBlocks[m_freeHead];
        if (head.size >= vertexCount) {
            const uint32_t offset = head.offset;
            head.offset += vertexCount;
            head.size -= vertexCount;
            if (head.size == 0) {
                const int next = head.next;
                returnFreeBlockNode(m_freeHead);
                m_freeHead = next;
            }
            m_usedVertices += vertexCount;
            outRange = {offset, vertexCount, m_generationCounter++};
            m_liveAllocations.insert(outRange.generation);
            return true;
        }
    }

    return false;
}

void VertexPoolAllocator::free(const GpuMeshRange& range) {
    if (range.vertexCount == 0 || range.firstVertex + range.vertexCount > m_capacityVertices) {
        return;
    }
    if (m_liveAllocations.erase(range.generation) == 0) {
        return;
    }

    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {range.firstVertex, range.vertexCount, m_freeHead};
    m_freeHead = node;
    m_usedVertices = range.vertexCount <= m_usedVertices ? m_usedVertices - range.vertexCount : 0;

    coalesceAt(node);
}

void VertexPoolAllocator::upload(const GpuMeshRange& range, const std::vector<BlockVertex>& vertices) {
    if (vertices.empty()) return;
    const size_t bytes = vertices.size() * sizeof(BlockVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(range.firstVertex) * sizeof(BlockVertex),
                    static_cast<GLsizeiptr>(bytes),
                    vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexPoolAllocator::expand(const size_t newCapacityVertices) {
    if (newCapacityVertices <= m_capacityVertices) return;

    GLuint newVbo = 0;
    glGenBuffers(1, &newVbo);
    glBindBuffer(GL_ARRAY_BUFFER, newVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(newCapacityVertices * sizeof(BlockVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    // Copy old data
    if (m_vbo != 0 && m_capacityVertices > 0) {
        glBindBuffer(GL_COPY_READ_BUFFER, m_vbo);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_ARRAY_BUFFER, 0, 0,
                            static_cast<GLsizeiptr>(m_capacityVertices * sizeof(BlockVertex)));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glDeleteBuffers(1, &m_vbo);
    }

    m_vbo = newVbo;
    const size_t oldCapacity = m_capacityVertices;
    m_capacityVertices = newCapacityVertices;

    // Add the new trailing space as a free block
    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {static_cast<uint32_t>(oldCapacity),
                          static_cast<uint32_t>(newCapacityVertices - oldCapacity),
                          m_freeHead};
    m_freeHead = node;
    coalesceAt(node);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexPoolAllocator::defragment() {
    if (m_activeRanges.empty()) {
        // No active ranges — reset the whole buffer as one free block
        while (m_freeHead != -1) {
            const int next = m_freeBlocks[m_freeHead].next;
            returnFreeBlockNode(m_freeHead);
            m_freeHead = next;
        }
        const int node = allocFreeBlockNode();
        m_freeBlocks[node] = {0, static_cast<uint32_t>(m_capacityVertices), -1};
        m_freeHead = node;
        return;
    }

    // Sort active ranges by offset
    std::sort(m_activeRanges.begin(), m_activeRanges.end(),
              [](const GpuMeshRange* a, const GpuMeshRange* b) {
                  return a->firstVertex < b->firstVertex;
              });

    // Compact: move each block to its new position
    uint32_t writeOffset = 0;
    for (GpuMeshRange* range : m_activeRanges) {
        if (range->firstVertex != writeOffset) {
            // Copy vertex data to new position
            glBindBuffer(GL_COPY_READ_BUFFER, m_vbo);
            glBindBuffer(GL_COPY_WRITE_BUFFER, m_vbo);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                                static_cast<GLintptr>(range->firstVertex) * sizeof(BlockVertex),
                                static_cast<GLintptr>(writeOffset) * sizeof(BlockVertex),
                                static_cast<GLsizeiptr>(range->vertexCount) * sizeof(BlockVertex));
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            range->firstVertex = writeOffset;
        }
        writeOffset += range->vertexCount;
    }

    // Rebuild free list as one block after the last used vertex
    while (m_freeHead != -1) {
        const int next = m_freeBlocks[m_freeHead].next;
        returnFreeBlockNode(m_freeHead);
        m_freeHead = next;
    }
    if (writeOffset < m_capacityVertices) {
        const int node = allocFreeBlockNode();
        m_freeBlocks[node] = {writeOffset, static_cast<uint32_t>(m_capacityVertices - writeOffset), -1};
        m_freeHead = node;
    }
}

float VertexPoolAllocator::fragmentationRatio() const {
    if (m_capacityVertices == 0) return 0.0f;
    size_t freeVertices = m_capacityVertices - m_usedVertices;
    if (freeVertices == 0) return 0.0f;

    // Largest free block
    uint32_t largestBlock = 0;
    int curr = m_freeHead;
    while (curr != -1) {
        largestBlock = std::max(largestBlock, m_freeBlocks[curr].size);
        curr = m_freeBlocks[curr].next;
    }

    if (largestBlock == 0) return 1.0f;
    return 1.0f - static_cast<float>(largestBlock) / static_cast<float>(freeVertices);
}

void VertexPoolAllocator::registerRange(GpuMeshRange* range) {
    m_activeRanges.push_back(range);
}

void VertexPoolAllocator::unregisterRange(GpuMeshRange* range) {
    auto it = std::find(m_activeRanges.begin(), m_activeRanges.end(), range);
    if (it != m_activeRanges.end()) {
        *it = m_activeRanges.back();
        m_activeRanges.pop_back();
    }
}

// ---------------------------------------------------------------------------
// WorldRenderBuffer
// ---------------------------------------------------------------------------

WorldRenderBuffer::WorldRenderBuffer() = default;

WorldRenderBuffer::~WorldRenderBuffer() {
    shutdown();
}

void WorldRenderBuffer::setupVertexLayout() {
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
}

void WorldRenderBuffer::init() {
    m_opaquePool.init(kInitialPoolVertices);
    m_cutoutPool.init(kInitialPoolVertices / 4);
    m_transparentPool.init(kInitialPoolVertices / 4);

    // Create three VAOs (one per pool)
    glGenVertexArrays(1, &m_opaqueVao);
    glBindVertexArray(m_opaqueVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_opaquePool.vbo());
    setupVertexLayout();
    m_opaqueVaoBoundVbo = m_opaquePool.vbo();
    glBindVertexArray(0);

    glGenVertexArrays(1, &m_cutoutVao);
    glBindVertexArray(m_cutoutVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cutoutPool.vbo());
    setupVertexLayout();
    m_cutoutVaoBoundVbo = m_cutoutPool.vbo();
    glBindVertexArray(0);

    glGenVertexArrays(1, &m_transparentVao);
    glBindVertexArray(m_transparentVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_transparentPool.vbo());
    setupVertexLayout();
    m_transparentVaoBoundVbo = m_transparentPool.vbo();
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Create indirect buffers
    glGenBuffers(1, &m_opaqueIndirectBuf);
    glGenBuffers(1, &m_cutoutIndirectBuf);
    glGenBuffers(1, &m_transparentIndirectBuf);

    m_opaqueIndirectCapacity = kInitialIndirectCapacity;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_opaqueIndirectBuf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 static_cast<GLsizeiptr>(kInitialIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                 nullptr, GL_DYNAMIC_DRAW);

    m_cutoutIndirectCapacity = kInitialIndirectCapacity;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_cutoutIndirectBuf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 static_cast<GLsizeiptr>(kInitialIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                 nullptr, GL_DYNAMIC_DRAW);

    m_transparentIndirectCapacity = kInitialIndirectCapacity;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_transparentIndirectBuf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 static_cast<GLsizeiptr>(kInitialIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                 nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    m_opaqueCommands.reserve(kInitialIndirectCapacity);
    m_cutoutCommands.reserve(kInitialIndirectCapacity);
    m_transparentCommands.reserve(kInitialIndirectCapacity);
}

void WorldRenderBuffer::shutdown() {
    m_opaquePool.shutdown();
    m_cutoutPool.shutdown();
    m_transparentPool.shutdown();

    if (m_opaqueVao != 0) { glDeleteVertexArrays(1, &m_opaqueVao); m_opaqueVao = 0; }
    if (m_cutoutVao != 0) { glDeleteVertexArrays(1, &m_cutoutVao); m_cutoutVao = 0; }
    if (m_transparentVao != 0) { glDeleteVertexArrays(1, &m_transparentVao); m_transparentVao = 0; }
    m_opaqueVaoBoundVbo = 0;
    m_cutoutVaoBoundVbo = 0;
    m_transparentVaoBoundVbo = 0;

    if (m_opaqueIndirectBuf != 0) { glDeleteBuffers(1, &m_opaqueIndirectBuf); m_opaqueIndirectBuf = 0; }
    if (m_cutoutIndirectBuf != 0) { glDeleteBuffers(1, &m_cutoutIndirectBuf); m_cutoutIndirectBuf = 0; }
    if (m_transparentIndirectBuf != 0) { glDeleteBuffers(1, &m_transparentIndirectBuf); m_transparentIndirectBuf = 0; }

    m_opaqueIndirectCapacity = 0;
    m_cutoutIndirectCapacity = 0;
    m_transparentIndirectCapacity = 0;
}

WorldGpuMesh WorldRenderBuffer::uploadSubChunk(
    const std::vector<BlockVertex>& opaque,
    const std::vector<BlockVertex>& cutout,
    const std::vector<BlockVertex>& transparent,
    const bool hasBounds, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
{
    WorldGpuMesh result;

    if (!opaque.empty()) {
        if (!m_opaquePool.allocate(static_cast<uint32_t>(opaque.size()), result.opaque)) {
            return {};
        }
        m_opaquePool.upload(result.opaque, opaque);
    }
    if (!cutout.empty()) {
        if (!m_cutoutPool.allocate(static_cast<uint32_t>(cutout.size()), result.cutout)) {
            m_opaquePool.free(result.opaque);
            return {};
        }
        m_cutoutPool.upload(result.cutout, cutout);
    }
    if (!transparent.empty()) {
        if (!m_transparentPool.allocate(static_cast<uint32_t>(transparent.size()), result.transparent)) {
            m_opaquePool.free(result.opaque);
            m_cutoutPool.free(result.cutout);
            return {};
        }
        m_transparentPool.upload(result.transparent, transparent);
    }

    result.hasBounds = hasBounds;
    result.boundsMin = boundsMin;
    result.boundsMax = boundsMax;
    return result;
}

void WorldRenderBuffer::free(const WorldGpuMesh& mesh) {
    m_opaquePool.free(mesh.opaque);
    m_cutoutPool.free(mesh.cutout);
    m_transparentPool.free(mesh.transparent);
}

void WorldRenderBuffer::beginFrame() {
    m_opaqueCommands.clear();
    m_cutoutCommands.clear();
    m_transparentCommands.clear();
    m_glSubmitCount = 0;
    m_opaqueVertexCount = 0;
    m_cutoutVertexCount = 0;
    m_transparentVertexCount = 0;
}

void WorldRenderBuffer::addOpaque(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_opaqueCommands.push_back({range.vertexCount, 1, range.firstVertex, 0});
    m_opaqueVertexCount += range.vertexCount;
}

void WorldRenderBuffer::addCutout(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_cutoutCommands.push_back({range.vertexCount, 1, range.firstVertex, 0});
    m_cutoutVertexCount += range.vertexCount;
}

void WorldRenderBuffer::addTransparent(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_transparentCommands.push_back({range.vertexCount, 1, range.firstVertex, 0});
    m_transparentVertexCount += range.vertexCount;
}

void WorldRenderBuffer::flushOpaque() {
    flushPass(m_opaqueCommands, m_opaqueIndirectBuf, m_opaqueVao, m_opaquePool.vbo(), m_opaqueVaoBoundVbo);
}

void WorldRenderBuffer::flushCutout() {
    flushPass(m_cutoutCommands, m_cutoutIndirectBuf, m_cutoutVao, m_cutoutPool.vbo(), m_cutoutVaoBoundVbo);
}

void WorldRenderBuffer::flushTransparent() {
    flushPass(m_transparentCommands, m_transparentIndirectBuf, m_transparentVao, m_transparentPool.vbo(), m_transparentVaoBoundVbo);
}

void WorldRenderBuffer::ensureVaoVertexBuffer(const GLuint vao, const GLuint vbo, GLuint& cachedVbo) {
    if (cachedVbo == vbo) {
        return;
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    setupVertexLayout();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    cachedVbo = vbo;
}

void WorldRenderBuffer::ensureIndirectCapacity(std::vector<DrawArraysIndirectCommand>& commands,
                                                GLuint& buf, size_t& capacity, const size_t needed) {
    if (needed <= capacity) return;
    capacity = std::max(needed, capacity * 2);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,
                 static_cast<GLsizeiptr>(capacity * sizeof(DrawArraysIndirectCommand)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void WorldRenderBuffer::flushPass(std::vector<DrawArraysIndirectCommand>& commands,
                                   GLuint indirectBuf, GLuint vao, GLuint vbo, GLuint& cachedVbo) {
    if (commands.empty()) return;

    ensureIndirectCapacity(commands, indirectBuf,
                           vao == m_opaqueVao ? m_opaqueIndirectCapacity :
                           vao == m_cutoutVao ? m_cutoutIndirectCapacity :
                           m_transparentIndirectCapacity,
                           commands.size());

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuf);
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                    static_cast<GLsizeiptr>(commands.size() * sizeof(DrawArraysIndirectCommand)),
                    commands.data());

    ensureVaoVertexBuffer(vao, vbo, cachedVbo);
    glBindVertexArray(vao);
    glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr, static_cast<GLsizei>(commands.size()), 0);
    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    ++m_glSubmitCount;
}
