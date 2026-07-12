#include "WorldRenderBuffer.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
constexpr uint32_t kInvalidMetadataIndex = 0xFFFFFFFFu;
constexpr float kPackedPositionScale = 128.0f;
constexpr float kPackedUvScale = 1024.0f;

template <typename Handle>
bool sameHandle(const Handle lhs, const Handle rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

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
    if (!std::isfinite(value)) {
        std::cerr << "WorldRenderBuffer: packed local coordinate is not finite value="
                  << value << '\n';
        std::abort();
    }
    const int quantized = static_cast<int>(std::lround(value * kPackedPositionScale));
    if (quantized < 0 || quantized > 4095) {
        std::cerr << "WorldRenderBuffer: packed local coordinate is out of range value="
                  << value << " quantized=" << quantized << '\n';
        std::abort();
    }
    return static_cast<uint32_t>(quantized);
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
    desc.initialState = RhiResourceState::VertexBuffer;
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
    if (m_rhiDevice != nullptr) {
        if (m_buffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_buffer);
        }
        for (const RhiBufferHandle retiredBuffer : m_retiredBuffers) {
            m_rhiDevice->destroyBuffer(retiredBuffer);
        }
    }
    m_buffer = {};
    m_retiredBuffers.clear();
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
    commandList.bufferBarrier({m_buffer,
                               RhiResourceState::VertexBuffer,
                               RhiResourceState::TransferDst});
    commandList.updateBuffer(
        m_buffer,
        static_cast<uint64_t>(range.firstVertex) * sizeof(PackedBlockVertex),
        vertices.data(),
        bytes);
    commandList.bufferBarrier({m_buffer,
                               RhiResourceState::TransferDst,
                               RhiResourceState::VertexBuffer});
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
    desc.initialState = RhiResourceState::VertexBuffer;
    const RhiBufferHandle newBuffer = m_rhiDevice->createBuffer(desc, nullptr, 0u);
    if (!newBuffer.isValid()) {
        return false;
    }

    commandList.bufferBarrier({m_buffer,
                               RhiResourceState::VertexBuffer,
                               RhiResourceState::TransferSrc});
    commandList.bufferBarrier({newBuffer,
                               RhiResourceState::VertexBuffer,
                               RhiResourceState::TransferDst});
    RhiBufferCopy copy;
    copy.src = m_buffer;
    copy.dst = newBuffer;
    copy.size = oldCapacityVertices * sizeof(PackedBlockVertex);
    commandList.copyBuffer(copy);
    commandList.bufferBarrier({newBuffer,
                               RhiResourceState::TransferDst,
                               RhiResourceState::VertexBuffer});
    m_retiredBuffers.push_back(m_buffer);
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

bool WorldRenderBuffer::init(RhiDevice& rhiDevice) {
    shutdown();
    m_rhiDevice = &rhiDevice;

    const uint64_t commandBytes = kInitialIndirectCapacity * sizeof(DrawArraysIndirectCommand);
    const uint64_t metadataBytes = kInitialIndirectCapacity * sizeof(SubChunkDrawMetadata);
    if (!m_opaquePool.init(rhiDevice, kInitialPoolVertices, "WorldRenderBuffer.OpaqueVertices") ||
        !m_cutoutPool.init(rhiDevice, kInitialCutoutPoolVertices, "WorldRenderBuffer.CutoutVertices") ||
        !m_transparentPool.init(
            rhiDevice, kInitialTransparentPoolVertices, "WorldRenderBuffer.TransparentVertices") ||
        !m_rhiOpaqueIndirectBuffer.init(
            rhiDevice,
            commandBytes,
            rhiFlag(RhiBufferUsage::Indirect),
            "WorldRenderBuffer.RhiOpaqueIndirect") ||
        !m_rhiCutoutIndirectBuffer.init(
            rhiDevice,
            commandBytes,
            rhiFlag(RhiBufferUsage::Indirect),
            "WorldRenderBuffer.RhiCutoutIndirect") ||
        !m_rhiTransparentIndirectBuffer.init(
            rhiDevice,
            commandBytes,
            rhiFlag(RhiBufferUsage::Indirect),
            "WorldRenderBuffer.RhiTransparentIndirect") ||
        !m_rhiWaterIndirectBuffer.init(
            rhiDevice,
            commandBytes,
            rhiFlag(RhiBufferUsage::Indirect),
            "WorldRenderBuffer.RhiWaterIndirect") ||
        !m_rhiMetadataBuffer.init(
            rhiDevice,
            metadataBytes,
            rhiFlag(RhiBufferUsage::Storage),
            "WorldRenderBuffer.RhiMetadata")) {
        shutdown();
        return false;
    }
    m_subChunkMetadata.reserve(kInitialIndirectCapacity);

    m_opaqueCommands.reserve(kInitialIndirectCapacity);
    m_cutoutCommands.reserve(kInitialIndirectCapacity);
    m_transparentCommands.reserve(kInitialIndirectCapacity);
    m_waterCommands.reserve(kInitialIndirectCapacity);
    return true;
}

void WorldRenderBuffer::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_rhiMetadataBindGroup.isValid()) {
            m_rhiDevice->destroyBindGroup(m_rhiMetadataBindGroup);
        }
        for (const RhiBindGroupHandle bindGroup : m_retiredMetadataBindGroups) {
            m_rhiDevice->destroyBindGroup(bindGroup);
        }
    }
    m_rhiMetadataBindGroup = {};
    m_retiredMetadataBindGroups.clear();
    m_rhiMetadataLayout = {};
    m_rhiMetadataBoundBuffer = {};
    m_rhiMetadataBuffer.shutdown();
    m_rhiWaterIndirectBuffer.shutdown();
    m_rhiTransparentIndirectBuffer.shutdown();
    m_rhiCutoutIndirectBuffer.shutdown();
    m_rhiOpaqueIndirectBuffer.shutdown();
    m_opaquePool.shutdown();
    m_cutoutPool.shutdown();
    m_transparentPool.shutdown();
    m_rhiDevice = nullptr;
    m_subChunkMetadata.clear();
    m_freeSubChunkMetadataIndices.clear();
}

WorldGpuMesh WorldRenderBuffer::uploadSubChunk(
    RhiCommandList& commandList,
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
    result.metadataIndex = uploadSubChunkMetadata(commandList, origin);
    if (result.metadataIndex == kInvalidMetadataIndex) {
        return {};
    }

    auto makePacked = [&](const std::vector<BlockVertex>& vertices) {
        std::vector<BlockVertex> local = vertices;
        subtractOrigin(local, origin);
        return packBlockVertices(local);
    };

    if (!opaque.empty()) {
        if (!m_opaquePool.allocate(commandList, static_cast<uint32_t>(opaque.size()), result.opaque)) {
            free(result);
            return {};
        }
        result.opaque.metadataIndex = result.metadataIndex;
        const std::vector<PackedBlockVertex> packed = makePacked(opaque);
        if (!m_opaquePool.upload(commandList, result.opaque, packed)) {
            free(result);
            return {};
        }
    }
    if (!cutout.empty()) {
        if (!m_cutoutPool.allocate(commandList, static_cast<uint32_t>(cutout.size()), result.cutout)) {
            free(result);
            return {};
        }
        result.cutout.metadataIndex = result.metadataIndex;
        const std::vector<PackedBlockVertex> packed = makePacked(cutout);
        if (!m_cutoutPool.upload(commandList, result.cutout, packed)) {
            free(result);
            return {};
        }
    }
    if (!cutoutDistance.empty()) {
        if (!m_cutoutPool.allocate(
                commandList,
                static_cast<uint32_t>(cutoutDistance.size()),
                result.cutoutDistance)) {
            free(result);
            return {};
        }
        result.cutoutDistance.metadataIndex = result.metadataIndex;
        const std::vector<PackedBlockVertex> packed = makePacked(cutoutDistance);
        if (!m_cutoutPool.upload(commandList, result.cutoutDistance, packed)) {
            free(result);
            return {};
        }
    }
    if (!transparent.empty()) {
        if (!m_transparentPool.allocate(
                commandList,
                static_cast<uint32_t>(transparent.size()),
                result.transparent)) {
            free(result);
            return {};
        }
        result.transparent.metadataIndex = result.metadataIndex;
        const std::vector<PackedBlockVertex> packed = makePacked(transparent);
        if (!m_transparentPool.upload(commandList, result.transparent, packed)) {
            free(result);
            return {};
        }
    }
    if (!water.empty()) {
        if (!m_transparentPool.allocate(commandList, static_cast<uint32_t>(water.size()), result.water)) {
            free(result);
            return {};
        }
        result.water.metadataIndex = result.metadataIndex;
        const std::vector<PackedBlockVertex> packed = makePacked(water);
        if (!m_transparentPool.upload(commandList, result.water, packed)) {
            free(result);
            return {};
        }
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

uint32_t WorldRenderBuffer::uploadSubChunkMetadata(RhiCommandList& commandList,
                                                   const glm::vec3& origin) {
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
    if (!m_rhiMetadataBuffer.write(
            commandList,
            static_cast<uint64_t>(index) * sizeof(SubChunkDrawMetadata),
            &m_subChunkMetadata[index],
            sizeof(SubChunkDrawMetadata))) {
        m_freeSubChunkMetadataIndices.push_back(index);
        return kInvalidMetadataIndex;
    }
    return index;
}

void WorldRenderBuffer::beginFrame() {
    if (m_rhiDevice != nullptr) {
        for (const RhiBindGroupHandle bindGroup : m_retiredMetadataBindGroups) {
            m_rhiDevice->destroyBindGroup(bindGroup);
        }
    }
    m_retiredMetadataBindGroups.clear();
    m_opaqueCommands.clear();
    m_cutoutCommands.clear();
    m_transparentCommands.clear();
    m_waterCommands.clear();
    m_rhiSubmitCount = 0;
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

bool WorldRenderBuffer::prepareRhiOpaqueAndCutout(
    RhiCommandList& commandList,
    const RhiBindGroupLayoutHandle metadataLayout) {
    if (m_rhiDevice == nullptr || !metadataLayout.isValid()) {
        return false;
    }
    if (!m_opaqueCommands.empty() &&
        !m_rhiOpaqueIndirectBuffer.write(
            commandList,
            0u,
            m_opaqueCommands.data(),
            m_opaqueCommands.size() * sizeof(DrawArraysIndirectCommand))) {
        return false;
    }
    if (!m_cutoutCommands.empty() &&
        !m_rhiCutoutIndirectBuffer.write(
            commandList,
            0u,
            m_cutoutCommands.data(),
            m_cutoutCommands.size() * sizeof(DrawArraysIndirectCommand))) {
        return false;
    }
    return ensureRhiMetadataBindGroup(metadataLayout);
}

void WorldRenderBuffer::recordRhiOpaque(RhiCommandList& commandList,
                                        const RhiPipelineHandle pipeline,
                                        const RhiBindGroupHandle materialBindGroup) {
    if (m_opaqueCommands.empty()) {
        return;
    }
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, m_rhiMetadataBindGroup);
    commandList.setBindGroup(1u, materialBindGroup);
    commandList.setVertexBuffer(0u, m_opaquePool.buffer(), 0u);
    commandList.drawIndirect(
        m_rhiOpaqueIndirectBuffer.buffer(),
        0u,
        static_cast<uint32_t>(m_opaqueCommands.size()),
        sizeof(DrawArraysIndirectCommand));
    ++m_rhiSubmitCount;
}

void WorldRenderBuffer::recordRhiCutout(RhiCommandList& commandList,
                                        const RhiPipelineHandle pipeline,
                                        const RhiBindGroupHandle materialBindGroup) {
    if (m_cutoutCommands.empty()) {
        return;
    }
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, m_rhiMetadataBindGroup);
    commandList.setBindGroup(1u, materialBindGroup);
    commandList.setVertexBuffer(0u, m_cutoutPool.buffer(), 0u);
    commandList.drawIndirect(
        m_rhiCutoutIndirectBuffer.buffer(),
        0u,
        static_cast<uint32_t>(m_cutoutCommands.size()),
        sizeof(DrawArraysIndirectCommand));
    ++m_rhiSubmitCount;
}

bool WorldRenderBuffer::prepareRhiTransparent(
    RhiCommandList& commandList,
    const RhiBindGroupLayoutHandle metadataLayout) {
    if (m_rhiDevice == nullptr || !metadataLayout.isValid()) {
        return false;
    }
    if (!m_transparentCommands.empty() &&
        !m_rhiTransparentIndirectBuffer.write(
            commandList,
            0u,
            m_transparentCommands.data(),
            m_transparentCommands.size() * sizeof(DrawArraysIndirectCommand))) {
        return false;
    }
    return ensureRhiMetadataBindGroup(metadataLayout);
}

void WorldRenderBuffer::recordRhiTransparent(
    RhiCommandList& commandList,
    const RhiPipelineHandle pipeline,
    const RhiBindGroupHandle materialBindGroup) {
    if (m_transparentCommands.empty()) {
        return;
    }
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, m_rhiMetadataBindGroup);
    commandList.setBindGroup(1u, materialBindGroup);
    commandList.setVertexBuffer(0u, m_transparentPool.buffer(), 0u);
    commandList.drawIndirect(
        m_rhiTransparentIndirectBuffer.buffer(),
        0u,
        static_cast<uint32_t>(m_transparentCommands.size()),
        sizeof(DrawArraysIndirectCommand));
    ++m_rhiSubmitCount;
}

bool WorldRenderBuffer::prepareRhiWater(
    RhiCommandList& commandList,
    const RhiBindGroupLayoutHandle metadataLayout) {
    if (m_rhiDevice == nullptr || !metadataLayout.isValid()) {
        return false;
    }
    if (!m_waterCommands.empty() &&
        !m_rhiWaterIndirectBuffer.write(
            commandList,
            0u,
            m_waterCommands.data(),
            m_waterCommands.size() * sizeof(DrawArraysIndirectCommand))) {
        return false;
    }
    return ensureRhiMetadataBindGroup(metadataLayout);
}

void WorldRenderBuffer::recordRhiWater(
    RhiCommandList& commandList,
    const RhiPipelineHandle pipeline,
    const RhiBindGroupHandle materialBindGroup) {
    if (m_waterCommands.empty()) {
        return;
    }
    commandList.setGraphicsPipeline(pipeline);
    commandList.setBindGroup(0u, m_rhiMetadataBindGroup);
    commandList.setBindGroup(1u, materialBindGroup);
    commandList.setVertexBuffer(0u, m_transparentPool.buffer(), 0u);
    commandList.drawIndirect(
        m_rhiWaterIndirectBuffer.buffer(),
        0u,
        static_cast<uint32_t>(m_waterCommands.size()),
        sizeof(DrawArraysIndirectCommand));
    ++m_rhiSubmitCount;
}

bool WorldRenderBuffer::ensureRhiMetadataBindGroup(
    const RhiBindGroupLayoutHandle metadataLayout) {
    const RhiBufferHandle metadataBuffer = m_rhiMetadataBuffer.buffer();
    if (m_rhiMetadataBindGroup.isValid() &&
        sameHandle(m_rhiMetadataLayout, metadataLayout) &&
        sameHandle(m_rhiMetadataBoundBuffer, metadataBuffer)) {
        return true;
    }
    if (m_rhiDevice == nullptr || !metadataBuffer.isValid()) {
        return false;
    }
    if (m_rhiMetadataBindGroup.isValid()) {
        m_retiredMetadataBindGroups.push_back(m_rhiMetadataBindGroup);
    }

    RhiBindGroupDesc desc;
    desc.layout = metadataLayout;
    RhiBindGroupEntry entry;
    entry.binding = kTerrainMetadataBinding;
    entry.resource.buffer.buffer = metadataBuffer;
    entry.resource.buffer.range = m_rhiMetadataBuffer.capacity();
    desc.entries.push_back(entry);
    m_rhiMetadataBindGroup = m_rhiDevice->createBindGroup(desc);
    if (!m_rhiMetadataBindGroup.isValid()) {
        m_rhiMetadataLayout = {};
        m_rhiMetadataBoundBuffer = {};
        return false;
    }
    m_rhiMetadataLayout = metadataLayout;
    m_rhiMetadataBoundBuffer = metadataBuffer;
    return true;
}

WorldRenderBuffer::FrameStatsSnapshot WorldRenderBuffer::makeCurrentFrameStats() const {
    FrameStatsSnapshot stats;
    stats.rhiSubmitCount = m_rhiSubmitCount;
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
    m_sceneFrameStats.rhiSubmitCount += m_rhiSubmitCount;
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
    // Water shares the transparent vertex pool but keeps an independent indirect command stream.
    m_waterCommands.push_back({range.vertexCount, 1, range.firstVertex, range.metadataIndex});
    m_waterVertexCount += range.vertexCount;
}

void WorldRenderBuffer::clearWaterCommands() {
    m_waterCommands.clear();
    m_waterVertexCount = 0;
}
