#include "TerrainBlasCache.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "../../world/chunk/Chunk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {

constexpr RhiBufferUsageFlags kGeometryBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::DeviceAddress) |
    rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
constexpr RhiBufferUsageFlags kScratchBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
constexpr RhiAccelerationStructureBuildFlags kTerrainBuildFlags =
    rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);

static_assert(offsetof(BlockVertex, x) == 0u && offsetof(BlockVertex, y) == sizeof(float) &&
                  offsetof(BlockVertex, z) == sizeof(float) * 2u,
              "Terrain BLAS positions must occupy the first three floats of BlockVertex");

[[nodiscard]] bool finitePosition(const BlockVertex& vertex) {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool validPreparedGeometry(const TerrainBlasGeometry& geometry) {
    const uint64_t totalVertexCount = static_cast<uint64_t>(geometry.opaqueVertexCount) + geometry.cutoutVertexCount;
    if (geometry.opaqueVertexCount % 3u != 0u || geometry.cutoutVertexCount % 3u != 0u ||
        totalVertexCount > std::numeric_limits<uint32_t>::max() || totalVertexCount != geometry.vertices.size()) {
        return false;
    }
    return std::all_of(geometry.vertices.begin(), geometry.vertices.end(), finitePosition);
}

} // namespace

bool TerrainBlasCache::init(RhiDevice* device) {
    shutdown();
    m_device = device;
    m_initialized = true;
    m_healthy = true;
    m_supported =
        device != nullptr && device->capabilities().accelerationStructure && device->capabilities().bufferDeviceAddress;
    if (!m_supported) {
        return true;
    }

    m_compactedSizeQueries =
        device->createQueryPool({"Terrain.BLAS.CompactedSizeQueries", RhiQueryType::AccelerationStructureCompactedSize,
                                 kCompactedSizeQueryCapacity});
    if (!m_compactedSizeQueries.isValid()) {
        setFatalError("Terrain BLAS compacted-size query pool creation failed");
        return false;
    }

    m_freeQueryIndices.reserve(kCompactedSizeQueryCapacity);
    for (uint32_t queryIndex = 0u; queryIndex < kCompactedSizeQueryCapacity; ++queryIndex) {
        m_freeQueryIndices.push_back(queryIndex);
    }
    return true;
}

void TerrainBlasCache::shutdown() {
    if (m_device != nullptr) {
        for (auto& [_, entry] : m_entries) {
            if (entry.active.has_value()) {
                destroyActiveResource(*entry.active);
            }
        }
        for (auto& [_, task] : m_tasks) {
            destroyTaskResources(task);
        }
        if (m_compactedSizeQueries.isValid()) {
            m_device->destroyQueryPool(m_compactedSizeQueries);
        }
    }

    m_device = nullptr;
    m_compactedSizeQueries = {};
    m_initialized = false;
    m_supported = false;
    m_healthy = true;
    m_nextRequestSequence = 1u;
    m_entries.clear();
    m_tasks.clear();
    m_freeQueryIndices.clear();
    m_quarantinedQueries.clear();
    m_recordedBuilds.clear();
    m_recordedCompactions.clear();
    m_buildsRecordedThisFrame = 0u;
    m_compactionsRecordedThisFrame = 0u;
    m_scratchBytesRecordedThisFrame = 0u;
    m_scratchPeakBytesThisFrame = 0u;
    m_lastError.clear();
}

void TerrainBlasCache::beginFrame() {
    m_buildsRecordedThisFrame = 0u;
    m_compactionsRecordedThisFrame = 0u;
    m_scratchBytesRecordedThisFrame = 0u;
    m_scratchPeakBytesThisFrame = 0u;
    if (m_healthy) {
        m_lastError.clear();
        (void)pollCompletedTasks();
    }
}

void TerrainBlasCache::setBudgets(const TerrainBlasBudgets& budgets) {
    m_budgets.maxBuilds = std::max(1u, budgets.maxBuilds);
    m_budgets.maxBuildGeometryBytes = std::max<uint64_t>(1u, budgets.maxBuildGeometryBytes);
    m_budgets.maxBuildPrimitives = std::max<uint64_t>(1u, budgets.maxBuildPrimitives);
    m_budgets.maxCompactions = std::max(1u, budgets.maxCompactions);
}

TerrainBlasRequestResult TerrainBlasCache::prepareGeometry(const std::vector<BlockVertex>& opaque,
                                                           const std::vector<BlockVertex>& cutout,
                                                           const std::vector<BlockVertex>& cutoutDistance,
                                                           TerrainBlasGeometry& geometry) {
    geometry = {};
    const uint64_t opaqueCount = opaque.size();
    const uint64_t cutoutCount = cutout.size() + static_cast<uint64_t>(cutoutDistance.size());
    const uint64_t totalCount = opaqueCount + cutoutCount;
    if (opaque.size() % 3u != 0u || cutout.size() % 3u != 0u || cutoutDistance.size() % 3u != 0u ||
        opaqueCount > std::numeric_limits<uint32_t>::max() || cutoutCount > std::numeric_limits<uint32_t>::max() ||
        totalCount > std::numeric_limits<uint32_t>::max() ||
        !std::all_of(opaque.begin(), opaque.end(), finitePosition) ||
        !std::all_of(cutout.begin(), cutout.end(), finitePosition) ||
        !std::all_of(cutoutDistance.begin(), cutoutDistance.end(), finitePosition)) {
        return TerrainBlasRequestResult::InvalidGeometry;
    }

    geometry.opaqueVertexCount = static_cast<uint32_t>(opaqueCount);
    geometry.cutoutVertexCount = static_cast<uint32_t>(cutoutCount);
    geometry.vertices.reserve(static_cast<size_t>(totalCount));
    geometry.vertices.insert(geometry.vertices.end(), opaque.begin(), opaque.end());
    geometry.vertices.insert(geometry.vertices.end(), cutout.begin(), cutout.end());
    geometry.vertices.insert(geometry.vertices.end(), cutoutDistance.begin(), cutoutDistance.end());
    return geometry.empty() ? TerrainBlasRequestResult::Cleared : TerrainBlasRequestResult::Queued;
}

bool TerrainBlasCache::validKey(const SubChunkGpuKey& key) {
    return key.scy >= 0 && key.scy < Chunk::NUM_SUB_CHUNKS;
}

TerrainBlasRequestResult TerrainBlasCache::requestBuild(const SubChunkGpuKey& key, const uint64_t revision,
                                                        const glm::vec3& worldOrigin, TerrainBlasGeometry&& geometry) {
    if (!m_initialized || !m_supported) {
        return TerrainBlasRequestResult::Unsupported;
    }
    if (!validKey(key)) {
        return TerrainBlasRequestResult::InvalidKey;
    }
    if (revision == 0u) {
        return TerrainBlasRequestResult::InvalidRevision;
    }
    if (!finiteVector(worldOrigin) || !validPreparedGeometry(geometry)) {
        return TerrainBlasRequestResult::InvalidGeometry;
    }

    auto [entryIt, _] = m_entries.try_emplace(key);
    Entry& entry = entryIt->second;
    switch (terrainBlasClassifyRevision(entry.hasRevision, entry.latestRevision, revision)) {
    case TerrainBlasRevisionRelation::Current: return TerrainBlasRequestResult::Unchanged;
    case TerrainBlasRevisionRelation::Stale: return TerrainBlasRequestResult::StaleRevision;
    case TerrainBlasRevisionRelation::Newer: break;
    }

    retireCurrentTask(entry);
    entry.hasRevision = true;
    entry.latestRevision = revision;
    if (geometry.empty()) {
        if (entry.active.has_value()) {
            destroyActiveResource(*entry.active);
            entry.active.reset();
        }
        return TerrainBlasRequestResult::Cleared;
    }
    if (m_nextRequestSequence == std::numeric_limits<uint64_t>::max()) {
        setFatalError("Terrain BLAS request sequence overflowed");
        return TerrainBlasRequestResult::InvalidRevision;
    }

    const uint64_t sequence = m_nextRequestSequence++;
    PendingTask task;
    task.schedule = {sequence, key};
    task.revision = revision;
    task.worldOrigin = worldOrigin;
    task.geometry = std::move(geometry);
    entry.currentTaskSequence = sequence;
    m_tasks.emplace(sequence, std::move(task));
    return TerrainBlasRequestResult::Queued;
}

void TerrainBlasCache::remove(const SubChunkGpuKey& key) {
    const auto entryIt = m_entries.find(key);
    if (entryIt == m_entries.end()) {
        return;
    }
    Entry& entry = entryIt->second;
    retireCurrentTask(entry);
    if (entry.active.has_value()) {
        destroyActiveResource(*entry.active);
        entry.active.reset();
    }
    m_entries.erase(entryIt);
}

bool TerrainBlasCache::recordFrame(RhiCommandList& commandList) {
    if (!m_supported) {
        return true;
    }
    if (!m_healthy || !m_recordedBuilds.empty() || !m_recordedCompactions.empty()) {
        if (m_healthy) {
            setTransientError("Terrain BLAS graph completion callback was not received");
        }
        return false;
    }
    if (!pollCompletedTasks()) {
        return false;
    }

    uint32_t compactionCount = 0u;
    for (auto& [_, task] : m_tasks) {
        if (compactionCount >= m_budgets.maxCompactions) {
            break;
        }
        if (!task.current || task.state != TaskState::ReadyToCompact) {
            continue;
        }
        if (!recordCompaction(task, commandList)) {
            return false;
        }
        ++compactionCount;
    }

    uint32_t buildCount = 0u;
    uint64_t geometryBytes = 0u;
    uint64_t primitiveCount = 0u;
    for (auto& [_, task] : m_tasks) {
        if (buildCount >= m_budgets.maxBuilds) {
            break;
        }
        if (!task.current || task.state != TaskState::Queued) {
            continue;
        }

        const uint64_t taskBytes = task.geometry.byteSize();
        const uint64_t taskPrimitives = task.geometry.primitiveCount();
        const bool fitsBudget =
            taskBytes <= m_budgets.maxBuildGeometryBytes - std::min(geometryBytes, m_budgets.maxBuildGeometryBytes) &&
            taskPrimitives <= m_budgets.maxBuildPrimitives - std::min(primitiveCount, m_budgets.maxBuildPrimitives);
        if (!fitsBudget && buildCount != 0u) {
            continue;
        }
        if (m_freeQueryIndices.empty()) {
            break;
        }
        if (!recordBuild(task, commandList)) {
            return false;
        }
        if (task.state == TaskState::Queued) {
            break;
        }
        ++buildCount;
        geometryBytes += taskBytes;
        primitiveCount += taskPrimitives;
    }
    return true;
}

void TerrainBlasCache::finishGraphExecution(const bool succeeded, const RhiSubmissionToken completionToken) {
    if (m_recordedBuilds.empty() && m_recordedCompactions.empty()) {
        return;
    }

    bool commit = succeeded;
    if (commit && !completionToken.isValid()) {
        setTransientError("Terrain BLAS graph submission token is invalid");
        commit = false;
    }

    for (const uint64_t sequence : m_recordedBuilds) {
        const auto taskIt = m_tasks.find(sequence);
        if (taskIt == m_tasks.end()) {
            continue;
        }
        PendingTask& task = taskIt->second;
        if (task.state != TaskState::BuildRecorded) {
            continue;
        }
        if (commit) {
            task.state = TaskState::BuildSubmitted;
            task.submissionToken = completionToken;
            if (task.scratchBuffer.isValid()) {
                m_device->destroyBuffer(task.scratchBuffer);
                task.scratchBuffer = {};
            }
            std::vector<BlockVertex>().swap(task.geometry.vertices);
            continue;
        }

        if (completionToken.isValid()) {
            quarantineQueryIndex(task, completionToken);
        } else {
            releaseQueryIndex(task);
        }
        destroyBuildAttempt(task);
        if (taskIsCurrent(task)) {
            task.state = TaskState::Queued;
        } else {
            m_tasks.erase(taskIt);
        }
    }

    for (const uint64_t sequence : m_recordedCompactions) {
        const auto taskIt = m_tasks.find(sequence);
        if (taskIt == m_tasks.end()) {
            continue;
        }
        PendingTask& task = taskIt->second;
        if (task.state != TaskState::CompactRecorded) {
            continue;
        }
        if (commit) {
            task.state = TaskState::CompactSubmitted;
            task.submissionToken = completionToken;
            continue;
        }

        destroyCompactAttempt(task);
        if (taskIsCurrent(task)) {
            task.state = TaskState::ReadyToCompact;
        } else {
            destroyTaskResources(task);
            m_tasks.erase(taskIt);
        }
    }

    m_recordedBuilds.clear();
    m_recordedCompactions.clear();
}

bool TerrainBlasCache::isSettled() const {
    if (!m_healthy) {
        return false;
    }
    return std::none_of(m_entries.begin(), m_entries.end(),
                        [](const auto& pair) { return pair.second.currentTaskSequence != 0u; });
}

std::optional<TerrainBlasView> TerrainBlasCache::activeView(const SubChunkGpuKey& key) const {
    const auto entryIt = m_entries.find(key);
    if (entryIt == m_entries.end() || !entryIt->second.active.has_value()) {
        return std::nullopt;
    }
    const ActiveResource& active = *entryIt->second.active;
    return TerrainBlasView{active.revision,          active.worldOrigin,    active.accelerationStructure,
                           active.geometryBuffer,    active.deviceAddress,  active.opaqueVertexCount,
                           active.cutoutVertexCount, active.primitiveCount, active.geometryBytes,
                           active.blasBytes};
}

TerrainBlasStats TerrainBlasCache::stats() const {
    TerrainBlasStats result;
    result.supported = m_supported;
    result.healthy = m_healthy;
    result.buildsRecordedThisFrame = m_buildsRecordedThisFrame;
    result.compactionsRecordedThisFrame = m_compactionsRecordedThisFrame;
    result.scratchPeakBytesThisFrame = m_scratchPeakBytesThisFrame;
    for (const auto& [_, entry] : m_entries) {
        if (!entry.active.has_value()) {
            continue;
        }
        ++result.activeBlasCount;
        result.activePrimitiveCount += entry.active->primitiveCount;
        result.activeGeometryBytes += entry.active->geometryBytes;
        result.activeBlasBytes += entry.active->blasBytes;
    }
    for (const auto& [_, task] : m_tasks) {
        if (!task.current) {
            ++result.retiredTaskCount;
            continue;
        }
        switch (task.state) {
        case TaskState::Queued:
        case TaskState::BuildRecorded:
        case TaskState::BuildSubmitted: ++result.pendingBuildCount; break;
        case TaskState::ReadyToCompact:
        case TaskState::CompactRecorded:
        case TaskState::CompactSubmitted: ++result.pendingCompactionCount; break;
        }
    }
    return result;
}

bool TerrainBlasCache::pollCompletedTasks() {
    for (auto quarantineIt = m_quarantinedQueries.begin(); quarantineIt != m_quarantinedQueries.end();) {
        bool complete = false;
        if (!m_device->isSubmissionComplete(quarantineIt->completionToken, complete)) {
            setFatalError("Terrain BLAS quarantined query completion failed");
            return false;
        }
        if (!complete) {
            ++quarantineIt;
            continue;
        }
        m_freeQueryIndices.push_back(quarantineIt->queryIndex);
        quarantineIt = m_quarantinedQueries.erase(quarantineIt);
    }

    for (auto taskIt = m_tasks.begin(); taskIt != m_tasks.end();) {
        PendingTask& task = taskIt->second;
        if (task.state != TaskState::BuildSubmitted && task.state != TaskState::CompactSubmitted) {
            ++taskIt;
            continue;
        }

        bool complete = false;
        if (!m_device->isSubmissionComplete(task.submissionToken, complete)) {
            setFatalError("Terrain BLAS submission completion query failed");
            return false;
        }
        if (!complete) {
            ++taskIt;
            continue;
        }

        if (task.state == TaskState::BuildSubmitted) {
            if (!taskIsCurrent(task)) {
                releaseQueryIndex(task);
                destroyTaskResources(task);
                taskIt = m_tasks.erase(taskIt);
                continue;
            }

            uint64_t compactedSize = 0u;
            if (task.queryIndex == kInvalidQueryIndex ||
                !m_device->areQueryResultsAvailable(m_compactedSizeQueries, task.queryIndex, 1u) ||
                !m_device->getQueryResults(m_compactedSizeQueries, task.queryIndex, 1u, &compactedSize) ||
                compactedSize == 0u || compactedSize > task.buildBlasBytes) {
                setFatalError("Terrain BLAS compacted-size query result is invalid");
                return false;
            }
            task.compactedBlasBytes = compactedSize;
            task.submissionToken = {};
            releaseQueryIndex(task);
            task.state = TaskState::ReadyToCompact;
            ++taskIt;
            continue;
        }

        const auto entryIt = m_entries.find(task.schedule.key);
        if (!taskIsCurrent(task) || entryIt == m_entries.end()) {
            destroyTaskResources(task);
            taskIt = m_tasks.erase(taskIt);
            continue;
        }
        if (!promoteTask(task, entryIt->second)) {
            return false;
        }
        taskIt = m_tasks.erase(taskIt);
    }
    return true;
}

bool TerrainBlasCache::recordBuild(PendingTask& task, RhiCommandList& commandList) {
    const std::optional<uint32_t> queryIndex = acquireQueryIndex();
    if (!queryIndex.has_value()) {
        return true;
    }
    task.queryIndex = *queryIndex;

    RhiBufferDesc geometryDesc;
    geometryDesc.debugName = "Terrain.BLAS.Geometry";
    geometryDesc.size = task.geometry.byteSize();
    geometryDesc.usage = kGeometryBufferUsages;
    geometryDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    geometryDesc.initialState = RhiResourceState::TransferDst;
    geometryDesc.memoryCategory = RhiMemoryCategory::Geometry;
    task.geometryBuffer = m_device->createBuffer(geometryDesc, nullptr, 0u);
    if (!task.geometryBuffer.isValid()) {
        releaseQueryIndex(task);
        setTransientError("Terrain BLAS geometry buffer creation failed");
        return false;
    }

    std::array<RhiAccelerationStructureGeometryDesc, 2u> geometries{};
    std::array<RhiAccelerationStructureBuildRangeDesc, 2u> ranges{};
    uint32_t geometryCount = 0u;
    const auto appendGeometry = [&](const uint32_t vertexOffset, const uint32_t vertexCount,
                                    const RhiAccelerationStructureGeometryFlags flags) {
        if (vertexCount == 0u) {
            return;
        }
        RhiAccelerationStructureGeometryDesc& geometry = geometries[geometryCount];
        geometry.type = RhiAccelerationStructureGeometryType::Triangles;
        geometry.flags = flags;
        geometry.triangles.vertexBuffer = task.geometryBuffer;
        geometry.triangles.vertexOffset = static_cast<uint64_t>(vertexOffset) * sizeof(BlockVertex);
        geometry.triangles.vertexStride = sizeof(BlockVertex);
        geometry.triangles.vertexCount = vertexCount;
        geometry.triangles.vertexFormat = RhiVertexFormat::Float3;
        geometry.triangles.indexFormat = RhiAccelerationStructureIndexFormat::None;
        ranges[geometryCount].primitiveCount = vertexCount / 3u;
        ++geometryCount;
    };
    appendGeometry(0u, task.geometry.opaqueVertexCount, rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque));
    appendGeometry(task.geometry.opaqueVertexCount, task.geometry.cutoutVertexCount, 0u);

    RhiAccelerationStructureBuildInput buildInput;
    buildInput.type = RhiAccelerationStructureType::BottomLevel;
    buildInput.flags = kTerrainBuildFlags;
    buildInput.geometries = geometries.data();
    buildInput.ranges = ranges.data();
    buildInput.geometryCount = geometryCount;
    RhiAccelerationStructureBuildSizes buildSizes;
    if (!m_device->queryAccelerationStructureBuildSizes(buildInput, buildSizes) ||
        buildSizes.accelerationStructureSize == 0u || buildSizes.buildScratchSize == 0u) {
        destroyBuildAttempt(task);
        setTransientError("Terrain BLAS build-size query failed");
        return false;
    }
    task.buildBlasBytes = buildSizes.accelerationStructureSize;
    task.buildScratchBytes = buildSizes.buildScratchSize;

    RhiBufferDesc storageDesc;
    storageDesc.debugName = "Terrain.BLAS.Build.Storage";
    storageDesc.size = buildSizes.accelerationStructureSize;
    storageDesc.usage = kAccelerationStructureStorageUsages;
    storageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    storageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
    storageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    task.buildStorageBuffer = m_device->createBuffer(storageDesc, nullptr, 0u);

    RhiBufferDesc scratchDesc;
    scratchDesc.debugName = "Terrain.BLAS.Build.Scratch";
    scratchDesc.size = buildSizes.buildScratchSize;
    scratchDesc.usage = kScratchBufferUsages;
    scratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    scratchDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
    scratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    task.scratchBuffer = m_device->createBuffer(scratchDesc, nullptr, 0u);
    if (task.buildStorageBuffer.isValid()) {
        task.buildAccelerationStructure =
            m_device->createAccelerationStructure({"Terrain.BLAS.Build", RhiAccelerationStructureType::BottomLevel,
                                                   task.buildStorageBuffer, 0u, buildSizes.accelerationStructureSize});
    }
    if (!task.buildStorageBuffer.isValid() || !task.scratchBuffer.isValid() ||
        !task.buildAccelerationStructure.isValid()) {
        destroyBuildAttempt(task);
        setTransientError("Terrain BLAS build resource creation failed");
        return false;
    }

    task.state = TaskState::BuildRecorded;
    m_recordedBuilds.push_back(task.schedule.requestSequence);
    ++m_buildsRecordedThisFrame;
    m_scratchBytesRecordedThisFrame += task.buildScratchBytes;
    m_scratchPeakBytesThisFrame = std::max(m_scratchPeakBytesThisFrame, m_scratchBytesRecordedThisFrame);

    commandList.updateBuffer(task.geometryBuffer, 0u, task.geometry.vertices.data(), task.geometry.byteSize());
    commandList.bufferBarrier(
        {task.geometryBuffer, RhiResourceState::TransferDst, RhiResourceState::AccelerationStructureBuildInput});
    commandList.resetQueryPool(m_compactedSizeQueries, task.queryIndex, 1u);
    const RhiAccelerationStructureBuildDesc build{
        buildInput, RhiAccelerationStructureBuildMode::Build, {}, task.buildAccelerationStructure, task.scratchBuffer,
        0u};
    if (!commandList.buildAccelerationStructures(&build, 1u) ||
        !commandList.accelerationStructureBarrier({task.buildAccelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead}) ||
        !commandList.writeAccelerationStructureProperties(
            {&task.buildAccelerationStructure, 1u, m_compactedSizeQueries, task.queryIndex})) {
        setTransientError("Terrain BLAS build command recording failed");
        return false;
    }
    return true;
}

bool TerrainBlasCache::recordCompaction(PendingTask& task, RhiCommandList& commandList) {
    RhiBufferDesc storageDesc;
    storageDesc.debugName = "Terrain.BLAS.Compact.Storage";
    storageDesc.size = task.compactedBlasBytes;
    storageDesc.usage = kAccelerationStructureStorageUsages;
    storageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    storageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
    storageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    task.compactStorageBuffer = m_device->createBuffer(storageDesc, nullptr, 0u);
    if (task.compactStorageBuffer.isValid()) {
        task.compactAccelerationStructure =
            m_device->createAccelerationStructure({"Terrain.BLAS.Compact", RhiAccelerationStructureType::BottomLevel,
                                                   task.compactStorageBuffer, 0u, task.compactedBlasBytes});
    }
    if (!task.compactStorageBuffer.isValid() || !task.compactAccelerationStructure.isValid()) {
        destroyCompactAttempt(task);
        setTransientError("Terrain BLAS compact resource creation failed");
        return false;
    }

    task.state = TaskState::CompactRecorded;
    m_recordedCompactions.push_back(task.schedule.requestSequence);
    ++m_compactionsRecordedThisFrame;
    if (!commandList.copyAccelerationStructure({task.buildAccelerationStructure, task.compactAccelerationStructure,
                                                RhiAccelerationStructureCopyMode::Compact}) ||
        !commandList.accelerationStructureBarrier({task.compactAccelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead})) {
        setTransientError("Terrain BLAS compact command recording failed");
        return false;
    }
    return true;
}

bool TerrainBlasCache::taskIsCurrent(const PendingTask& task) const {
    const auto entryIt = m_entries.find(task.schedule.key);
    return task.current && entryIt != m_entries.end() &&
           entryIt->second.currentTaskSequence == task.schedule.requestSequence &&
           entryIt->second.latestRevision == task.revision;
}

void TerrainBlasCache::retireCurrentTask(Entry& entry) {
    if (entry.currentTaskSequence == 0u) {
        return;
    }
    const auto taskIt = m_tasks.find(entry.currentTaskSequence);
    entry.currentTaskSequence = 0u;
    if (taskIt == m_tasks.end()) {
        return;
    }
    PendingTask& task = taskIt->second;
    task.current = false;
    if (task.state == TaskState::Queued || task.state == TaskState::ReadyToCompact) {
        releaseQueryIndex(task);
        destroyTaskResources(task);
        m_tasks.erase(taskIt);
    }
}

bool TerrainBlasCache::promoteTask(PendingTask& task, Entry& entry) {
    const uint64_t deviceAddress = m_device->accelerationStructureDeviceAddress(task.compactAccelerationStructure);
    if (deviceAddress == 0u) {
        setFatalError("Terrain BLAS compacted device address is invalid");
        return false;
    }

    ActiveResource active;
    active.revision = task.revision;
    active.worldOrigin = task.worldOrigin;
    active.accelerationStructure = task.compactAccelerationStructure;
    active.storageBuffer = task.compactStorageBuffer;
    active.geometryBuffer = task.geometryBuffer;
    active.deviceAddress = deviceAddress;
    active.opaqueVertexCount = task.geometry.opaqueVertexCount;
    active.cutoutVertexCount = task.geometry.cutoutVertexCount;
    active.primitiveCount = task.geometry.primitiveCount();
    active.geometryBytes = static_cast<uint64_t>(task.geometry.vertexCount()) * sizeof(BlockVertex);
    active.blasBytes = task.compactedBlasBytes;
    task.compactAccelerationStructure = {};
    task.compactStorageBuffer = {};
    task.geometryBuffer = {};

    if (entry.active.has_value()) {
        destroyActiveResource(*entry.active);
    }
    entry.active = std::move(active);
    entry.currentTaskSequence = 0u;

    if (task.buildAccelerationStructure.isValid()) {
        m_device->destroyAccelerationStructure(task.buildAccelerationStructure);
        task.buildAccelerationStructure = {};
    }
    if (task.buildStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.buildStorageBuffer);
        task.buildStorageBuffer = {};
    }
    return true;
}

void TerrainBlasCache::destroyActiveResource(ActiveResource& resource) {
    if (resource.accelerationStructure.isValid()) {
        m_device->destroyAccelerationStructure(resource.accelerationStructure);
        resource.accelerationStructure = {};
    }
    if (resource.storageBuffer.isValid()) {
        m_device->destroyBuffer(resource.storageBuffer);
        resource.storageBuffer = {};
    }
    if (resource.geometryBuffer.isValid()) {
        m_device->destroyBuffer(resource.geometryBuffer);
        resource.geometryBuffer = {};
    }
}

void TerrainBlasCache::destroyTaskResources(PendingTask& task) {
    destroyCompactAttempt(task);
    if (task.buildAccelerationStructure.isValid()) {
        m_device->destroyAccelerationStructure(task.buildAccelerationStructure);
        task.buildAccelerationStructure = {};
    }
    if (task.scratchBuffer.isValid()) {
        m_device->destroyBuffer(task.scratchBuffer);
        task.scratchBuffer = {};
    }
    if (task.buildStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.buildStorageBuffer);
        task.buildStorageBuffer = {};
    }
    if (task.geometryBuffer.isValid()) {
        m_device->destroyBuffer(task.geometryBuffer);
        task.geometryBuffer = {};
    }
}

void TerrainBlasCache::destroyBuildAttempt(PendingTask& task) {
    releaseQueryIndex(task);
    if (task.buildAccelerationStructure.isValid()) {
        m_device->destroyAccelerationStructure(task.buildAccelerationStructure);
        task.buildAccelerationStructure = {};
    }
    if (task.scratchBuffer.isValid()) {
        m_device->destroyBuffer(task.scratchBuffer);
        task.scratchBuffer = {};
    }
    if (task.buildStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.buildStorageBuffer);
        task.buildStorageBuffer = {};
    }
    if (task.geometryBuffer.isValid()) {
        m_device->destroyBuffer(task.geometryBuffer);
        task.geometryBuffer = {};
    }
    task.buildBlasBytes = 0u;
    task.buildScratchBytes = 0u;
    task.compactedBlasBytes = 0u;
    task.submissionToken = {};
}

void TerrainBlasCache::destroyCompactAttempt(PendingTask& task) {
    if (task.compactAccelerationStructure.isValid()) {
        m_device->destroyAccelerationStructure(task.compactAccelerationStructure);
        task.compactAccelerationStructure = {};
    }
    if (task.compactStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.compactStorageBuffer);
        task.compactStorageBuffer = {};
    }
}

void TerrainBlasCache::releaseQueryIndex(PendingTask& task) {
    if (task.queryIndex == kInvalidQueryIndex) {
        return;
    }
    m_freeQueryIndices.push_back(task.queryIndex);
    task.queryIndex = kInvalidQueryIndex;
}

void TerrainBlasCache::quarantineQueryIndex(PendingTask& task, const RhiSubmissionToken completionToken) {
    if (task.queryIndex == kInvalidQueryIndex) {
        return;
    }
    m_quarantinedQueries.push_back({task.queryIndex, completionToken});
    task.queryIndex = kInvalidQueryIndex;
}

std::optional<uint32_t> TerrainBlasCache::acquireQueryIndex() {
    if (m_freeQueryIndices.empty()) {
        return std::nullopt;
    }
    const auto queryIt = std::min_element(m_freeQueryIndices.begin(), m_freeQueryIndices.end());
    const uint32_t queryIndex = *queryIt;
    m_freeQueryIndices.erase(queryIt);
    return queryIndex;
}

void TerrainBlasCache::setTransientError(const char* message) {
    m_lastError = message != nullptr ? message : "Terrain BLAS operation failed";
}

void TerrainBlasCache::setFatalError(const char* message) {
    setTransientError(message);
    m_healthy = false;
}
