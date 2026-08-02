#ifndef MECRAFT_SCENE_BLAS_RESOURCE_H
#define MECRAFT_SCENE_BLAS_RESOURCE_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class RhiDevice;

namespace renderer::rt {

/// Owns one compacted bottom-level acceleration structure and resources that must share its visible lifetime.
/// Scene TLAS generations retain this object so a replaced producer cannot invalidate referenced device addresses.
class SceneBlasResource final {
public:
    /// Creates one shared BLAS lifetime after validating every native-independent handle and byte count.
    /// @param device Device that owns the acceleration structure and buffers.
    /// @param accelerationStructure Compacted bottom-level acceleration structure.
    /// @param storageBuffer Buffer backing accelerationStructure.
    /// @param deviceAddress Non-zero traversal address published into TLAS instances.
    /// @param blasBytes Exact compacted storage byte count.
    /// @param retainedBuffers Geometry buffers that must remain addressable while the BLAS can be traversed.
    /// @return Shared lifetime object, or nullptr when the descriptor is invalid.
    [[nodiscard]] static std::shared_ptr<SceneBlasResource>
    create(RhiDevice& device, RhiAccelerationStructureHandle accelerationStructure, RhiBufferHandle storageBuffer,
           uint64_t deviceAddress, uint64_t blasBytes, std::vector<RhiBufferHandle> retainedBuffers = {});

    ~SceneBlasResource();

    SceneBlasResource(const SceneBlasResource&) = delete;
    SceneBlasResource& operator=(const SceneBlasResource&) = delete;

    [[nodiscard]] RhiAccelerationStructureHandle accelerationStructure() const { return m_accelerationStructure; }
    [[nodiscard]] RhiBufferHandle storageBuffer() const { return m_storageBuffer; }
    [[nodiscard]] uint64_t deviceAddress() const { return m_deviceAddress; }
    [[nodiscard]] uint64_t blasBytes() const { return m_blasBytes; }
    [[nodiscard]] const std::vector<RhiBufferHandle>& retainedBuffers() const { return m_retainedBuffers; }

    /// Resolves the immutable device address captured for one retained geometry buffer.
    /// @param buffer Exact handle whose lifetime is owned by this shared BLAS resource.
    /// @return Non-zero creation-time address, or no value when the handle is not retained.
    [[nodiscard]] std::optional<uint64_t> retainedBufferDeviceAddress(RhiBufferHandle buffer) const;

private:
    SceneBlasResource(RhiDevice& device, RhiAccelerationStructureHandle accelerationStructure,
                      RhiBufferHandle storageBuffer, uint64_t deviceAddress, uint64_t blasBytes,
                      std::vector<RhiBufferHandle> retainedBuffers, std::vector<uint64_t> retainedBufferAddresses);

    RhiDevice* m_device = nullptr;
    RhiAccelerationStructureHandle m_accelerationStructure;
    RhiBufferHandle m_storageBuffer;
    uint64_t m_deviceAddress = 0u;
    uint64_t m_blasBytes = 0u;
    std::vector<RhiBufferHandle> m_retainedBuffers;
    std::vector<uint64_t> m_retainedBufferAddresses;
};

using SceneBlasResourcePtr = std::shared_ptr<const SceneBlasResource>;

} // namespace renderer::rt

#endif // MECRAFT_SCENE_BLAS_RESOURCE_H
