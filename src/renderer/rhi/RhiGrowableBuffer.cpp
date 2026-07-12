#include "renderer/rhi/RhiGrowableBuffer.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"

#include <algorithm>
#include <limits>

namespace {

[[nodiscard]] bool resolveSteadyState(const RhiBufferUsageFlags usage,
                                      RhiResourceState& state) {
    uint32_t stateCount = 0u;
    const auto select = [&](const RhiBufferUsage bufferUsage,
                            const RhiResourceState candidate) {
        if ((usage & rhiFlag(bufferUsage)) != 0u) {
            state = candidate;
            ++stateCount;
        }
    };
    select(RhiBufferUsage::Vertex, RhiResourceState::VertexBuffer);
    select(RhiBufferUsage::Index, RhiResourceState::IndexBuffer);
    select(RhiBufferUsage::Uniform, RhiResourceState::UniformBuffer);
    select(RhiBufferUsage::Storage, RhiResourceState::StorageBuffer);
    select(RhiBufferUsage::Indirect, RhiResourceState::IndirectArgument);
    return stateCount == 1u;
}

} // namespace

RhiGrowableBuffer::RhiGrowableBuffer() = default;

RhiGrowableBuffer::~RhiGrowableBuffer() {
    shutdown();
}

bool RhiGrowableBuffer::init(RhiDevice& rhiDevice,
                             const uint64_t initialSize,
                             const RhiBufferUsageFlags usage,
                             const char* debugName) {
    shutdown();
    if (initialSize == 0u || usage == 0u) {
        return false;
    }
    if (!resolveSteadyState(usage, m_steadyState)) {
        return false;
    }

    m_usage = usage |
              rhiFlag(RhiBufferUsage::TransferSrc) |
              rhiFlag(RhiBufferUsage::TransferDst);
    RhiBufferDesc desc;
    desc.debugName = debugName;
    desc.size = initialSize;
    desc.usage = m_usage;
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = m_steadyState;
    m_buffer = rhiDevice.createBuffer(desc, nullptr, 0u);
    if (!m_buffer.isValid()) {
        m_usage = 0u;
        m_steadyState = RhiResourceState::Undefined;
        return false;
    }

    m_rhiDevice = &rhiDevice;
    m_capacity = initialSize;
    m_debugName = debugName;
    return true;
}

void RhiGrowableBuffer::shutdown() {
    if (m_rhiDevice != nullptr) {
        if (m_buffer.isValid()) {
            m_rhiDevice->destroyBuffer(m_buffer);
        }
        for (const RhiBufferHandle retiredBuffer : m_retiredBuffers) {
            m_rhiDevice->destroyBuffer(retiredBuffer);
        }
    }
    m_rhiDevice = nullptr;
    m_buffer = {};
    m_usage = 0u;
    m_steadyState = RhiResourceState::Undefined;
    m_capacity = 0u;
    m_debugName = nullptr;
    m_retiredBuffers.clear();
}

bool RhiGrowableBuffer::ensureCapacity(RhiCommandList& commandList,
                                       const uint64_t requiredSize) {
    if (m_rhiDevice == nullptr || !m_buffer.isValid() || requiredSize == 0u) {
        return false;
    }
    if (requiredSize <= m_capacity) {
        return true;
    }

    const uint64_t doubledCapacity = m_capacity <= std::numeric_limits<uint64_t>::max() / 2u
        ? m_capacity * 2u
        : std::numeric_limits<uint64_t>::max();
    const uint64_t newCapacity = std::max(requiredSize, doubledCapacity);

    RhiBufferDesc desc;
    desc.debugName = m_debugName;
    desc.size = newCapacity;
    desc.usage = m_usage;
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = m_steadyState;
    const RhiBufferHandle newBuffer = m_rhiDevice->createBuffer(desc, nullptr, 0u);
    if (!newBuffer.isValid()) {
        return false;
    }

    commandList.bufferBarrier({m_buffer, m_steadyState, RhiResourceState::TransferSrc});
    commandList.bufferBarrier({newBuffer, m_steadyState, RhiResourceState::TransferDst});
    RhiBufferCopy copy;
    copy.src = m_buffer;
    copy.dst = newBuffer;
    copy.size = m_capacity;
    commandList.copyBuffer(copy);
    commandList.bufferBarrier({newBuffer, RhiResourceState::TransferDst, m_steadyState});
    m_retiredBuffers.push_back(m_buffer);
    m_buffer = newBuffer;
    m_capacity = newCapacity;
    return true;
}

bool RhiGrowableBuffer::write(RhiCommandList& commandList,
                              const uint64_t offset,
                              const void* data,
                              const size_t size) {
    if (data == nullptr || size == 0u || (offset & 3u) != 0u || (size & 3u) != 0u ||
        offset > std::numeric_limits<uint64_t>::max() - size) {
        return false;
    }
    const uint64_t requiredSize = offset + size;
    if (!ensureCapacity(commandList, requiredSize)) {
        return false;
    }
    commandList.bufferBarrier({m_buffer, m_steadyState, RhiResourceState::TransferDst});
    commandList.updateBuffer(m_buffer, offset, data, size);
    commandList.bufferBarrier({m_buffer, RhiResourceState::TransferDst, m_steadyState});
    return true;
}
