#ifndef MECRAFT_GPU_SCENE_BUFFER_SET_H
#define MECRAFT_GPU_SCENE_BUFFER_SET_H

#include "renderer/contracts/GpuMaterialContract.h"
#include "renderer/contracts/GpuSceneContract.h"
#include "renderer/core/GlobalBindlessSet.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiTypes.h"

#include <cstdint>
#include <limits>
#include <vector>

class RhiCommandList;
class RhiDevice;

namespace renderer::core {

/// Identifies deterministic fixed-capacity GPU scene buffer failures.
enum class GpuSceneBufferSetError : uint8_t {
    None,
    AlreadyInitialized,
    NotInitialized,
    InvalidCapacity,
    SizeOverflow,
    BufferCreationFailed,
    BindlessPublicationFailed,
    CapacityExceeded,
    CommandListNotRecording,
    UploadAwaitingSubmission,
    InvalidSubmissionToken,
    BindlessRetirementFailed
};

/// Returns the stable identifier used by logs and automated validation.
/// @param error GPU scene buffer failure to identify.
/// @return Process-lifetime string containing the stable identifier.
[[nodiscard]] const char* gpuSceneBufferSetErrorStableId(GpuSceneBufferSetError error);

/// Defines immutable table capacities for one Material/Geometry/Instance buffer set.
struct GpuSceneBufferSetConfig final {
    uint32_t materialCapacity = 4096u;
    uint32_t geometryCapacity = 16384u;
    uint32_t instanceCapacity = 16384u;
};

/// Reports one table's capacity, active record count, peak count, and pending upload span.
struct GpuSceneTableStats final {
    uint32_t capacity = 0u;
    uint32_t activeCount = 0u;
    uint32_t peakActiveCount = 0u;
    uint32_t dirtyRecordCount = 0u;
};

/// Reports CPU table occupancy and cumulative upload bytes for the complete set.
struct GpuSceneBufferSetStats final {
    GpuSceneTableStats materials;
    GpuSceneTableStats geometries;
    GpuSceneTableStats instances;
    uint64_t uploadedBytes = 0u;
};

/// Owns fixed-capacity GPU scene tables and records contiguous dirty-span uploads.
/// The owner writes normalized records, records uploads before consumers, and stamps every using submission.
class GpuSceneBufferSet final {
public:
    GpuSceneBufferSet() = default;
    ~GpuSceneBufferSet();

    GpuSceneBufferSet(const GpuSceneBufferSet&) = delete;
    GpuSceneBufferSet& operator=(const GpuSceneBufferSet&) = delete;

    /// Creates three GPU-only storage buffers and publishes them into the Global Bindless Set.
    /// @param rhiDevice Initialized Vulkan device used by the bindless set.
    /// @param bindlessSet Initialized global descriptor owner.
    /// @param config Fixed table capacities; zero and byte-size overflow are rejected.
    /// @return None on success or a stable failure with all partial resources released.
    [[nodiscard]] GpuSceneBufferSetError initialize(RhiDevice& rhiDevice, GlobalBindlessSet& bindlessSet,
                                                    const GpuSceneBufferSetConfig& config);

    /// Retires bindless slots using the newest stamped submission and destroys all public buffers.
    /// @return None when every live slot retired successfully.
    [[nodiscard]] GpuSceneBufferSetError shutdown();

    /// Writes one material record and expands the contiguous dirty upload span.
    [[nodiscard]] GpuSceneBufferSetError writeMaterial(uint32_t index,
                                                       const renderer::contracts::GpuMaterial& material);
    /// Writes one geometry record and expands the contiguous dirty upload span.
    [[nodiscard]] GpuSceneBufferSetError writeGeometry(uint32_t index,
                                                       const renderer::contracts::GpuSceneGeometry& geometry);
    /// Writes one instance record and expands the contiguous dirty upload span.
    [[nodiscard]] GpuSceneBufferSetError writeInstance(uint32_t index,
                                                       const renderer::contracts::GpuSceneInstance& instance);

    /// Records transfer uploads for every dirty table and restores StorageBuffer state before consumers.
    /// @param commandList Recording command list ordered before all scene-buffer readers.
    /// @return None when every dirty span was valid and recorded.
    [[nodiscard]] GpuSceneBufferSetError recordUploads(RhiCommandList& commandList);

    /// Discards one recorded upload batch after command recording or submission fails.
    /// Dirty CPU spans remain intact and can be recorded again.
    [[nodiscard]] GpuSceneBufferSetError discardRecordedUploads();

    /// Records the newest submission that may reference any published scene buffer.
    /// @param token Valid non-present submission token returned by the owning device.
    /// @return None when the monotonic sequence was accepted.
    [[nodiscard]] GpuSceneBufferSetError markSubmitted(RhiSubmissionToken token);

    /// Reports whether all buffers and bindless handles are initialized.
    [[nodiscard]] bool initialized() const;
    /// Returns constant-time occupancy, dirty-span, and upload statistics.
    [[nodiscard]] GpuSceneBufferSetStats stats() const;

    [[nodiscard]] RhiBufferHandle materialBuffer() const { return m_materialBuffer; }
    [[nodiscard]] RhiBufferHandle geometryBuffer() const { return m_geometryBuffer; }
    [[nodiscard]] RhiBufferHandle instanceBuffer() const { return m_instanceBuffer; }
    [[nodiscard]] renderer::contracts::BindlessStorageBufferHandle materialHandle() const { return m_materialHandle; }
    [[nodiscard]] renderer::contracts::BindlessStorageBufferHandle geometryHandle() const { return m_geometryHandle; }
    [[nodiscard]] renderer::contracts::BindlessStorageBufferHandle instanceHandle() const { return m_instanceHandle; }

private:
    struct DirtySpan final {
        uint32_t first = std::numeric_limits<uint32_t>::max();
        uint32_t last = 0u;

        [[nodiscard]] bool valid() const { return first < last; }
        [[nodiscard]] uint32_t count() const { return valid() ? last - first : 0u; }
        void include(uint32_t index);
        void clear();
    };

    template <typename Record>
    [[nodiscard]] static GpuSceneBufferSetError
    writeRecord(std::vector<Record>& records, uint32_t index, const Record& record, DirtySpan& dirty,
                uint32_t& activeCount, uint32_t& peakActiveCount, uint64_t& revision);
    template <typename Record>
    static void uploadTable(RhiCommandList& commandList, RhiBufferHandle buffer, const std::vector<Record>& records,
                            const DirtySpan& dirty);
    void releasePartialInitialization();

    RhiDevice* m_device = nullptr;
    GlobalBindlessSet* m_bindlessSet = nullptr;
    RhiBufferHandle m_materialBuffer;
    RhiBufferHandle m_geometryBuffer;
    RhiBufferHandle m_instanceBuffer;
    renderer::contracts::BindlessStorageBufferHandle m_materialHandle;
    renderer::contracts::BindlessStorageBufferHandle m_geometryHandle;
    renderer::contracts::BindlessStorageBufferHandle m_instanceHandle;
    std::vector<renderer::contracts::GpuMaterial> m_materials;
    std::vector<renderer::contracts::GpuSceneGeometry> m_geometries;
    std::vector<renderer::contracts::GpuSceneInstance> m_instances;
    DirtySpan m_materialDirty;
    DirtySpan m_geometryDirty;
    DirtySpan m_instanceDirty;
    DirtySpan m_recordedMaterialDirty;
    DirtySpan m_recordedGeometryDirty;
    DirtySpan m_recordedInstanceDirty;
    uint32_t m_materialCount = 0u;
    uint32_t m_geometryCount = 0u;
    uint32_t m_instanceCount = 0u;
    uint32_t m_peakMaterialCount = 0u;
    uint32_t m_peakGeometryCount = 0u;
    uint32_t m_peakInstanceCount = 0u;
    uint64_t m_materialRevision = 0u;
    uint64_t m_geometryRevision = 0u;
    uint64_t m_instanceRevision = 0u;
    uint64_t m_recordedMaterialRevision = 0u;
    uint64_t m_recordedGeometryRevision = 0u;
    uint64_t m_recordedInstanceRevision = 0u;
    uint64_t m_lastUseSequence = 0u;
    uint64_t m_uploadedBytes = 0u;
    uint64_t m_recordedUploadBytes = 0u;
    bool m_uploadAwaitingSubmission = false;
};

} // namespace renderer::core

#endif // MECRAFT_GPU_SCENE_BUFFER_SET_H
