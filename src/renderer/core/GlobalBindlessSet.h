#ifndef MECRAFT_GLOBAL_BINDLESS_SET_H
#define MECRAFT_GLOBAL_BINDLESS_SET_H

#include "renderer/contracts/BindlessDescriptorContract.h"
#include "renderer/core/BindlessDescriptorSlotAllocator.h"
#include "renderer/rhi/RhiDescriptor.h"
#include "renderer/rhi/RhiResources.h"

#include <cstdint>

class RhiDevice;

namespace renderer::core {

/// Identifies deterministic Global Bindless Set initialization and publication failures.
enum class GlobalBindlessSetError : uint8_t {
    None,
    AlreadyInitialized,
    NotInitialized,
    BackendUnsupported,
    CapabilityMissing,
    InvalidCapacity,
    DescriptorLimitExceeded,
    LayoutCreationFailed,
    BindGroupCreationFailed,
    InvalidResource,
    DescriptorPublicationFailed,
    CapacityExceeded,
    InvalidHandle,
    StaleGeneration,
    SlotNotLive
};

/// Returns the stable identifier used by logs and automated validation.
/// @param error Global bindless failure to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* globalBindlessSetErrorStableId(GlobalBindlessSetError error);

/// Defines the immutable descriptor-array capacities allocated by one global set.
struct GlobalBindlessSetConfig final {
    uint32_t sampledTexture2DCapacity = 1024u;
    uint32_t sampledTextureCubeCapacity = 128u;
    uint32_t samplerCapacity = 128u;
    uint32_t storageBufferCapacity = 512u;
};

/// Returns either one published strong handle or a structured publication failure.
/// @tparam Tag Bindless descriptor domain associated with the returned handle.
template <typename Tag> struct GlobalBindlessPublicationResult final {
    renderer::contracts::BindlessDescriptorHandle<Tag> handle;
    GlobalBindlessSetError error = GlobalBindlessSetError::None;

    /// Reports whether publication produced one valid descriptor handle.
    /// @return True only when the descriptor update and slot publication both succeeded.
    [[nodiscard]] constexpr bool succeeded() const { return error == GlobalBindlessSetError::None && handle.isValid(); }
};

/// Reports reclaim results independently for every descriptor-array domain.
struct GlobalBindlessReclaimResult final {
    BindlessDescriptorSlotReclaimResult sampledTexture2D;
    BindlessDescriptorSlotReclaimResult sampledTextureCube;
    BindlessDescriptorSlotReclaimResult samplers;
    BindlessDescriptorSlotReclaimResult storageBuffers;
};

/// Reports current occupancy for every descriptor-array domain and fixed TLAS descriptor update count.
struct GlobalBindlessSetStats final {
    BindlessDescriptorSlotStats sampledTexture2D;
    BindlessDescriptorSlotStats sampledTextureCube;
    BindlessDescriptorSlotStats samplers;
    BindlessDescriptorSlotStats storageBuffers;
    uint64_t accelerationStructureUpdateCount = 0u;
};

/// Owns the Vulkan-only frame-stable descriptor set used by modern GPU scene paths.
/// OpenGL callers are rejected explicitly and never receive a simulated bindless table.
class GlobalBindlessSet final {
public:
    GlobalBindlessSet() = default;
    ~GlobalBindlessSet();

    GlobalBindlessSet(const GlobalBindlessSet&) = delete;
    GlobalBindlessSet& operator=(const GlobalBindlessSet&) = delete;

    /// Creates the immutable layout and descriptor set after validating every required Vulkan capability.
    /// @param rhiDevice Initialized Vulkan RHI device that owns all created handles.
    /// @param config Fixed descriptor counts bounded by update-after-bind device limits.
    /// @return None on success or a stable initialization failure without partial live state.
    [[nodiscard]] GlobalBindlessSetError initialize(RhiDevice& rhiDevice, const GlobalBindlessSetConfig& config);

    /// Destroys the descriptor set and layout and invalidates every CPU publication handle.
    /// The owner must stop submitting users of this set before shutdown.
    void shutdown();

    /// Publishes one sampled two-dimensional texture view into a new descriptor slot.
    /// @param textureView Live Texture2D view created with sampled usage.
    /// @return Strong slot handle or a structured validation/publication failure.
    [[nodiscard]] GlobalBindlessPublicationResult<renderer::contracts::BindlessTexture2DTag>
    publishTexture2D(RhiTextureViewHandle textureView);

    /// Publishes one sampled cube texture view into a new descriptor slot.
    /// @param textureView Live Cube view created with sampled usage.
    /// @return Strong slot handle or a structured validation/publication failure.
    [[nodiscard]] GlobalBindlessPublicationResult<renderer::contracts::BindlessTextureCubeTag>
    publishTextureCube(RhiTextureViewHandle textureView);

    /// Publishes one sampler into a new descriptor slot.
    /// @param sampler Live sampler owned by the initialized RHI device.
    /// @return Strong slot handle or a structured validation/publication failure.
    [[nodiscard]] GlobalBindlessPublicationResult<renderer::contracts::BindlessSamplerTag>
    publishSampler(RhiSamplerHandle sampler);

    /// Publishes one complete storage buffer into a new descriptor slot.
    /// @param buffer Live buffer created with Storage usage.
    /// @return Strong slot handle or a structured validation/publication failure.
    [[nodiscard]] GlobalBindlessPublicationResult<renderer::contracts::BindlessStorageBufferTag>
    publishStorageBuffer(RhiBufferHandle buffer);

    /// Publishes the current top-level acceleration structure at fixed binding 4.
    /// @param accelerationStructure Live top-level structure owned by the initialized device.
    /// @return None after the descriptor is updated, or a stable validation/publication error.
    [[nodiscard]] GlobalBindlessSetError setAccelerationStructure(RhiAccelerationStructureHandle accelerationStructure);

    /// Retires one sampled two-dimensional texture slot after its newest referencing submission.
    [[nodiscard]] GlobalBindlessSetError retire(renderer::contracts::BindlessTexture2DHandle handle,
                                                uint64_t lastUseSequence);
    /// Retires one sampled cube texture slot after its newest referencing submission.
    [[nodiscard]] GlobalBindlessSetError retire(renderer::contracts::BindlessTextureCubeHandle handle,
                                                uint64_t lastUseSequence);
    /// Retires one sampler slot after its newest referencing submission.
    [[nodiscard]] GlobalBindlessSetError retire(renderer::contracts::BindlessSamplerHandle handle,
                                                uint64_t lastUseSequence);
    /// Retires one storage-buffer slot after its newest referencing submission.
    [[nodiscard]] GlobalBindlessSetError retire(renderer::contracts::BindlessStorageBufferHandle handle,
                                                uint64_t lastUseSequence);

    /// Atomically validates and retires multiple distinct storage-buffer slots.
    /// @param handles Array of exact live storage-buffer generations.
    /// @param handleCount Number of handles in the batch.
    /// @param lastUseSequence Newest GPU submission that may read any handle in the batch.
    /// @return None when every handle was retired or one stable error without mutation.
    [[nodiscard]] GlobalBindlessSetError
    retireStorageBuffers(const renderer::contracts::BindlessStorageBufferHandle* handles, uint32_t handleCount,
                         uint64_t lastUseSequence);

    /// Reclaims every descriptor slot whose final referencing submission completed.
    /// @param completedSequence Greatest contiguous completed device submission sequence.
    /// @return Per-domain counts of reusable and permanently exhausted slots.
    [[nodiscard]] GlobalBindlessReclaimResult reclaim(uint64_t completedSequence);

    /// Returns the immutable layout handle used when creating modern pipelines.
    [[nodiscard]] RhiBindGroupLayoutHandle layout() const { return m_layout; }
    /// Returns the frame-stable descriptor set bound by modern draw and compute paths.
    [[nodiscard]] RhiBindGroupHandle bindGroup() const { return m_bindGroup; }
    /// Reports whether the Vulkan descriptor set is fully initialized.
    [[nodiscard]] bool initialized() const {
        return m_device != nullptr && m_layout.isValid() && m_bindGroup.isValid();
    }
    /// Returns constant-time per-domain occupancy statistics.
    [[nodiscard]] GlobalBindlessSetStats stats() const;

private:
    [[nodiscard]] bool updateResource(renderer::contracts::GlobalBindlessBinding binding, uint32_t index,
                                      const RhiBindingResource& resource);
    [[nodiscard]] bool validTextureView(RhiTextureViewHandle textureView, RhiTextureViewType requiredType) const;

    RhiDevice* m_device = nullptr;
    RhiBindGroupLayoutHandle m_layout;
    RhiBindGroupHandle m_bindGroup;
    BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTexture2DTag> m_sampledTexture2DSlots{0u};
    BindlessDescriptorSlotAllocator<renderer::contracts::BindlessTextureCubeTag> m_sampledTextureCubeSlots{0u};
    BindlessDescriptorSlotAllocator<renderer::contracts::BindlessSamplerTag> m_samplerSlots{0u};
    BindlessDescriptorSlotAllocator<renderer::contracts::BindlessStorageBufferTag> m_storageBufferSlots{0u};
    RhiAccelerationStructureHandle m_accelerationStructure;
    uint64_t m_accelerationStructureUpdateCount = 0u;
};

} // namespace renderer::core

#endif // MECRAFT_GLOBAL_BINDLESS_SET_H
