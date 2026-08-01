#include "renderer/core/GpuSceneBufferSet.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace renderer::core {
namespace {

template <typename Record> [[nodiscard]] bool tableSizeBytes(const uint32_t capacity, uint64_t& size) {
    if (capacity == 0u || capacity > std::numeric_limits<uint64_t>::max() / sizeof(Record)) {
        return false;
    }
    size = static_cast<uint64_t>(capacity) * sizeof(Record);
    return true;
}

[[nodiscard]] RhiBufferHandle createSceneBuffer(RhiDevice& device, const uint64_t size, const char* debugName) {
    RhiBufferDesc desc;
    desc.debugName = debugName;
    desc.size = size;
    desc.usage = rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst);
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::StorageBuffer;
    desc.memoryCategory = RhiMemoryCategory::SceneData;
    return device.createBuffer(desc, nullptr, 0u);
}

} // namespace

GpuSceneBufferSet::~GpuSceneBufferSet() {
    (void)shutdown();
}

GpuSceneBufferSetError GpuSceneBufferSet::initialize(RhiDevice& rhiDevice, GlobalBindlessSet& bindlessSet,
                                                     const GpuSceneBufferSetConfig& config) {
    if (initialized()) {
        return GpuSceneBufferSetError::AlreadyInitialized;
    }
    if (!bindlessSet.initialized()) {
        return GpuSceneBufferSetError::BindlessPublicationFailed;
    }
    uint64_t materialBytes = 0u;
    uint64_t geometryBytes = 0u;
    uint64_t instanceBytes = 0u;
    if (config.materialCapacity == 0u || config.geometryCapacity == 0u || config.instanceCapacity == 0u) {
        return GpuSceneBufferSetError::InvalidCapacity;
    }
    if (!tableSizeBytes<renderer::contracts::GpuMaterial>(config.materialCapacity, materialBytes) ||
        !tableSizeBytes<renderer::contracts::GpuSceneGeometry>(config.geometryCapacity, geometryBytes) ||
        !tableSizeBytes<renderer::contracts::GpuSceneInstance>(config.instanceCapacity, instanceBytes)) {
        return GpuSceneBufferSetError::SizeOverflow;
    }

    m_materials.resize(config.materialCapacity);
    m_geometries.resize(config.geometryCapacity);
    m_instances.resize(config.instanceCapacity);
    m_device = &rhiDevice;
    m_bindlessSet = &bindlessSet;
    m_materialBuffer = createSceneBuffer(rhiDevice, materialBytes, "GpuScene.Materials");
    m_geometryBuffer = createSceneBuffer(rhiDevice, geometryBytes, "GpuScene.Geometries");
    m_instanceBuffer = createSceneBuffer(rhiDevice, instanceBytes, "GpuScene.Instances");
    if (!m_materialBuffer.isValid() || !m_geometryBuffer.isValid() || !m_instanceBuffer.isValid()) {
        releasePartialInitialization();
        return GpuSceneBufferSetError::BufferCreationFailed;
    }

    const auto materialPublication = bindlessSet.publishStorageBuffer(m_materialBuffer);
    if (!materialPublication.succeeded()) {
        releasePartialInitialization();
        return GpuSceneBufferSetError::BindlessPublicationFailed;
    }
    m_materialHandle = materialPublication.handle;
    const auto geometryPublication = bindlessSet.publishStorageBuffer(m_geometryBuffer);
    if (!geometryPublication.succeeded()) {
        releasePartialInitialization();
        return GpuSceneBufferSetError::BindlessPublicationFailed;
    }
    m_geometryHandle = geometryPublication.handle;
    const auto instancePublication = bindlessSet.publishStorageBuffer(m_instanceBuffer);
    if (!instancePublication.succeeded()) {
        releasePartialInitialization();
        return GpuSceneBufferSetError::BindlessPublicationFailed;
    }
    m_instanceHandle = instancePublication.handle;
    return GpuSceneBufferSetError::None;
}

GpuSceneBufferSetError GpuSceneBufferSet::shutdown() {
    if (m_device == nullptr && m_bindlessSet == nullptr) {
        return GpuSceneBufferSetError::None;
    }
    if (m_uploadAwaitingSubmission) {
        return GpuSceneBufferSetError::UploadAwaitingSubmission;
    }
    if (m_bindlessSet == nullptr || !m_bindlessSet->initialized()) {
        return GpuSceneBufferSetError::BindlessRetirementFailed;
    }
    const std::array<renderer::contracts::BindlessStorageBufferHandle, 3u> handles{m_materialHandle, m_geometryHandle,
                                                                                   m_instanceHandle};
    if (m_bindlessSet->retireStorageBuffers(handles.data(), static_cast<uint32_t>(handles.size()), m_lastUseSequence) !=
        GlobalBindlessSetError::None) {
        return GpuSceneBufferSetError::BindlessRetirementFailed;
    }
    if (m_device != nullptr) {
        if (m_materialBuffer.isValid()) {
            m_device->destroyBuffer(m_materialBuffer);
        }
        if (m_geometryBuffer.isValid()) {
            m_device->destroyBuffer(m_geometryBuffer);
        }
        if (m_instanceBuffer.isValid()) {
            m_device->destroyBuffer(m_instanceBuffer);
        }
    }

    m_device = nullptr;
    m_bindlessSet = nullptr;
    m_materialBuffer = {};
    m_geometryBuffer = {};
    m_instanceBuffer = {};
    m_materialHandle = {};
    m_geometryHandle = {};
    m_instanceHandle = {};
    m_materials.clear();
    m_geometries.clear();
    m_instances.clear();
    m_materialDirty.clear();
    m_geometryDirty.clear();
    m_instanceDirty.clear();
    m_recordedMaterialDirty.clear();
    m_recordedGeometryDirty.clear();
    m_recordedInstanceDirty.clear();
    m_materialCount = 0u;
    m_geometryCount = 0u;
    m_instanceCount = 0u;
    m_peakMaterialCount = 0u;
    m_peakGeometryCount = 0u;
    m_peakInstanceCount = 0u;
    m_materialRevision = 0u;
    m_geometryRevision = 0u;
    m_instanceRevision = 0u;
    m_recordedMaterialRevision = 0u;
    m_recordedGeometryRevision = 0u;
    m_recordedInstanceRevision = 0u;
    m_lastUseSequence = 0u;
    m_uploadedBytes = 0u;
    m_recordedUploadBytes = 0u;
    m_uploadAwaitingSubmission = false;
    return GpuSceneBufferSetError::None;
}

GpuSceneBufferSetError GpuSceneBufferSet::writeMaterial(const uint32_t index,
                                                        const renderer::contracts::GpuMaterial& material) {
    return initialized() ? writeRecord(m_materials, index, material, m_materialDirty, m_materialCount,
                                       m_peakMaterialCount, m_materialRevision)
                         : GpuSceneBufferSetError::NotInitialized;
}

GpuSceneBufferSetError GpuSceneBufferSet::writeGeometry(const uint32_t index,
                                                        const renderer::contracts::GpuSceneGeometry& geometry) {
    return initialized() ? writeRecord(m_geometries, index, geometry, m_geometryDirty, m_geometryCount,
                                       m_peakGeometryCount, m_geometryRevision)
                         : GpuSceneBufferSetError::NotInitialized;
}

GpuSceneBufferSetError GpuSceneBufferSet::writeInstance(const uint32_t index,
                                                        const renderer::contracts::GpuSceneInstance& instance) {
    return initialized() ? writeRecord(m_instances, index, instance, m_instanceDirty, m_instanceCount,
                                       m_peakInstanceCount, m_instanceRevision)
                         : GpuSceneBufferSetError::NotInitialized;
}

GpuSceneBufferSetError GpuSceneBufferSet::recordUploads(RhiCommandList& commandList) {
    if (!initialized()) {
        return GpuSceneBufferSetError::NotInitialized;
    }
    if (commandList.state() != RhiCommandListState::Recording) {
        return GpuSceneBufferSetError::CommandListNotRecording;
    }
    if (m_uploadAwaitingSubmission) {
        return GpuSceneBufferSetError::UploadAwaitingSubmission;
    }
    uint64_t uploadBytes = 0u;
    const auto appendBytes = [&](const uint32_t count, const uint64_t recordSize) {
        const uint64_t bytes = static_cast<uint64_t>(count) * recordSize;
        if (bytes > std::numeric_limits<uint64_t>::max() - uploadBytes) {
            return false;
        }
        uploadBytes += bytes;
        return true;
    };
    if (!appendBytes(m_materialDirty.count(), sizeof(renderer::contracts::GpuMaterial)) ||
        !appendBytes(m_geometryDirty.count(), sizeof(renderer::contracts::GpuSceneGeometry)) ||
        !appendBytes(m_instanceDirty.count(), sizeof(renderer::contracts::GpuSceneInstance)) ||
        uploadBytes > std::numeric_limits<uint64_t>::max() - m_uploadedBytes) {
        return GpuSceneBufferSetError::SizeOverflow;
    }
    uploadTable(commandList, m_materialBuffer, m_materials, m_materialDirty);
    uploadTable(commandList, m_geometryBuffer, m_geometries, m_geometryDirty);
    uploadTable(commandList, m_instanceBuffer, m_instances, m_instanceDirty);
    if (uploadBytes != 0u) {
        m_recordedMaterialDirty = m_materialDirty;
        m_recordedGeometryDirty = m_geometryDirty;
        m_recordedInstanceDirty = m_instanceDirty;
        m_recordedMaterialRevision = m_materialRevision;
        m_recordedGeometryRevision = m_geometryRevision;
        m_recordedInstanceRevision = m_instanceRevision;
        m_recordedUploadBytes = uploadBytes;
        m_uploadAwaitingSubmission = true;
    }
    return GpuSceneBufferSetError::None;
}

GpuSceneBufferSetError GpuSceneBufferSet::discardRecordedUploads() {
    if (!initialized()) {
        return GpuSceneBufferSetError::NotInitialized;
    }
    m_recordedMaterialDirty.clear();
    m_recordedGeometryDirty.clear();
    m_recordedInstanceDirty.clear();
    m_recordedMaterialRevision = 0u;
    m_recordedGeometryRevision = 0u;
    m_recordedInstanceRevision = 0u;
    m_recordedUploadBytes = 0u;
    m_uploadAwaitingSubmission = false;
    return GpuSceneBufferSetError::None;
}

GpuSceneBufferSetError GpuSceneBufferSet::markSubmitted(const RhiSubmissionToken token) {
    if (!initialized()) {
        return GpuSceneBufferSetError::NotInitialized;
    }
    bool complete = false;
    if (!token.isValid() || !m_device->isSubmissionComplete(token, complete)) {
        return GpuSceneBufferSetError::InvalidSubmissionToken;
    }
    if (m_uploadAwaitingSubmission) {
        if (m_materialRevision == m_recordedMaterialRevision) {
            m_materialDirty.clear();
        }
        if (m_geometryRevision == m_recordedGeometryRevision) {
            m_geometryDirty.clear();
        }
        if (m_instanceRevision == m_recordedInstanceRevision) {
            m_instanceDirty.clear();
        }
        m_uploadedBytes += m_recordedUploadBytes;
        (void)discardRecordedUploads();
    }
    m_lastUseSequence = std::max(m_lastUseSequence, token.sequence);
    return GpuSceneBufferSetError::None;
}

bool GpuSceneBufferSet::initialized() const {
    return m_device != nullptr && m_bindlessSet != nullptr && m_materialBuffer.isValid() &&
           m_geometryBuffer.isValid() && m_instanceBuffer.isValid() && m_materialHandle.isValid() &&
           m_geometryHandle.isValid() && m_instanceHandle.isValid();
}

GpuSceneBufferSetStats GpuSceneBufferSet::stats() const {
    return {{static_cast<uint32_t>(m_materials.size()), m_materialCount, m_peakMaterialCount, m_materialDirty.count()},
            {static_cast<uint32_t>(m_geometries.size()), m_geometryCount, m_peakGeometryCount, m_geometryDirty.count()},
            {static_cast<uint32_t>(m_instances.size()), m_instanceCount, m_peakInstanceCount, m_instanceDirty.count()},
            m_uploadedBytes};
}

void GpuSceneBufferSet::DirtySpan::include(const uint32_t index) {
    first = std::min(first, index);
    last = std::max(last, index + 1u);
}

void GpuSceneBufferSet::DirtySpan::clear() {
    first = std::numeric_limits<uint32_t>::max();
    last = 0u;
}

template <typename Record>
GpuSceneBufferSetError GpuSceneBufferSet::writeRecord(std::vector<Record>& records, const uint32_t index,
                                                      const Record& record, DirtySpan& dirty, uint32_t& activeCount,
                                                      uint32_t& peakActiveCount, uint64_t& revision) {
    if (index >= records.size()) {
        return GpuSceneBufferSetError::CapacityExceeded;
    }
    if (revision == std::numeric_limits<uint64_t>::max()) {
        return GpuSceneBufferSetError::SizeOverflow;
    }
    records[index] = record;
    dirty.include(index);
    ++revision;
    activeCount = std::max(activeCount, index + 1u);
    peakActiveCount = std::max(peakActiveCount, activeCount);
    return GpuSceneBufferSetError::None;
}

template <typename Record>
void GpuSceneBufferSet::uploadTable(RhiCommandList& commandList, const RhiBufferHandle buffer,
                                    const std::vector<Record>& records, const DirtySpan& dirty) {
    if (!dirty.valid()) {
        return;
    }
    const uint64_t offset = static_cast<uint64_t>(dirty.first) * sizeof(Record);
    const uint64_t size = static_cast<uint64_t>(dirty.count()) * sizeof(Record);
    commandList.bufferBarrier({buffer, RhiResourceState::StorageBuffer, RhiResourceState::TransferDst});
    commandList.updateBuffer(buffer, offset, records.data() + dirty.first, static_cast<size_t>(size));
    commandList.bufferBarrier({buffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
}

void GpuSceneBufferSet::releasePartialInitialization() {
    if (m_bindlessSet != nullptr && m_bindlessSet->initialized()) {
        if (m_materialHandle.isValid()) {
            (void)m_bindlessSet->retire(m_materialHandle, 0u);
        }
        if (m_geometryHandle.isValid()) {
            (void)m_bindlessSet->retire(m_geometryHandle, 0u);
        }
        if (m_instanceHandle.isValid()) {
            (void)m_bindlessSet->retire(m_instanceHandle, 0u);
        }
        (void)m_bindlessSet->reclaim(0u);
    }
    if (m_device != nullptr) {
        if (m_materialBuffer.isValid()) {
            m_device->destroyBuffer(m_materialBuffer);
        }
        if (m_geometryBuffer.isValid()) {
            m_device->destroyBuffer(m_geometryBuffer);
        }
        if (m_instanceBuffer.isValid()) {
            m_device->destroyBuffer(m_instanceBuffer);
        }
    }
    m_device = nullptr;
    m_bindlessSet = nullptr;
    m_materialBuffer = {};
    m_geometryBuffer = {};
    m_instanceBuffer = {};
    m_materialHandle = {};
    m_geometryHandle = {};
    m_instanceHandle = {};
    m_materials.clear();
    m_geometries.clear();
    m_instances.clear();
}

const char* gpuSceneBufferSetErrorStableId(const GpuSceneBufferSetError error) {
    switch (error) {
    case GpuSceneBufferSetError::None: return "None";
    case GpuSceneBufferSetError::AlreadyInitialized: return "AlreadyInitialized";
    case GpuSceneBufferSetError::NotInitialized: return "NotInitialized";
    case GpuSceneBufferSetError::InvalidCapacity: return "InvalidCapacity";
    case GpuSceneBufferSetError::SizeOverflow: return "SizeOverflow";
    case GpuSceneBufferSetError::BufferCreationFailed: return "GpuSceneBufferCreationFailed";
    case GpuSceneBufferSetError::BindlessPublicationFailed: return "GpuSceneBindlessPublicationFailed";
    case GpuSceneBufferSetError::CapacityExceeded: return "GpuSceneCapacityExceeded";
    case GpuSceneBufferSetError::CommandListNotRecording: return "CommandListNotRecording";
    case GpuSceneBufferSetError::UploadAwaitingSubmission: return "GpuSceneUploadAwaitingSubmission";
    case GpuSceneBufferSetError::InvalidSubmissionToken: return "InvalidSubmissionToken";
    case GpuSceneBufferSetError::BindlessRetirementFailed: return "GpuSceneBindlessRetirementFailed";
    }
    std::abort();
}

} // namespace renderer::core
