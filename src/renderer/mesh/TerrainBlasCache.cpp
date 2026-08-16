#include "TerrainBlasCache.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/debug/RenderDebugService.h"
#include "../../world/chunk/Chunk.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {

constexpr RhiBufferUsageFlags kGeometryBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst) |
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
constexpr RhiBufferUsageFlags kPrimitiveMetadataBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst) |
    rhiFlag(RhiBufferUsage::DeviceAddress);
constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
constexpr RhiBufferUsageFlags kScratchBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
constexpr RhiBufferUsageFlags kMicromapBuildInputUsages = rhiFlag(RhiBufferUsage::TransferDst) |
                                                          rhiFlag(RhiBufferUsage::DeviceAddress) |
                                                          rhiFlag(RhiBufferUsage::MicromapBuildInput);
constexpr RhiBufferUsageFlags kMicromapStorageUsages =
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::MicromapStorage);
constexpr RhiAccelerationStructureBuildFlags kTerrainBuildFlags =
    rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);

static_assert(offsetof(BlockVertex, x) == renderer::contracts::kTerrainRayTracingVertexPositionOffset &&
                  offsetof(BlockVertex, y) == sizeof(float) && offsetof(BlockVertex, z) == sizeof(float) * 2u,
              "Terrain BLAS positions must occupy the first three floats of BlockVertex");
static_assert(offsetof(BlockVertex, u) == renderer::contracts::kTerrainRayTracingVertexUvOffset &&
                  offsetof(BlockVertex, v) == renderer::contracts::kTerrainRayTracingVertexUvOffset + sizeof(float),
              "Terrain ray-query UVs must match the fixed BlockVertex byte offsets");
static_assert(sizeof(BlockVertex) == renderer::contracts::kTerrainRayTracingVertexStride,
              "Terrain ray-query vertex stride must match BlockVertex");

[[nodiscard]] bool finitePositionAndUv(const BlockVertex& vertex) {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z) && std::isfinite(vertex.u) &&
           std::isfinite(vertex.v);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool samePrimitiveMaterial(const BlockVertex& left, const BlockVertex& right) {
    return left.normal == right.normal && left.layer == right.layer &&
           left.animationFrameCount == right.animationFrameCount && left.animationFps == right.animationFps &&
           left.animationAndFlags == right.animationAndFlags && left.tintPacked == right.tintPacked;
}

[[nodiscard]] std::optional<renderer::contracts::TerrainPrimitiveMetadata>
primitiveMetadataForTriangle(const std::vector<BlockVertex>& vertices, const size_t firstVertex) {
    if (firstVertex > vertices.size() || vertices.size() - firstVertex < 3u) {
        return std::nullopt;
    }
    const BlockVertex& first = vertices[firstVertex];
    const BlockVertex& second = vertices[firstVertex + 1u];
    const BlockVertex& third = vertices[firstVertex + 2u];
    if (!validBlockVertexFlags(first) || !finitePositionAndUv(first) || !finitePositionAndUv(second) ||
        !finitePositionAndUv(third) || !samePrimitiveMaterial(first, second) || !samePrimitiveMaterial(first, third)) {
        return std::nullopt;
    }
    return renderer::contracts::encodeTerrainPrimitiveMetadata({first.layer, first.animationFrameCount,
                                                                first.animationFps, blockVertexAnimated(first),
                                                                first.tintPacked, first.normal,
                                                                blockVertexAnalyticLightOwnsEmission(first)});
}

[[nodiscard]] bool buildPrimitiveMetadata(const std::vector<BlockVertex>& vertices,
                                          std::vector<renderer::contracts::TerrainPrimitiveMetadata>& metadata) {
    metadata.clear();
    if (vertices.size() % 3u != 0u) {
        return false;
    }
    metadata.reserve(vertices.size() / 3u);
    for (size_t firstVertex = 0u; firstVertex < vertices.size(); firstVertex += 3u) {
        const std::optional<renderer::contracts::TerrainPrimitiveMetadata> primitive =
            primitiveMetadataForTriangle(vertices, firstVertex);
        if (!primitive.has_value()) {
            metadata.clear();
            return false;
        }
        metadata.push_back(*primitive);
    }
    return true;
}

/// Expands the opaque prefix along its encoded face normals without changing cutout vertices.
/// @param vertices Combined opaque and cutout BLAS vertex payload.
/// @param opaqueVertexCount Number of vertices in the opaque prefix.
/// @return False when the prefix is invalid or contains a non-axis-aligned face marker.
[[nodiscard]] bool sealOpaqueGeometry(std::vector<BlockVertex>& vertices, const uint32_t opaqueVertexCount) {
    constexpr std::array<std::array<float, 3u>, 6u> kFaceNormals{{
        {{0.0f, 1.0f, 0.0f}},
        {{0.0f, -1.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, -1.0f}},
        {{-1.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
    }};
    if (opaqueVertexCount > vertices.size()) {
        return false;
    }
    for (uint32_t vertexIndex = 0u; vertexIndex < opaqueVertexCount; ++vertexIndex) {
        BlockVertex& vertex = vertices[vertexIndex];
        if (vertex.normal < 0 || vertex.normal >= static_cast<int8_t>(kFaceNormals.size())) {
            return false;
        }
        const auto& normal = kFaceNormals[static_cast<size_t>(vertex.normal)];
        vertex.x += normal[0] * kTerrainBlasOpaqueSurfaceExpansion;
        vertex.y += normal[1] * kTerrainBlasOpaqueSurfaceExpansion;
        vertex.z += normal[2] * kTerrainBlasOpaqueSurfaceExpansion;
    }
    return true;
}

[[nodiscard]] bool validPreparedGeometry(const TerrainBlasGeometry& geometry) {
    const uint64_t totalVertexCount = static_cast<uint64_t>(geometry.opaqueVertexCount) + geometry.cutoutVertexCount;
    if (geometry.opaqueVertexCount % 3u != 0u || geometry.cutoutVertexCount % 3u != 0u ||
        totalVertexCount > std::numeric_limits<uint32_t>::max() || totalVertexCount != geometry.vertices.size() ||
        geometry.primitiveMetadata.size() != totalVertexCount / 3u) {
        return false;
    }
    for (size_t primitiveIndex = 0u; primitiveIndex < geometry.primitiveMetadata.size(); ++primitiveIndex) {
        const std::optional<renderer::contracts::TerrainPrimitiveMetadata> expected =
            primitiveMetadataForTriangle(geometry.vertices, primitiveIndex * 3u);
        if (!expected.has_value() || !renderer::contracts::validTerrainPrimitiveMetadata(*expected) ||
            !(*expected == geometry.primitiveMetadata[primitiveIndex])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<renderer::contracts::TerrainOpacityMicromapTriangleInput>
makeTerrainOpacityMicromapInputs(const TerrainBlasGeometry& geometry) {
    std::vector<renderer::contracts::TerrainOpacityMicromapTriangleInput> inputs;
    inputs.reserve(geometry.cutoutVertexCount / 3u);
    for (uint32_t firstVertex = geometry.opaqueVertexCount; firstVertex < geometry.vertexCount(); firstVertex += 3u) {
        const BlockVertex& first = geometry.vertices[firstVertex];
        const BlockVertex& second = geometry.vertices[firstVertex + 1u];
        const BlockVertex& third = geometry.vertices[firstVertex + 2u];
        renderer::contracts::TerrainOpacityMicromapTriangleInput input;
        input.uv = {first.u, first.v, second.u, second.v, third.u, third.v};
        input.firstTextureLayer = first.layer;
        input.animationFrameCount = first.animationFrameCount;
        input.animated = blockVertexAnimated(first);
        inputs.push_back(input);
    }
    return inputs;
}

[[nodiscard]] std::optional<renderer::contracts::TerrainRayTracingHitData>
makeTerrainHitData(const uint64_t revision, const uint64_t vertexAddress, const uint64_t primitiveMetadataAddress,
                   const uint32_t opaqueVertexCount, const uint32_t cutoutVertexCount) {
    renderer::contracts::TerrainRayTracingHitData hitData;
    hitData.revision = revision;
    hitData.vertexAddress = vertexAddress;
    hitData.primitiveMetadataAddress = primitiveMetadataAddress;
    uint32_t geometryIndex = 0u;
    uint32_t vertexBase = 0u;
    uint32_t primitiveBase = 0u;
    const auto appendGeometry = [&](const renderer::contracts::TerrainRayTracingGeometryClass geometryClass,
                                    const uint32_t vertexCount) {
        if (vertexCount == 0u) {
            return;
        }
        hitData.geometries[geometryIndex] = {geometryIndex, geometryClass, vertexBase,
                                             vertexCount,   primitiveBase, vertexCount / 3u};
        ++geometryIndex;
        vertexBase += vertexCount;
        primitiveBase += vertexCount / 3u;
    };
    appendGeometry(renderer::contracts::TerrainRayTracingGeometryClass::Opaque, opaqueVertexCount);
    appendGeometry(renderer::contracts::TerrainRayTracingGeometryClass::Cutout, cutoutVertexCount);
    hitData.geometryCount = geometryIndex;
    if (!renderer::contracts::validTerrainRayTracingHitData(hitData)) {
        return std::nullopt;
    }
    return hitData;
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

bool TerrainBlasCache::setOpacityMicromapSource(const renderer::contracts::TerrainOpacityMicromapSource source) {
    if (!m_entries.empty() || !m_tasks.empty()) {
        setFatalError("Terrain opacity micromap source cannot change while BLAS generations exist");
        return false;
    }
    if (!renderer::contracts::validTerrainOpacityMicromapSource(source)) {
        setFatalError("Terrain opacity micromap source is invalid");
        return false;
    }
    m_opacityMicromapSource = source;
    return true;
}

bool TerrainBlasCache::setOpacityMicromapEnabled(const bool enabled) {
    if (enabled == m_opacityMicromapEnabled) {
        return true;
    }
    if (!m_entries.empty() || !m_tasks.empty()) {
        setTransientError("Terrain opacity micromap mode cannot change while BLAS generations exist");
        return false;
    }
    if (enabled && (!m_initialized || m_device == nullptr || !m_device->capabilities().opacityMicromap ||
                    m_device->capabilities().maxOpacityMicromapFourStateSubdivisionLevel <
                        m_opacityMicromapProfile.subdivisionLevel ||
                    !renderer::contracts::validTerrainOpacityMicromapSource(m_opacityMicromapSource))) {
        setFatalError("Terrain opacity micromap mode is unsupported by the active device or texture source");
        return false;
    }
    m_opacityMicromapEnabled = enabled;
    return true;
}

void TerrainBlasCache::shutdown() {
    if (m_device != nullptr) {
        for (auto& [_, entry] : m_entries) {
            if (entry.active.has_value()) {
                entry.active.reset();
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
    m_buildPrimitiveCountThisFrame = 0u;
    m_buildOpaquePrimitiveCountThisFrame = 0u;
    m_buildCutoutPrimitiveCountThisFrame = 0u;
    m_compactionPrimitiveCountThisFrame = 0u;
    m_buildBlasBytesThisFrame = 0u;
    m_compactedBlasBytesThisFrame = 0u;
    m_dynamicResourceBytesThisFrame = 0u;
    m_scratchBytesRecordedThisFrame = 0u;
    m_scratchPeakBytesThisFrame = 0u;
    m_opacityMicromapsBuiltThisFrame = 0u;
    m_opacityMicromapPrimitivesBuiltThisFrame = 0u;
    m_opacityMicromapInputBytesThisFrame = 0u;
    m_opacityMicromapStorageBytesThisFrame = 0u;
    m_opacityMicromapScratchBytesThisFrame = 0u;
    m_opacityMicromapCountersBuiltThisFrame = {};
    m_buildCpuMsThisFrame = 0.0;
    m_compactionCpuMsThisFrame = 0.0;
    m_dynamicResourceCpuMsThisFrame = 0.0;
    m_lastError.clear();
    m_opacityMicromapSource = {};
    m_opacityMicromapProfile = {};
    m_opacityMicromapEnabled = false;
}

void TerrainBlasCache::beginFrame() {
    m_buildsRecordedThisFrame = 0u;
    m_compactionsRecordedThisFrame = 0u;
    m_buildPrimitiveCountThisFrame = 0u;
    m_buildOpaquePrimitiveCountThisFrame = 0u;
    m_buildCutoutPrimitiveCountThisFrame = 0u;
    m_compactionPrimitiveCountThisFrame = 0u;
    m_buildBlasBytesThisFrame = 0u;
    m_compactedBlasBytesThisFrame = 0u;
    m_dynamicResourceBytesThisFrame = 0u;
    m_scratchBytesRecordedThisFrame = 0u;
    m_scratchPeakBytesThisFrame = 0u;
    m_opacityMicromapsBuiltThisFrame = 0u;
    m_opacityMicromapPrimitivesBuiltThisFrame = 0u;
    m_opacityMicromapInputBytesThisFrame = 0u;
    m_opacityMicromapStorageBytesThisFrame = 0u;
    m_opacityMicromapScratchBytesThisFrame = 0u;
    m_opacityMicromapCountersBuiltThisFrame = {};
    m_buildCpuMsThisFrame = 0.0;
    m_compactionCpuMsThisFrame = 0.0;
    m_dynamicResourceCpuMsThisFrame = 0.0;
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
        totalCount > std::numeric_limits<uint32_t>::max()) {
        return TerrainBlasRequestResult::InvalidGeometry;
    }

    geometry.opaqueVertexCount = static_cast<uint32_t>(opaqueCount);
    geometry.cutoutVertexCount = static_cast<uint32_t>(cutoutCount);
    geometry.vertices.reserve(static_cast<size_t>(totalCount));
    geometry.vertices.insert(geometry.vertices.end(), opaque.begin(), opaque.end());
    geometry.vertices.insert(geometry.vertices.end(), cutout.begin(), cutout.end());
    geometry.vertices.insert(geometry.vertices.end(), cutoutDistance.begin(), cutoutDistance.end());
    if (!sealOpaqueGeometry(geometry.vertices, geometry.opaqueVertexCount) ||
        !buildPrimitiveMetadata(geometry.vertices, geometry.primitiveMetadata)) {
        geometry = {};
        return TerrainBlasRequestResult::InvalidGeometry;
    }
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

    std::optional<renderer::contracts::TerrainOpacityMicromapCpuData> opacityMicromap;
    if (m_opacityMicromapEnabled && geometry.cutoutVertexCount != 0u) {
        opacityMicromap = renderer::contracts::buildTerrainOpacityMicromapCpuData(
            m_opacityMicromapSource, m_opacityMicromapProfile, makeTerrainOpacityMicromapInputs(geometry));
        if (!opacityMicromap.has_value()) {
            setFatalError("Terrain opacity micromap CPU preparation failed");
            return TerrainBlasRequestResult::OpacityMicromapPreparationFailed;
        }
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
    task.opacityMicromap = std::move(opacityMicromap);
    if (task.opacityMicromap.has_value()) {
        const renderer::contracts::TerrainOpacityMicromapCpuData& cpuData = *task.opacityMicromap;
        task.micromapInputBytes =
            cpuData.opacityData.size() + cpuData.triangleRecords.size() * sizeof(cpuData.triangleRecords.front());
        task.micromapPrimitiveCount = task.geometry.cutoutVertexCount / 3u;
        task.micromapCounters = cpuData.counters;
        task.micromapAlphaTextureHash = cpuData.alphaTextureHash;
        task.micromapProfileHash = cpuData.profileHash;
        task.micromapSubdivisionLevel = cpuData.subdivisionLevel;
    }
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
        entry.active.reset();
    }
    m_entries.erase(entryIt);
}

bool TerrainBlasCache::recordFrame(RhiCommandList& commandList, RenderDebugService* const debugService) {
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
        if (!recordCompaction(task, commandList, debugService)) {
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

        const uint64_t taskBytes = task.geometry.uploadByteSize();
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
        if (!recordBuild(task, commandList, debugService)) {
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
            if (task.micromapScratchBuffer.isValid()) {
                m_device->destroyBuffer(task.micromapScratchBuffer);
                task.micromapScratchBuffer = {};
            }
            if (task.micromapOpacityBuffer.isValid()) {
                m_device->destroyBuffer(task.micromapOpacityBuffer);
                task.micromapOpacityBuffer = {};
            }
            if (task.micromapTriangleBuffer.isValid()) {
                m_device->destroyBuffer(task.micromapTriangleBuffer);
                task.micromapTriangleBuffer = {};
            }
            std::vector<BlockVertex>().swap(task.geometry.vertices);
            std::vector<renderer::contracts::TerrainPrimitiveMetadata>().swap(task.geometry.primitiveMetadata);
            task.opacityMicromap.reset();
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
    TerrainBlasView view;
    view.key = key;
    view.revision = active.revision;
    view.worldOrigin = active.worldOrigin;
    view.resource = active.resource;
    view.accelerationStructure = active.resource->accelerationStructure();
    view.geometryBuffer = active.geometryBuffer;
    view.primitiveMetadataBuffer = active.primitiveMetadataBuffer;
    view.deviceAddress = active.resource->deviceAddress();
    view.vertexAddress = active.hitData.vertexAddress;
    view.primitiveMetadataAddress = active.hitData.primitiveMetadataAddress;
    view.opaqueVertexCount = active.opaqueVertexCount;
    view.cutoutVertexCount = active.cutoutVertexCount;
    view.primitiveCount = active.primitiveCount;
    view.geometryBytes = active.geometryBytes;
    view.primitiveMetadataBytes = active.primitiveMetadataBytes;
    view.blasBytes = active.resource->blasBytes();
    view.hitData = active.hitData;
    return view;
}

std::vector<TerrainBlasView> TerrainBlasCache::activeViews() const {
    std::vector<TerrainBlasView> views;
    views.reserve(m_entries.size());
    for (const auto& [key, entry] : m_entries) {
        if (!entry.active.has_value()) {
            continue;
        }
        const ActiveResource& active = *entry.active;
        TerrainBlasView view;
        view.key = key;
        view.revision = active.revision;
        view.worldOrigin = active.worldOrigin;
        view.resource = active.resource;
        view.accelerationStructure = active.resource->accelerationStructure();
        view.geometryBuffer = active.geometryBuffer;
        view.primitiveMetadataBuffer = active.primitiveMetadataBuffer;
        view.deviceAddress = active.resource->deviceAddress();
        view.vertexAddress = active.hitData.vertexAddress;
        view.primitiveMetadataAddress = active.hitData.primitiveMetadataAddress;
        view.opaqueVertexCount = active.opaqueVertexCount;
        view.cutoutVertexCount = active.cutoutVertexCount;
        view.primitiveCount = active.primitiveCount;
        view.geometryBytes = active.geometryBytes;
        view.primitiveMetadataBytes = active.primitiveMetadataBytes;
        view.blasBytes = active.resource->blasBytes();
        view.hitData = active.hitData;
        views.push_back(std::move(view));
    }
    std::sort(views.begin(), views.end(), [](const TerrainBlasView& left, const TerrainBlasView& right) {
        if (left.key.chunkKey != right.key.chunkKey) {
            return left.key.chunkKey < right.key.chunkKey;
        }
        return left.key.scy < right.key.scy;
    });
    return views;
}

TerrainBlasStats TerrainBlasCache::stats() const {
    TerrainBlasStats result;
    result.supported = m_supported;
    result.healthy = m_healthy;
    result.buildsRecordedThisFrame = m_buildsRecordedThisFrame;
    result.compactionsRecordedThisFrame = m_compactionsRecordedThisFrame;
    result.buildPrimitiveCountThisFrame = m_buildPrimitiveCountThisFrame;
    result.buildOpaquePrimitiveCountThisFrame = m_buildOpaquePrimitiveCountThisFrame;
    result.buildCutoutPrimitiveCountThisFrame = m_buildCutoutPrimitiveCountThisFrame;
    result.compactionPrimitiveCountThisFrame = m_compactionPrimitiveCountThisFrame;
    result.buildBlasBytesThisFrame = m_buildBlasBytesThisFrame;
    result.compactedBlasBytesThisFrame = m_compactedBlasBytesThisFrame;
    result.dynamicResourceBytesThisFrame = m_dynamicResourceBytesThisFrame;
    result.scratchPeakBytesThisFrame = m_scratchPeakBytesThisFrame;
    result.opacityMicromapEnabled = m_opacityMicromapEnabled;
    result.opacityMicromapSubdivisionLevel = m_opacityMicromapProfile.subdivisionLevel;
    result.opacityMicromapAlphaTextureHash = m_opacityMicromapSource.alphaTextureHash;
    result.opacityMicromapProfileHash =
        renderer::contracts::terrainOpacityMicromapProfileHash(m_opacityMicromapProfile);
    result.opacityMicromapsBuiltThisFrame = m_opacityMicromapsBuiltThisFrame;
    result.opacityMicromapPrimitivesBuiltThisFrame = m_opacityMicromapPrimitivesBuiltThisFrame;
    result.opacityMicromapInputBytesThisFrame = m_opacityMicromapInputBytesThisFrame;
    result.opacityMicromapStorageBytesThisFrame = m_opacityMicromapStorageBytesThisFrame;
    result.opacityMicromapScratchBytesThisFrame = m_opacityMicromapScratchBytesThisFrame;
    result.opacityMicromapOpaqueMicroTrianglesBuiltThisFrame = m_opacityMicromapCountersBuiltThisFrame.opaque;
    result.opacityMicromapTransparentMicroTrianglesBuiltThisFrame = m_opacityMicromapCountersBuiltThisFrame.transparent;
    result.opacityMicromapUnknownMicroTrianglesBuiltThisFrame = m_opacityMicromapCountersBuiltThisFrame.unknown;
    result.buildCpuMsThisFrame = m_buildCpuMsThisFrame;
    result.compactionCpuMsThisFrame = m_compactionCpuMsThisFrame;
    result.dynamicResourceCpuMsThisFrame = m_dynamicResourceCpuMsThisFrame;
    for (const auto& [_, entry] : m_entries) {
        if (!entry.active.has_value()) {
            continue;
        }
        ++result.activeBlasCount;
        result.activePrimitiveCount += entry.active->primitiveCount;
        result.activeOpaquePrimitiveCount += entry.active->opaqueVertexCount / 3u;
        result.activeCutoutPrimitiveCount += entry.active->cutoutVertexCount / 3u;
        result.activeGeometryBytes += entry.active->geometryBytes;
        result.activePrimitiveMetadataBytes += entry.active->primitiveMetadataBytes;
        result.activeBlasBytes += entry.active->resource->blasBytes();
        if (entry.active->opacityMicromapBytes != 0u) {
            if (result.activeOpacityMicromapCount == 0u) {
                result.opacityMicromapAlphaTextureHash = entry.active->opacityMicromapAlphaTextureHash;
                result.opacityMicromapProfileHash = entry.active->opacityMicromapProfileHash;
                result.opacityMicromapSubdivisionLevel = entry.active->opacityMicromapSubdivisionLevel;
            } else if (result.opacityMicromapAlphaTextureHash != entry.active->opacityMicromapAlphaTextureHash ||
                       result.opacityMicromapProfileHash != entry.active->opacityMicromapProfileHash ||
                       result.opacityMicromapSubdivisionLevel != entry.active->opacityMicromapSubdivisionLevel) {
                result.healthy = false;
            }
            ++result.activeOpacityMicromapCount;
            result.activeOpacityMicromapBytes += entry.active->opacityMicromapBytes;
            result.activeOpacityMicromapOpaqueMicroTriangles += entry.active->opacityMicromapCounters.opaque;
            result.activeOpacityMicromapTransparentMicroTriangles += entry.active->opacityMicromapCounters.transparent;
            result.activeOpacityMicromapUnknownMicroTriangles += entry.active->opacityMicromapCounters.unknown;
        }
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

bool TerrainBlasCache::recordBuild(PendingTask& task, RhiCommandList& commandList,
                                   RenderDebugService* const debugService) {
    const auto dynamicResourceCpuStart = std::chrono::steady_clock::now();
    const std::optional<uint32_t> queryIndex = acquireQueryIndex();
    if (!queryIndex.has_value()) {
        return true;
    }
    task.queryIndex = *queryIndex;

    RhiBufferDesc geometryDesc;
    geometryDesc.debugName = "Terrain.BLAS.Geometry";
    geometryDesc.size = task.geometry.vertexByteSize();
    geometryDesc.usage = kGeometryBufferUsages;
    geometryDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    geometryDesc.initialState = RhiResourceState::TransferDst;
    geometryDesc.memoryCategory = RhiMemoryCategory::Geometry;
    task.geometryBuffer = m_device->createBuffer(geometryDesc, nullptr, 0u);
    RhiBufferDesc primitiveMetadataDesc;
    primitiveMetadataDesc.debugName = "Terrain.BLAS.PrimitiveMetadata";
    primitiveMetadataDesc.size = task.geometry.primitiveMetadataByteSize();
    primitiveMetadataDesc.usage = kPrimitiveMetadataBufferUsages;
    primitiveMetadataDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    primitiveMetadataDesc.initialState = RhiResourceState::TransferDst;
    primitiveMetadataDesc.memoryCategory = RhiMemoryCategory::Geometry;
    task.primitiveMetadataBuffer = m_device->createBuffer(primitiveMetadataDesc, nullptr, 0u);
    if (!task.geometryBuffer.isValid() || !task.primitiveMetadataBuffer.isValid()) {
        destroyBuildAttempt(task);
        setTransientError("Terrain BLAS geometry or primitive-metadata buffer creation failed");
        return false;
    }

    RhiOpacityMicromapUsageDesc micromapUsage;
    RhiMicromapBuildInput micromapBuildInput;
    if (task.opacityMicromap.has_value()) {
        const renderer::contracts::TerrainOpacityMicromapCpuData& cpuData = *task.opacityMicromap;
        RhiBufferDesc opacityDesc;
        opacityDesc.debugName = "Terrain.OMM.OpacityInput";
        opacityDesc.size = cpuData.opacityData.size();
        opacityDesc.usage = kMicromapBuildInputUsages;
        opacityDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        opacityDesc.initialState = RhiResourceState::TransferDst;
        opacityDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        task.micromapOpacityBuffer = m_device->createBuffer(opacityDesc, nullptr, 0u);

        RhiBufferDesc triangleDesc;
        triangleDesc.debugName = "Terrain.OMM.TriangleInput";
        triangleDesc.size = cpuData.triangleRecords.size() * sizeof(cpuData.triangleRecords.front());
        triangleDesc.usage = kMicromapBuildInputUsages;
        triangleDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        triangleDesc.initialState = RhiResourceState::TransferDst;
        triangleDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        task.micromapTriangleBuffer = m_device->createBuffer(triangleDesc, nullptr, 0u);

        micromapUsage.count = task.geometry.cutoutVertexCount / 3u;
        micromapUsage.subdivisionLevel = cpuData.subdivisionLevel;
        micromapUsage.format = RhiOpacityMicromapFormat::FourState;
        micromapBuildInput.flags = rhiFlag(RhiMicromapBuildFlag::PreferFastTrace);
        micromapBuildInput.usages = &micromapUsage;
        micromapBuildInput.usageCount = 1u;
        micromapBuildInput.opacityDataBuffer = task.micromapOpacityBuffer;
        micromapBuildInput.triangleBuffer = task.micromapTriangleBuffer;
        micromapBuildInput.triangleStride = sizeof(renderer::contracts::TerrainOpacityMicromapTriangleRecord);

        RhiMicromapBuildSizes micromapSizes;
        if (!task.micromapOpacityBuffer.isValid() || !task.micromapTriangleBuffer.isValid() ||
            !m_device->queryMicromapBuildSizes(micromapBuildInput, micromapSizes)) {
            destroyBuildAttempt(task);
            setTransientError("Terrain opacity micromap build-size query failed");
            return false;
        }
        task.micromapStorageBytes = micromapSizes.micromapSize;
        task.micromapScratchBytes = micromapSizes.buildScratchSize;

        RhiBufferDesc micromapStorageDesc;
        micromapStorageDesc.debugName = "Terrain.OMM.Storage";
        micromapStorageDesc.size = micromapSizes.micromapSize;
        micromapStorageDesc.usage = kMicromapStorageUsages;
        micromapStorageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        micromapStorageDesc.initialState = RhiResourceState::MicromapBuildWrite;
        micromapStorageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        task.micromapStorageBuffer = m_device->createBuffer(micromapStorageDesc, nullptr, 0u);

        RhiBufferDesc micromapScratchDesc;
        micromapScratchDesc.debugName = "Terrain.OMM.Scratch";
        micromapScratchDesc.size = micromapSizes.buildScratchSize;
        micromapScratchDesc.usage = kScratchBufferUsages;
        micromapScratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
        micromapScratchDesc.initialState = RhiResourceState::MicromapBuildScratch;
        micromapScratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
        task.micromapScratchBuffer = m_device->createBuffer(micromapScratchDesc, nullptr, 0u);
        if (task.micromapStorageBuffer.isValid()) {
            task.micromap =
                m_device->createMicromap({"Terrain.OMM", task.micromapStorageBuffer, 0u, micromapSizes.micromapSize});
        }
        if (!task.micromapStorageBuffer.isValid() || !task.micromapScratchBuffer.isValid() ||
            !task.micromap.isValid()) {
            destroyBuildAttempt(task);
            setTransientError("Terrain opacity micromap GPU resource creation failed");
            return false;
        }
    }

    std::array<RhiAccelerationStructureGeometryDesc, 2u> geometries{};
    std::array<RhiAccelerationStructureBuildRangeDesc, 2u> ranges{};
    uint32_t geometryCount = 0u;
    const auto appendGeometry = [&](const uint32_t vertexOffset, const uint32_t vertexCount,
                                    const RhiAccelerationStructureGeometryFlags flags, const bool attachMicromap) {
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
        if (attachMicromap) {
            geometry.opacityMicromap.micromap = task.micromap;
            geometry.opacityMicromap.usages = &micromapUsage;
            geometry.opacityMicromap.usageCount = 1u;
        }
        ranges[geometryCount].primitiveCount = vertexCount / 3u;
        ++geometryCount;
    };
    appendGeometry(0u, task.geometry.opaqueVertexCount, rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque), false);
    appendGeometry(task.geometry.opaqueVertexCount, task.geometry.cutoutVertexCount, 0u,
                   task.opacityMicromap.has_value());

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
    const GpuTimerSegmentToken dynamicResourceTimer =
        debugService != nullptr
            ? debugService->beginGpuTimer(commandList, GpuTimerPass::AccelerationStructureDynamicPrepare)
            : GpuTimerSegmentToken{};
    commandList.updateBuffer(task.geometryBuffer, 0u, task.geometry.vertices.data(), task.geometry.vertexByteSize());
    commandList.updateBuffer(task.primitiveMetadataBuffer, 0u, task.geometry.primitiveMetadata.data(),
                             task.geometry.primitiveMetadataByteSize());
    if (task.opacityMicromap.has_value()) {
        const renderer::contracts::TerrainOpacityMicromapCpuData& cpuData = *task.opacityMicromap;
        commandList.updateBuffer(task.micromapOpacityBuffer, 0u, cpuData.opacityData.data(),
                                 cpuData.opacityData.size());
        commandList.updateBuffer(task.micromapTriangleBuffer, 0u, cpuData.triangleRecords.data(),
                                 cpuData.triangleRecords.size() * sizeof(cpuData.triangleRecords.front()));
        commandList.bufferBarrier(
            {task.micromapOpacityBuffer, RhiResourceState::TransferDst, RhiResourceState::MicromapBuildInput});
        commandList.bufferBarrier(
            {task.micromapTriangleBuffer, RhiResourceState::TransferDst, RhiResourceState::MicromapBuildInput});
    }
    commandList.bufferBarrier(
        {task.geometryBuffer, RhiResourceState::TransferDst, RhiResourceState::AccelerationStructureBuildInput});
    commandList.bufferBarrier(
        {task.primitiveMetadataBuffer, RhiResourceState::TransferDst, RhiResourceState::ShaderRead});
    if (debugService != nullptr) {
        debugService->endGpuTimer(commandList, dynamicResourceTimer);
    }
    m_dynamicResourceCpuMsThisFrame +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - dynamicResourceCpuStart).count();

    commandList.resetQueryPool(m_compactedSizeQueries, task.queryIndex, 1u);
    const RhiAccelerationStructureBuildDesc build{
        buildInput, RhiAccelerationStructureBuildMode::Build, {}, task.buildAccelerationStructure, task.scratchBuffer,
        0u};
    const auto buildCpuStart = std::chrono::steady_clock::now();
    const GpuTimerSegmentToken buildTimer =
        debugService != nullptr ? debugService->beginGpuTimer(commandList, GpuTimerPass::TerrainBlasBuild)
                                : GpuTimerSegmentToken{};
    if (task.opacityMicromap.has_value()) {
        const RhiMicromapBuildDesc micromapBuild{micromapBuildInput, task.micromap, task.micromapScratchBuffer, 0u};
        if (!commandList.buildMicromaps(&micromapBuild, 1u)) {
            if (debugService != nullptr) {
                debugService->cancelGpuTimer(buildTimer);
            }
            setTransientError("Terrain opacity micromap build command was rejected");
            return false;
        }
        commandList.bufferBarrier(
            {task.micromapStorageBuffer, RhiResourceState::MicromapBuildWrite, RhiResourceState::MicromapRead});
    }
    if (!commandList.buildAccelerationStructures(&build, 1u)) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(buildTimer);
        }
        setTransientError("Terrain BLAS acceleration-structure build command was rejected");
        return false;
    }
    if (!commandList.accelerationStructureBarrier({task.buildAccelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead})) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(buildTimer);
        }
        setTransientError("Terrain BLAS acceleration-structure barrier recording failed");
        return false;
    }
    if (!commandList.writeAccelerationStructureProperties(
            {&task.buildAccelerationStructure, 1u, m_compactedSizeQueries, task.queryIndex})) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(buildTimer);
        }
        setTransientError("Terrain BLAS compacted-size query recording failed");
        return false;
    }
    commandList.bufferBarrier(
        {task.geometryBuffer, RhiResourceState::AccelerationStructureBuildInput, RhiResourceState::ShaderRead});
    if (debugService != nullptr) {
        debugService->endGpuTimer(commandList, buildTimer);
    }
    m_buildCpuMsThisFrame +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildCpuStart).count();
    ++m_buildsRecordedThisFrame;
    m_buildPrimitiveCountThisFrame += task.geometry.primitiveCount();
    m_buildOpaquePrimitiveCountThisFrame += task.geometry.opaqueVertexCount / 3u;
    m_buildCutoutPrimitiveCountThisFrame += task.geometry.cutoutVertexCount / 3u;
    m_buildBlasBytesThisFrame += task.buildBlasBytes;
    m_dynamicResourceBytesThisFrame += task.geometry.uploadByteSize();
    m_scratchBytesRecordedThisFrame += task.buildScratchBytes + task.micromapScratchBytes;
    m_scratchPeakBytesThisFrame = std::max(m_scratchPeakBytesThisFrame, m_scratchBytesRecordedThisFrame);
    if (task.micromap.isValid()) {
        ++m_opacityMicromapsBuiltThisFrame;
        m_opacityMicromapPrimitivesBuiltThisFrame += task.micromapPrimitiveCount;
        m_opacityMicromapInputBytesThisFrame += task.micromapInputBytes;
        m_opacityMicromapStorageBytesThisFrame += task.micromapStorageBytes;
        m_opacityMicromapScratchBytesThisFrame += task.micromapScratchBytes;
        m_opacityMicromapCountersBuiltThisFrame.opaque += task.micromapCounters.opaque;
        m_opacityMicromapCountersBuiltThisFrame.transparent += task.micromapCounters.transparent;
        m_opacityMicromapCountersBuiltThisFrame.unknown += task.micromapCounters.unknown;
    }
    return true;
}

bool TerrainBlasCache::recordCompaction(PendingTask& task, RhiCommandList& commandList,
                                        RenderDebugService* const debugService) {
    const auto compactionCpuStart = std::chrono::steady_clock::now();
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
    const GpuTimerSegmentToken compactionTimer =
        debugService != nullptr ? debugService->beginGpuTimer(commandList, GpuTimerPass::TerrainBlasCompaction)
                                : GpuTimerSegmentToken{};
    if (!commandList.copyAccelerationStructure({task.buildAccelerationStructure, task.compactAccelerationStructure,
                                                RhiAccelerationStructureCopyMode::Compact}) ||
        !commandList.accelerationStructureBarrier({task.compactAccelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead})) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(compactionTimer);
        }
        setTransientError("Terrain BLAS compact command recording failed");
        return false;
    }
    if (debugService != nullptr) {
        debugService->endGpuTimer(commandList, compactionTimer);
    }
    m_compactionCpuMsThisFrame +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compactionCpuStart).count();
    ++m_compactionsRecordedThisFrame;
    m_compactionPrimitiveCountThisFrame +=
        (static_cast<uint64_t>(task.geometry.opaqueVertexCount) + task.geometry.cutoutVertexCount) / 3u;
    m_compactedBlasBytesThisFrame += task.compactedBlasBytes;
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
    const uint64_t vertexAddress = m_device->bufferDeviceAddress(task.geometryBuffer);
    const uint64_t primitiveMetadataAddress = m_device->bufferDeviceAddress(task.primitiveMetadataBuffer);
    const std::optional<renderer::contracts::TerrainRayTracingHitData> hitData =
        makeTerrainHitData(task.revision, vertexAddress, primitiveMetadataAddress, task.geometry.opaqueVertexCount,
                           task.geometry.cutoutVertexCount);
    if (deviceAddress == 0u || !hitData.has_value()) {
        setFatalError("Terrain BLAS compacted or hit-data device address is invalid");
        return false;
    }

    ActiveResource active;
    active.revision = task.revision;
    active.worldOrigin = task.worldOrigin;
    std::vector<RhiBufferHandle> retainedBuffers{task.geometryBuffer, task.primitiveMetadataBuffer};
    std::vector<RhiMicromapHandle> retainedMicromaps;
    if (task.micromap.isValid()) {
        retainedBuffers.push_back(task.micromapStorageBuffer);
        retainedMicromaps.push_back(task.micromap);
    }
    active.resource = renderer::rt::SceneBlasResource::create(
        *m_device, task.compactAccelerationStructure, task.compactStorageBuffer, deviceAddress, task.compactedBlasBytes,
        std::move(retainedBuffers), std::move(retainedMicromaps));
    if (active.resource == nullptr) {
        setFatalError("Terrain BLAS shared resource creation failed");
        return false;
    }
    active.geometryBuffer = task.geometryBuffer;
    active.primitiveMetadataBuffer = task.primitiveMetadataBuffer;
    active.opaqueVertexCount = task.geometry.opaqueVertexCount;
    active.cutoutVertexCount = task.geometry.cutoutVertexCount;
    active.primitiveCount = task.geometry.primitiveCount();
    active.geometryBytes = static_cast<uint64_t>(task.geometry.vertexCount()) * sizeof(BlockVertex);
    active.primitiveMetadataBytes =
        static_cast<uint64_t>(task.geometry.primitiveCount()) * sizeof(renderer::contracts::TerrainPrimitiveMetadata);
    active.opacityMicromapBytes = task.micromapStorageBytes;
    active.opacityMicromapCounters = task.micromapCounters;
    active.opacityMicromapAlphaTextureHash = task.micromapAlphaTextureHash;
    active.opacityMicromapProfileHash = task.micromapProfileHash;
    active.opacityMicromapSubdivisionLevel = task.micromapSubdivisionLevel;
    active.hitData = *hitData;
    task.compactAccelerationStructure = {};
    task.compactStorageBuffer = {};
    task.geometryBuffer = {};
    task.primitiveMetadataBuffer = {};
    task.micromapStorageBuffer = {};
    task.micromap = {};

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
    if (task.primitiveMetadataBuffer.isValid()) {
        m_device->destroyBuffer(task.primitiveMetadataBuffer);
        task.primitiveMetadataBuffer = {};
    }
    if (task.micromap.isValid()) {
        m_device->destroyMicromap(task.micromap);
        task.micromap = {};
    }
    if (task.micromapStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapStorageBuffer);
        task.micromapStorageBuffer = {};
    }
    if (task.micromapScratchBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapScratchBuffer);
        task.micromapScratchBuffer = {};
    }
    if (task.micromapOpacityBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapOpacityBuffer);
        task.micromapOpacityBuffer = {};
    }
    if (task.micromapTriangleBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapTriangleBuffer);
        task.micromapTriangleBuffer = {};
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
    if (task.primitiveMetadataBuffer.isValid()) {
        m_device->destroyBuffer(task.primitiveMetadataBuffer);
        task.primitiveMetadataBuffer = {};
    }
    if (task.micromap.isValid()) {
        m_device->destroyMicromap(task.micromap);
        task.micromap = {};
    }
    if (task.micromapStorageBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapStorageBuffer);
        task.micromapStorageBuffer = {};
    }
    if (task.micromapScratchBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapScratchBuffer);
        task.micromapScratchBuffer = {};
    }
    if (task.micromapOpacityBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapOpacityBuffer);
        task.micromapOpacityBuffer = {};
    }
    if (task.micromapTriangleBuffer.isValid()) {
        m_device->destroyBuffer(task.micromapTriangleBuffer);
        task.micromapTriangleBuffer = {};
    }
    task.buildBlasBytes = 0u;
    task.buildScratchBytes = 0u;
    task.compactedBlasBytes = 0u;
    task.micromapStorageBytes = 0u;
    task.micromapScratchBytes = 0u;
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
