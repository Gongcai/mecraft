#ifndef MECRAFT_RHI_GROWABLE_BUFFER_H
#define MECRAFT_RHI_GROWABLE_BUFFER_H

#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>

class RhiCommandList;
class RhiDevice;

class RhiGrowableBuffer {
public:
    RhiGrowableBuffer();
    ~RhiGrowableBuffer();

    bool init(RhiDevice& rhiDevice,
              uint64_t initialSize,
              RhiBufferUsageFlags usage,
              const char* debugName);
    void shutdown();

    bool ensureCapacity(RhiCommandList& commandList, uint64_t requiredSize);
    bool write(RhiCommandList& commandList,
               uint64_t offset,
               const void* data,
               size_t size);

    [[nodiscard]] RhiBufferHandle buffer() const { return m_buffer; }
    [[nodiscard]] uint64_t capacity() const { return m_capacity; }

private:
    RhiDevice* m_rhiDevice = nullptr;
    RhiBufferHandle m_buffer;
    RhiBufferUsageFlags m_usage = 0u;
    uint64_t m_capacity = 0u;
    const char* m_debugName = nullptr;
};

#endif // MECRAFT_RHI_GROWABLE_BUFFER_H
