#include "WorldRenderBuffer.h"
#include "../debug/RenderDebugLabels.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
constexpr uint32_t kInvalidMetadataIndex = 0xFFFFFFFFu;
constexpr float kPackedPositionScale = 128.0f;
constexpr float kPackedUvScale = 1024.0f;

uint32_t packFixedUv16(const float value) {
    const float clamped = std::clamp(value, -32768.0f / kPackedUvScale, 32767.0f / kPackedUvScale);
    const int quantized = static_cast<int>(std::lround(clamped * kPackedUvScale));
    return static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(quantized)));
}

uint32_t packSignedNormal(const int8_t normal) {
    if (normal == -1) {
        return 6u;
    }
    if (normal == -2) {
        return 7u;
    }
    return static_cast<uint32_t>(std::clamp<int>(normal, 0, 5));
}

uint32_t packLocalCoord12(const float value) {
    const int quantized = static_cast<int>(std::lround(value * kPackedPositionScale));
    return static_cast<uint32_t>(std::clamp(quantized, 0, 4095));
}

std::vector<PackedBlockVertex> packBlockVertices(const std::vector<BlockVertex>& vertices) {
    std::vector<PackedBlockVertex> packed;
    packed.reserve(vertices.size());
    for (const BlockVertex& v : vertices) {
        const uint32_t x = packLocalCoord12(v.x);
        const uint32_t y = packLocalCoord12(v.y);
        const uint32_t z = packLocalCoord12(v.z);

        PackedBlockVertex out{};
        out.posPacked =
            ((x & 0x0FFFu) << 20u) |
            ((y & 0x0FFFu) << 8u) |
            ((z >> 4u) & 0xFFu);
        out.uvPacked = (packFixedUv16(v.u) << 16u) | packFixedUv16(v.v);
        out.lightAoLayer =
            ((z & 0x0Fu) << 28u) |
            (packSignedNormal(v.normal) << 25u) |
            ((static_cast<uint32_t>(v.sunlight) & 0xFFu) << 17u) |
            ((static_cast<uint32_t>(v.blockLight) & 0xFFu) << 9u) |
            ((static_cast<uint32_t>(v.ao) & 0x03u) << 7u) |
            (static_cast<uint32_t>(v.layer) & 0x7Fu);
        out.tintAnim =
            ((static_cast<uint32_t>(v.animationFrameCount) & 0x3Fu) << 26u) |
            ((static_cast<uint32_t>(v.animationFps) & 0x3Fu) << 20u) |
            ((static_cast<uint32_t>(v.animated) & 0x01u) << 19u) |
            (((static_cast<uint32_t>(v.layer) >> 7u) & 0x07u) << 16u) |
            (static_cast<uint32_t>(v.tintPacked) & 0xFFFFu);
        packed.push_back(out);
    }
    return packed;
}

void subtractOrigin(std::vector<BlockVertex>& vertices, const glm::vec3& origin) {
    for (BlockVertex& v : vertices) {
        v.x -= origin.x;
        v.y -= origin.y;
        v.z -= origin.z;
    }
}

}

// ---------------------------------------------------------------------------
// VertexRangeAllocator
// ---------------------------------------------------------------------------

void VertexRangeAllocator::init(const size_t capacityVertices) {
    shutdown();
    m_capacityVertices = capacityVertices;

    m_freeBlocks.resize(kFreeBlockPoolSize);
    m_freeBlockFreeList.clear();
    for (size_t i = 0; i < kFreeBlockPoolSize; ++i) {
        m_freeBlockFreeList.push_back(static_cast<int>(kFreeBlockPoolSize - 1 - i));
    }

    // The initial free block covers the complete logical range.
    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {0, static_cast<uint32_t>(capacityVertices), -1};
    m_freeHead = node;
}

void VertexRangeAllocator::shutdown() {
    m_freeBlocks.clear();
    m_freeBlockFreeList.clear();
    m_freeHead = -1;
    m_capacityVertices = 0;
    m_usedVertices = 0;
    m_generationCounter = 1;
    m_liveAllocations.clear();
}

int VertexRangeAllocator::allocFreeBlockNode() {
    if (m_freeBlockFreeList.empty()) {
        const int idx = static_cast<int>(m_freeBlocks.size());
        m_freeBlocks.emplace_back();
        return idx;
    }
    const int idx = m_freeBlockFreeList.back();
    m_freeBlockFreeList.pop_back();
    return idx;
}

void VertexRangeAllocator::returnFreeBlockNode(const int nodeIdx) {
    m_freeBlockFreeList.push_back(nodeIdx);
}

void VertexRangeAllocator::coalesce() {
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

bool VertexRangeAllocator::allocate(const uint32_t vertexCount, GpuMeshRange& outRange) {
    if (vertexCount == 0) {
        outRange = {};
        return true;
    }

    // First-fit keeps allocation cost bounded by the current free-list length.
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

    return false;
}

void VertexRangeAllocator::free(const GpuMeshRange& range) {
    if (range.vertexCount == 0 ||
        static_cast<size_t>(range.firstVertex) + range.vertexCount > m_capacityVertices) {
        return;
    }
    if (m_liveAllocations.erase(range.generation) == 0) {
        return;
    }

    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {range.firstVertex, range.vertexCount, m_freeHead};
    m_freeHead = node;
    m_usedVertices = range.vertexCount <= m_usedVertices ? m_usedVertices - range.vertexCount : 0;

    coalesce();
}

void VertexRangeAllocator::grow(const size_t newCapacityVertices) {
    if (newCapacityVertices <= m_capacityVertices) {
        return;
    }
    const size_t oldCapacity = m_capacityVertices;
    m_capacityVertices = newCapacityVertices;

    const int node = allocFreeBlockNode();
    m_freeBlocks[node] = {
        static_cast<uint32_t>(oldCapacity),
        static_cast<uint32_t>(newCapacityVertices - oldCapacity),
        m_freeHead
    };
    m_freeHead = node;
    coalesce();
}

float VertexRangeAllocator::fragmentationRatio() const {
    if (m_capacityVertices == 0) return 0.0f;
    const size_t freeVertices = m_capacityVertices - m_usedVertices;
    if (freeVertices == 0) return 0.0f;

    uint32_t largestBlock = 0;
    int curr = m_freeHead;
    while (curr != -1) {
        largestBlock = std::max(largestBlock, m_freeBlocks[curr].size);
        curr = m_freeBlocks[curr].next;
    }

    if (largestBlock == 0) return 1.0f;
    return 1.0f - static_cast<float>(largestBlock) / static_cast<float>(freeVertices);
}

// ---------------------------------------------------------------------------
// VertexPoolAllocator
// ---------------------------------------------------------------------------

VertexPoolAllocator::VertexPoolAllocator() = default;

VertexPoolAllocator::~VertexPoolAllocator() {
    shutdown();
}

void VertexPoolAllocator::init(const size_t initialCapacityVertices) {
    shutdown();

    const size_t bytes = initialCapacityVertices * sizeof(PackedBlockVertex);
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_STORAGE_BIT);
    m_ranges.init(initialCapacityVertices);
}

void VertexPoolAllocator::shutdown() {
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_ranges.shutdown();
    m_expandCountThisFrame = 0;
    m_uploadedBytesThisFrame = 0;
}

bool VertexPoolAllocator::allocate(const uint32_t vertexCount, GpuMeshRange& outRange) {
    if (m_ranges.allocate(vertexCount, outRange)) {
        return true;
    }

    const size_t currentCapacity = m_ranges.capacityVertices();
    const size_t newCapacity = std::max<size_t>(
        currentCapacity + vertexCount,
        currentCapacity == 0 ? vertexCount : currentCapacity * 2);
    expand(newCapacity);
    return m_ranges.allocate(vertexCount, outRange);
}

void VertexPoolAllocator::free(const GpuMeshRange& range) {
    m_ranges.free(range);
}

void VertexPoolAllocator::upload(const GpuMeshRange& range, const std::vector<PackedBlockVertex>& vertices) {
    if (vertices.empty()) return;
    const size_t bytes = vertices.size() * sizeof(PackedBlockVertex);
    glNamedBufferSubData(m_vbo,
                         static_cast<GLintptr>(range.firstVertex) * sizeof(PackedBlockVertex),
                         static_cast<GLsizeiptr>(bytes),
                         vertices.data());
    m_uploadedBytesThisFrame += bytes;
}

void VertexPoolAllocator::expand(const size_t newCapacityVertices) {
    const size_t oldCapacity = m_ranges.capacityVertices();
    if (newCapacityVertices <= oldCapacity) return;
    ++m_expandCountThisFrame;

    GLuint newVbo = 0;
    glCreateBuffers(1, &newVbo);
    glNamedBufferStorage(newVbo,
                         static_cast<GLsizeiptr>(newCapacityVertices * sizeof(PackedBlockVertex)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    if (m_vbo != 0 && oldCapacity > 0) {
        glCopyNamedBufferSubData(m_vbo, newVbo, 0, 0,
                                 static_cast<GLsizeiptr>(oldCapacity * sizeof(PackedBlockVertex)));
        glDeleteBuffers(1, &m_vbo);
    }

    m_vbo = newVbo;
    m_ranges.grow(newCapacityVertices);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexPoolAllocator::beginFrame() {
    m_expandCountThisFrame = 0;
    m_uploadedBytesThisFrame = 0;
}

// ---------------------------------------------------------------------------
// RhiVertexPoolAllocator
// ---------------------------------------------------------------------------

RhiVertexPoolAllocator::RhiVertexPoolAllocator() = default;

RhiVertexPoolAllocator::~RhiVertexPoolAllocator() {
    shutdown();
}

bool RhiVertexPoolAllocator::init(RhiDevice& rhiDevice,
                                  const size_t initialCapacityVertices,
                                  const char* debugName) {
    shutdown();
    if (initialCapacityVertices == 0u ||
        initialCapacityVertices > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }

    RhiBufferDesc desc;
    desc.debugName = debugName;
    desc.size = initialCapacityVertices * sizeof(PackedBlockVertex);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferSrc) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    m_buffer = rhiDevice.createBuffer(desc, nullptr, 0u);
    if (!m_buffer.isValid()) {
        return false;
    }

    m_rhiDevice = &rhiDevice;
    m_debugName = debugName;
    m_ranges.init(initialCapacityVertices);
    return true;
}

void RhiVertexPoolAllocator::shutdown() {
    if (m_rhiDevice != nullptr && m_buffer.isValid()) {
        m_rhiDevice->destroyBuffer(m_buffer);
    }
    m_buffer = {};
    m_rhiDevice = nullptr;
    m_debugName = nullptr;
    m_ranges.shutdown();
    m_expandCountThisFrame = 0u;
    m_uploadedBytesThisFrame = 0u;
}

bool RhiVertexPoolAllocator::allocate(RhiCommandList& commandList,
                                      const uint32_t vertexCount,
                                      GpuMeshRange& outRange) {
    if (m_rhiDevice == nullptr || !m_buffer.isValid()) {
        return false;
    }
    if (m_ranges.allocate(vertexCount, outRange)) {
        return true;
    }

    const size_t currentCapacity = m_ranges.capacityVertices();
    const size_t requiredCapacity = currentCapacity + vertexCount;
    constexpr size_t kMaxCapacity = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
    if (requiredCapacity > kMaxCapacity) {
        return false;
    }
    const size_t doubledCapacity = std::min(currentCapacity * 2u, kMaxCapacity);
    const size_t newCapacity = std::max(requiredCapacity, doubledCapacity);
    if (!expand(commandList, newCapacity)) {
        return false;
    }
    return m_ranges.allocate(vertexCount, outRange);
}

void RhiVertexPoolAllocator::free(const GpuMeshRange& range) {
    m_ranges.free(range);
}

bool RhiVertexPoolAllocator::upload(RhiCommandList& commandList,
                                    const GpuMeshRange& range,
                                    const std::vector<PackedBlockVertex>& vertices) {
    if (!m_buffer.isValid() || vertices.empty() || vertices.size() != range.vertexCount ||
        static_cast<size_t>(range.firstVertex) + range.vertexCount > m_ranges.capacityVertices()) {
        return false;
    }
    const size_t bytes = vertices.size() * sizeof(PackedBlockVertex);
    commandList.updateBuffer(
        m_buffer,
        static_cast<uint64_t>(range.firstVertex) * sizeof(PackedBlockVertex),
        vertices.data(),
        bytes);
    m_uploadedBytesThisFrame += bytes;
    return true;
}

bool RhiVertexPoolAllocator::expand(RhiCommandList& commandList,
                                    const size_t newCapacityVertices) {
    const size_t oldCapacityVertices = m_ranges.capacityVertices();
    if (m_rhiDevice == nullptr || !m_buffer.isValid() ||
        newCapacityVertices <= oldCapacityVertices) {
        return false;
    }

    RhiBufferDesc desc;
    desc.debugName = m_debugName;
    desc.size = newCapacityVertices * sizeof(PackedBlockVertex);
    desc.usage = rhiFlag(RhiBufferUsage::Vertex) |
                 rhiFlag(RhiBufferUsage::TransferSrc) |
                 rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    const RhiBufferHandle newBuffer = m_rhiDevice->createBuffer(desc, nullptr, 0u);
    if (!newBuffer.isValid()) {
        return false;
    }

    RhiBufferCopy copy;
    copy.src = m_buffer;
    copy.dst = newBuffer;
    copy.size = oldCapacityVertices * sizeof(PackedBlockVertex);
    commandList.copyBuffer(copy);
    m_rhiDevice->destroyBuffer(m_buffer);
    m_buffer = newBuffer;
    m_ranges.grow(newCapacityVertices);
    ++m_expandCountThisFrame;
    return true;
}

void RhiVertexPoolAllocator::beginFrame() {
    m_expandCountThisFrame = 0u;
    m_uploadedBytesThisFrame = 0u;
}

// ---------------------------------------------------------------------------
// WorldRenderBuffer
// ---------------------------------------------------------------------------

WorldRenderBuffer::WorldRenderBuffer() = default;

WorldRenderBuffer::~WorldRenderBuffer() {
    shutdown();
}

void WorldRenderBuffer::setupVertexLayout() {
    for (GLuint attrib = 0; attrib <= 10; ++attrib) {
        glDisableVertexAttribArray(attrib);
    }

    glEnableVertexAttribArray(11);
    glVertexAttribIPointer(11, 1, GL_UNSIGNED_INT, sizeof(PackedBlockVertex), reinterpret_cast<void*>(offsetof(PackedBlockVertex, posPacked)));

    glEnableVertexAttribArray(12);
    glVertexAttribIPointer(12, 1, GL_UNSIGNED_INT, sizeof(PackedBlockVertex), reinterpret_cast<void*>(offsetof(PackedBlockVertex, uvPacked)));

    glEnableVertexAttribArray(13);
    glVertexAttribIPointer(13, 1, GL_UNSIGNED_INT, sizeof(PackedBlockVertex), reinterpret_cast<void*>(offsetof(PackedBlockVertex, lightAoLayer)));

    glEnableVertexAttribArray(14);
    glVertexAttribIPointer(14, 1, GL_UNSIGNED_INT, sizeof(PackedBlockVertex), reinterpret_cast<void*>(offsetof(PackedBlockVertex, tintAnim)));
}

void WorldRenderBuffer::init() {
    m_opaquePool.init(kInitialPoolVertices);
    m_cutoutPool.init(kInitialCutoutPoolVertices);
    m_transparentPool.init(kInitialTransparentPoolVertices);

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
    m_opaqueIndirectCapacity = kInitialIndirectCapacity;
    m_cutoutIndirectCapacity = kInitialIndirectCapacity;
    m_transparentIndirectCapacity = kInitialIndirectCapacity;
    m_waterIndirectCapacity = kInitialIndirectCapacity;

    glCreateBuffers(1, &m_opaqueIndirectBuf);
    glNamedBufferStorage(m_opaqueIndirectBuf,
                         static_cast<GLsizeiptr>(m_opaqueIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_cutoutIndirectBuf);
    glNamedBufferStorage(m_cutoutIndirectBuf,
                         static_cast<GLsizeiptr>(m_cutoutIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_transparentIndirectBuf);
    glNamedBufferStorage(m_transparentIndirectBuf,
                         static_cast<GLsizeiptr>(m_transparentIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_waterIndirectBuf);
    glNamedBufferStorage(m_waterIndirectBuf,
                         static_cast<GLsizeiptr>(m_waterIndirectCapacity * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
    glCreateBuffers(1, &m_subChunkMetadataBuffer);
    m_subChunkMetadata.reserve(kInitialIndirectCapacity);

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelVertexArray(m_opaqueVao, "WorldRenderBuffer.OpaqueVAO");
    renderer::debug::labelVertexArray(m_cutoutVao, "WorldRenderBuffer.CutoutVAO");
    renderer::debug::labelVertexArray(m_transparentVao, "WorldRenderBuffer.TransparentVAO");
    renderer::debug::labelBuffer(m_opaquePool.vbo(), "WorldRenderBuffer.OpaqueVBO");
    renderer::debug::labelBuffer(m_cutoutPool.vbo(), "WorldRenderBuffer.CutoutVBO");
    renderer::debug::labelBuffer(m_transparentPool.vbo(), "WorldRenderBuffer.TransparentVBO");
    renderer::debug::labelBuffer(m_opaqueIndirectBuf, "WorldRenderBuffer.OpaqueIndirect");
    renderer::debug::labelBuffer(m_cutoutIndirectBuf, "WorldRenderBuffer.CutoutIndirect");
    renderer::debug::labelBuffer(m_transparentIndirectBuf, "WorldRenderBuffer.TransparentIndirect");
    renderer::debug::labelBuffer(m_waterIndirectBuf, "WorldRenderBuffer.WaterIndirectBuffer");
    renderer::debug::labelBuffer(m_subChunkMetadataBuffer, "WorldRenderBuffer.SubChunkMetadata");

    m_opaqueCommands.reserve(kInitialIndirectCapacity);
    m_cutoutCommands.reserve(kInitialIndirectCapacity);
    m_transparentCommands.reserve(kInitialIndirectCapacity);
    m_waterCommands.reserve(kInitialIndirectCapacity);
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
    if (m_waterIndirectBuf != 0) { glDeleteBuffers(1, &m_waterIndirectBuf); m_waterIndirectBuf = 0; }
    if (m_subChunkMetadataBuffer != 0) { glDeleteBuffers(1, &m_subChunkMetadataBuffer); m_subChunkMetadataBuffer = 0; }
    m_subChunkMetadata.clear();
    m_freeSubChunkMetadataIndices.clear();

    m_opaqueIndirectCapacity = 0;
    m_cutoutIndirectCapacity = 0;
    m_transparentIndirectCapacity = 0;
    m_waterIndirectCapacity = 0;
}

WorldGpuMesh WorldRenderBuffer::uploadSubChunk(
    const std::vector<BlockVertex>& opaque,
    const std::vector<BlockVertex>& cutout,
    const std::vector<BlockVertex>& cutoutDistance,
    const std::vector<BlockVertex>& transparent,
    const std::vector<BlockVertex>& water,
    const bool hasBounds, const glm::vec3& boundsMin, const glm::vec3& boundsMax)
{
    WorldGpuMesh result;
    if (opaque.empty() && cutout.empty() && cutoutDistance.empty() && transparent.empty() && water.empty()) {
        result.hasBounds = hasBounds;
        result.boundsMin = boundsMin;
        result.boundsMax = boundsMax;
        return result;
    }

    glm::vec3 origin(0.0f);
    if (hasBounds) {
        origin = glm::floor(boundsMin);
    } else {
        bool originSet = false;
        auto consumeOrigin = [&](const std::vector<BlockVertex>& vertices) {
            if (!vertices.empty() && !originSet) {
                origin = glm::floor(glm::vec3(vertices.front().x, vertices.front().y, vertices.front().z));
                originSet = true;
            }
        };
        consumeOrigin(opaque);
        consumeOrigin(cutout);
        consumeOrigin(cutoutDistance);
        consumeOrigin(transparent);
        consumeOrigin(water);
    }
    result.metadataIndex = uploadSubChunkMetadata(origin);

    auto makePacked = [&](const std::vector<BlockVertex>& vertices) {
        std::vector<BlockVertex> local = vertices;
        subtractOrigin(local, origin);
        return packBlockVertices(local);
    };

    if (!opaque.empty()) {
        if (!m_opaquePool.allocate(static_cast<uint32_t>(opaque.size()), result.opaque)) {
            return {};
        }
        result.opaque.metadataIndex = result.metadataIndex;
        m_opaquePool.upload(result.opaque, makePacked(opaque));
    }
    if (!cutout.empty()) {
        if (!m_cutoutPool.allocate(static_cast<uint32_t>(cutout.size()), result.cutout)) {
            m_opaquePool.free(result.opaque);
            return {};
        }
        result.cutout.metadataIndex = result.metadataIndex;
        m_cutoutPool.upload(result.cutout, makePacked(cutout));
    }
    if (!cutoutDistance.empty()) {
        if (!m_cutoutPool.allocate(static_cast<uint32_t>(cutoutDistance.size()), result.cutoutDistance)) {
            m_opaquePool.free(result.opaque);
            m_cutoutPool.free(result.cutout);
            return {};
        }
        result.cutoutDistance.metadataIndex = result.metadataIndex;
        m_cutoutPool.upload(result.cutoutDistance, makePacked(cutoutDistance));
    }
    if (!transparent.empty()) {
        if (!m_transparentPool.allocate(static_cast<uint32_t>(transparent.size()), result.transparent)) {
            m_opaquePool.free(result.opaque);
            m_cutoutPool.free(result.cutout);
            m_cutoutPool.free(result.cutoutDistance);
            return {};
        }
        result.transparent.metadataIndex = result.metadataIndex;
        m_transparentPool.upload(result.transparent, makePacked(transparent));
    }
    if (!water.empty()) {
        if (!m_transparentPool.allocate(static_cast<uint32_t>(water.size()), result.water)) {
            m_opaquePool.free(result.opaque);
            m_cutoutPool.free(result.cutout);
            m_cutoutPool.free(result.cutoutDistance);
            m_transparentPool.free(result.transparent);
            return {};
        }
        result.water.metadataIndex = result.metadataIndex;
        m_transparentPool.upload(result.water, makePacked(water));
    }

    result.hasBounds = hasBounds;
    result.boundsMin = boundsMin;
    result.boundsMax = boundsMax;
    return result;
}

void WorldRenderBuffer::free(const WorldGpuMesh& mesh) {
    if (mesh.metadataIndex != kInvalidMetadataIndex) {
        m_freeSubChunkMetadataIndices.push_back(mesh.metadataIndex);
    }
    m_opaquePool.free(mesh.opaque);
    m_cutoutPool.free(mesh.cutout);
    m_cutoutPool.free(mesh.cutoutDistance);
    m_transparentPool.free(mesh.transparent);
    m_transparentPool.free(mesh.water);
}

uint32_t WorldRenderBuffer::uploadSubChunkMetadata(const glm::vec3& origin) {
    if (m_subChunkMetadata.size() >= static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return kInvalidMetadataIndex;
    }

    uint32_t index = kInvalidMetadataIndex;
    if (!m_freeSubChunkMetadataIndices.empty()) {
        index = m_freeSubChunkMetadataIndices.back();
        m_freeSubChunkMetadataIndices.pop_back();
        m_subChunkMetadata[index] = SubChunkDrawMetadata{glm::vec4(origin, 0.0f)};
    } else {
        index = static_cast<uint32_t>(m_subChunkMetadata.size());
        m_subChunkMetadata.push_back(SubChunkDrawMetadata{glm::vec4(origin, 0.0f)});
    }
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(m_subChunkMetadata.size() * sizeof(SubChunkDrawMetadata));
    glNamedBufferData(m_subChunkMetadataBuffer, bytes, m_subChunkMetadata.data(), GL_DYNAMIC_DRAW);
    return index;
}

void WorldRenderBuffer::bindMetadataBuffer() const {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTerrainMetadataBinding, m_subChunkMetadataBuffer);
}

void WorldRenderBuffer::beginFrame() {
    m_opaqueCommands.clear();
    m_cutoutCommands.clear();
    m_transparentCommands.clear();
    m_waterCommands.clear();
    m_glSubmitCount = 0;
    m_opaqueLogicalCommandCount = 0;
    m_cutoutLogicalCommandCount = 0;
    m_transparentLogicalCommandCount = 0;
    m_opaqueVertexCount = 0;
    m_cutoutVertexCount = 0;
    m_transparentVertexCount = 0;
    m_waterVertexCount = 0;
    m_opaquePool.beginFrame();
    m_cutoutPool.beginFrame();
    m_transparentPool.beginFrame();
}

WorldRenderBuffer::FrameStatsSnapshot WorldRenderBuffer::makeCurrentFrameStats() const {
    FrameStatsSnapshot stats;
    stats.glSubmitCount = m_glSubmitCount;
    stats.opaqueCommands = m_opaqueCommands.size();
    stats.cutoutCommands = m_cutoutCommands.size();
    stats.transparentCommands = m_transparentCommands.size();
    stats.waterCommands = m_waterCommands.size();
    stats.opaqueLogicalCommands = m_opaqueLogicalCommandCount;
    stats.cutoutLogicalCommands = m_cutoutLogicalCommandCount;
    stats.transparentLogicalCommands = m_transparentLogicalCommandCount;
    stats.opaqueVertices = m_opaqueVertexCount;
    stats.cutoutVertices = m_cutoutVertexCount;
    stats.transparentVertices = m_transparentVertexCount;
    stats.waterVertices = m_waterVertexCount;
    return stats;
}

void WorldRenderBuffer::captureSceneFrameStats() {
    m_sceneFrameStats = makeCurrentFrameStats();
}

void WorldRenderBuffer::mergeSceneWaterFrameStats() {
    // Water is rendered after shadow passes have reused the command lists, so
    // only merge the water counters into the preserved main-scene snapshot.
    m_sceneFrameStats.glSubmitCount += m_glSubmitCount;
    m_sceneFrameStats.waterCommands = m_waterCommands.size();
    m_sceneFrameStats.waterVertices = m_waterVertexCount;
}

void WorldRenderBuffer::addOpaque(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_opaqueCommands.push_back({range.vertexCount, 1, range.firstVertex, range.metadataIndex});
    ++m_opaqueLogicalCommandCount;
    m_opaqueVertexCount += range.vertexCount;
}

void WorldRenderBuffer::addCutout(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_cutoutCommands.push_back({range.vertexCount, 1, range.firstVertex, range.metadataIndex});
    ++m_cutoutLogicalCommandCount;
    m_cutoutVertexCount += range.vertexCount;
}

void WorldRenderBuffer::addTransparent(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    m_transparentCommands.push_back({range.vertexCount, 1, range.firstVertex, range.metadataIndex});
    ++m_transparentLogicalCommandCount;
    m_transparentVertexCount += range.vertexCount;
}

void WorldRenderBuffer::addWater(const GpuMeshRange& range) {
    if (range.vertexCount == 0) return;
    // Water uses the transparent pool VAO/VBO, but has its own command list
    // so it can be flushed independently for the water composite pass.
    m_waterCommands.push_back({range.vertexCount, 1, range.firstVertex, range.metadataIndex});
    m_waterVertexCount += range.vertexCount;
}

void WorldRenderBuffer::clearWaterCommands() {
    m_waterCommands.clear();
    m_waterVertexCount = 0;
}

void WorldRenderBuffer::flushWater() {
    if (m_waterCommands.empty()) return;
    char label[64];
    std::snprintf(label, sizeof(label), "Water.MDI commands=%zu vertices=%llu",
                  m_waterCommands.size(),
                  static_cast<unsigned long long>(m_waterVertexCount));
    renderer::debug::ScopedDebugGroup group(label);
    ensureVaoVertexBuffer(m_transparentVao, m_transparentPool.vbo(), m_transparentVaoBoundVbo);
    ensureIndirectCapacity(m_waterCommands, m_waterIndirectBuf, m_waterIndirectCapacity, m_waterCommands.size());

    glNamedBufferSubData(m_waterIndirectBuf, 0,
                         static_cast<GLsizeiptr>(m_waterCommands.size() * sizeof(DrawArraysIndirectCommand)),
                         m_waterCommands.data());

    bindMetadataBuffer();
    glBindVertexArray(m_transparentVao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_waterIndirectBuf);
    glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr,
                              static_cast<GLsizei>(m_waterCommands.size()),
                              0);
    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    ++m_glSubmitCount;
}

void WorldRenderBuffer::flushOpaque() {
    char label[64];
    std::snprintf(label, sizeof(label), "Terrain.Opaque.MDI commands=%zu vertices=%llu",
                  m_opaqueCommands.size(),
                  static_cast<unsigned long long>(m_opaqueVertexCount));
    renderer::debug::ScopedDebugGroup group(label);
    flushPass(m_opaqueCommands, m_opaqueIndirectBuf, m_opaqueVao, m_opaquePool.vbo(), m_opaqueVaoBoundVbo);
}

void WorldRenderBuffer::flushCutout() {
    char label[64];
    std::snprintf(label, sizeof(label), "Terrain.Cutout.MDI commands=%zu vertices=%llu",
                  m_cutoutCommands.size(),
                  static_cast<unsigned long long>(m_cutoutVertexCount));
    renderer::debug::ScopedDebugGroup group(label);
    flushPass(m_cutoutCommands, m_cutoutIndirectBuf, m_cutoutVao, m_cutoutPool.vbo(), m_cutoutVaoBoundVbo);
}

void WorldRenderBuffer::flushTransparent() {
    char label[64];
    std::snprintf(label, sizeof(label), "Terrain.Transparent.MDI commands=%zu vertices=%llu",
                  m_transparentCommands.size(),
                  static_cast<unsigned long long>(m_transparentVertexCount));
    renderer::debug::ScopedDebugGroup group(label);
    flushPass(m_transparentCommands, m_transparentIndirectBuf, m_transparentVao, m_transparentPool.vbo(), m_transparentVaoBoundVbo);
}

void WorldRenderBuffer::bindTransparentVao() {
    ensureVaoVertexBuffer(m_transparentVao, m_transparentPool.vbo(), m_transparentVaoBoundVbo);
    glBindVertexArray(m_transparentVao);
}

void WorldRenderBuffer::ensureVaoVertexBuffer(const uint32_t vao, const uint32_t vbo, uint32_t& cachedVbo) {
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
                                                uint32_t& buf, size_t& capacity, const size_t needed) {
    if (needed <= capacity) return;
    capacity = std::max(needed, capacity * 2);
    if (buf != 0) {
        glDeleteBuffers(1, &buf);
    }
    glCreateBuffers(1, &buf);
    glNamedBufferStorage(buf,
                         static_cast<GLsizeiptr>(capacity * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
}

void WorldRenderBuffer::flushPass(std::vector<DrawArraysIndirectCommand>& commands,
                                   uint32_t& indirectBuf, uint32_t vao, uint32_t vbo, uint32_t& cachedVbo) {
    if (commands.empty()) return;

    ensureIndirectCapacity(commands, indirectBuf,
                           vao == m_opaqueVao ? m_opaqueIndirectCapacity :
                           vao == m_cutoutVao ? m_cutoutIndirectCapacity :
                           m_transparentIndirectCapacity,
                           commands.size());

    glNamedBufferSubData(indirectBuf, 0,
                         static_cast<GLsizeiptr>(commands.size() * sizeof(DrawArraysIndirectCommand)),
                         commands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuf);

    bindMetadataBuffer();
    ensureVaoVertexBuffer(vao, vbo, cachedVbo);
    glBindVertexArray(vao);
    glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr, static_cast<GLsizei>(commands.size()), 0);
    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    ++m_glSubmitCount;
}
