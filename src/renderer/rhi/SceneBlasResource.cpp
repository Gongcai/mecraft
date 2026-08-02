#include "renderer/rhi/SceneBlasResource.h"

#include "renderer/rhi/RhiDevice.h"

#include <unordered_set>
#include <utility>

namespace renderer::rt {

std::shared_ptr<SceneBlasResource> SceneBlasResource::create(RhiDevice& device,
                                                             const RhiAccelerationStructureHandle accelerationStructure,
                                                             const RhiBufferHandle storageBuffer,
                                                             const uint64_t deviceAddress, const uint64_t blasBytes,
                                                             std::vector<RhiBufferHandle> retainedBuffers) {
    if (!accelerationStructure.isValid() || !storageBuffer.isValid() || deviceAddress == 0u || blasBytes == 0u) {
        return nullptr;
    }
    std::unordered_set<uint64_t> uniqueBuffers;
    uniqueBuffers.reserve(retainedBuffers.size());
    for (const RhiBufferHandle buffer : retainedBuffers) {
        const uint64_t identity = (static_cast<uint64_t>(buffer.generation) << 32u) | buffer.index;
        if (!buffer.isValid() ||
            (buffer.index == storageBuffer.index && buffer.generation == storageBuffer.generation) ||
            !uniqueBuffers.insert(identity).second) {
            return nullptr;
        }
    }
    return std::shared_ptr<SceneBlasResource>(new SceneBlasResource(
        device, accelerationStructure, storageBuffer, deviceAddress, blasBytes, std::move(retainedBuffers)));
}

SceneBlasResource::SceneBlasResource(RhiDevice& device, const RhiAccelerationStructureHandle accelerationStructure,
                                     const RhiBufferHandle storageBuffer, const uint64_t deviceAddress,
                                     const uint64_t blasBytes, std::vector<RhiBufferHandle> retainedBuffers)
    : m_device(&device), m_accelerationStructure(accelerationStructure), m_storageBuffer(storageBuffer),
      m_deviceAddress(deviceAddress), m_blasBytes(blasBytes), m_retainedBuffers(std::move(retainedBuffers)) {}

SceneBlasResource::~SceneBlasResource() {
    m_device->destroyAccelerationStructure(m_accelerationStructure);
    m_device->destroyBuffer(m_storageBuffer);
    for (const RhiBufferHandle buffer : m_retainedBuffers) {
        m_device->destroyBuffer(buffer);
    }
}

} // namespace renderer::rt
