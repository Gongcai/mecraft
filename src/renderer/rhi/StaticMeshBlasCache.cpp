#include "renderer/rhi/StaticMeshBlasCache.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"

#include <glm/vector_relational.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace renderer::rt {
namespace {

constexpr RhiAccelerationStructureBuildFlags kStaticMeshBuildFlags =
    rhiFlag(RhiAccelerationStructureBuildFlag::AllowCompaction) |
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);
constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
constexpr RhiBufferUsageFlags kScratchBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);

[[nodiscard]] uint64_t bufferIdentity(const RhiBufferHandle buffer) {
    return (static_cast<uint64_t>(buffer.generation) << 32u) | buffer.index;
}

[[nodiscard]] bool validGeometry(const StaticMeshBlasGeometry& geometry) {
    const auto finite = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!geometry.geometryId.isValid() || !geometry.materialId.isValid() || !geometry.vertexBuffer.isValid() ||
        !geometry.indexBuffer.isValid() || !geometry.primitiveMetadataBuffer.isValid() || geometry.vertexCount == 0u ||
        geometry.indexCount == 0u || geometry.indexCount % 3u != 0u ||
        geometry.vertexStride != renderer::contracts::kStaticMeshRayTracingVertexStride ||
        geometry.positionOffset != renderer::contracts::kStaticMeshRayTracingPositionOffset ||
        geometry.materialIndex == renderer::contracts::kGpuSceneInvalidTableIndex || geometry.geometryRevision == 0u ||
        !finite(geometry.localBoundsMin) || !finite(geometry.localBoundsMax) ||
        glm::any(glm::greaterThan(geometry.localBoundsMin, geometry.localBoundsMax))) {
        return false;
    }
    return bufferIdentity(geometry.vertexBuffer) != bufferIdentity(geometry.indexBuffer) &&
           bufferIdentity(geometry.vertexBuffer) != bufferIdentity(geometry.primitiveMetadataBuffer) &&
           bufferIdentity(geometry.indexBuffer) != bufferIdentity(geometry.primitiveMetadataBuffer);
}

} // namespace

bool StaticMeshBlasCache::init(RhiDevice* device) {
    shutdown();
    if (device == nullptr) {
        setError("Static mesh BLAS cache requires a valid device");
        return false;
    }
    m_device = device;
    m_initialized = true;
    m_supported = device->capabilities().accelerationStructure && device->capabilities().bufferDeviceAddress;
    m_stats.supported = m_supported;
    return true;
}

void StaticMeshBlasCache::shutdown() {
    m_resource.reset();
    m_rayTracingGeometries.clear();
    m_device = nullptr;
    m_initialized = false;
    m_supported = false;
    m_stats = {};
    m_lastError.clear();
}

StaticMeshBlasBuildResult StaticMeshBlasCache::build(RhiCommandListPool& commandListPool,
                                                     const std::vector<StaticMeshBlasGeometry>& geometries) {
    m_lastError.clear();
    if (!m_initialized || m_device == nullptr || m_resource != nullptr) {
        setError("Static mesh BLAS cache build state is invalid");
        return StaticMeshBlasBuildResult::Failed;
    }
    if (!m_supported) {
        return StaticMeshBlasBuildResult::Unsupported;
    }
    if (geometries.empty()) {
        return StaticMeshBlasBuildResult::Empty;
    }
    if (geometries.size() > std::numeric_limits<uint32_t>::max()) {
        setError("Static mesh BLAS geometry count exceeds the 32-bit contract");
        return StaticMeshBlasBuildResult::InvalidGeometry;
    }

    std::unordered_set<uint32_t> geometryIds;
    std::unordered_set<uint64_t> retainedIdentities;
    std::vector<RhiBufferHandle> retainedBuffers;
    retainedBuffers.reserve(geometries.size() * 3u);
    uint64_t primitiveCount = 0u;
    for (const StaticMeshBlasGeometry& geometry : geometries) {
        if (!validGeometry(geometry) || !geometryIds.insert(geometry.geometryId.value).second ||
            primitiveCount > std::numeric_limits<uint64_t>::max() - geometry.primitiveCount()) {
            setError("Static mesh BLAS geometry descriptor is invalid");
            return StaticMeshBlasBuildResult::InvalidGeometry;
        }
        primitiveCount += geometry.primitiveCount();
        if (!retainedIdentities.insert(bufferIdentity(geometry.vertexBuffer)).second ||
            !retainedIdentities.insert(bufferIdentity(geometry.indexBuffer)).second) {
            setError("Static mesh BLAS geometry buffers must have unique ownership");
            return StaticMeshBlasBuildResult::InvalidGeometry;
        }
        retainedBuffers.push_back(geometry.vertexBuffer);
        retainedBuffers.push_back(geometry.indexBuffer);
        if (!retainedIdentities.insert(bufferIdentity(geometry.primitiveMetadataBuffer)).second) {
            setError("Static mesh BLAS primitive metadata buffers must have unique ownership");
            return StaticMeshBlasBuildResult::InvalidGeometry;
        }
        retainedBuffers.push_back(geometry.primitiveMetadataBuffer);
    }

    std::vector<RhiAccelerationStructureGeometryDesc> nativeGeometries(geometries.size());
    std::vector<RhiAccelerationStructureBuildRangeDesc> ranges(geometries.size());
    for (std::size_t index = 0u; index < geometries.size(); ++index) {
        const StaticMeshBlasGeometry& source = geometries[index];
        RhiAccelerationStructureGeometryDesc& destination = nativeGeometries[index];
        destination.type = RhiAccelerationStructureGeometryType::Triangles;
        destination.flags = source.opaque ? rhiFlag(RhiAccelerationStructureGeometryFlag::Opaque) : 0u;
        destination.triangles.vertexBuffer = source.vertexBuffer;
        destination.triangles.vertexOffset = source.positionOffset;
        destination.triangles.vertexStride = source.vertexStride;
        destination.triangles.vertexCount = source.vertexCount;
        destination.triangles.vertexFormat = RhiVertexFormat::Float3;
        destination.triangles.indexBuffer = source.indexBuffer;
        destination.triangles.indexFormat = RhiAccelerationStructureIndexFormat::Uint32;
        ranges[index].primitiveCount = source.primitiveCount();
    }

    RhiAccelerationStructureBuildInput buildInput;
    buildInput.type = RhiAccelerationStructureType::BottomLevel;
    buildInput.flags = kStaticMeshBuildFlags;
    buildInput.geometries = nativeGeometries.data();
    buildInput.ranges = ranges.data();
    buildInput.geometryCount = static_cast<uint32_t>(nativeGeometries.size());
    RhiAccelerationStructureBuildSizes buildSizes;
    if (!m_device->queryAccelerationStructureBuildSizes(buildInput, buildSizes) ||
        buildSizes.accelerationStructureSize == 0u || buildSizes.buildScratchSize == 0u) {
        setError("Static mesh BLAS build-size query failed");
        return StaticMeshBlasBuildResult::Failed;
    }

    RhiQueryPoolHandle compactedSizeQuery = m_device->createQueryPool(
        {"StaticMesh.BLAS.CompactedSizeQuery", RhiQueryType::AccelerationStructureCompactedSize, 1u});
    RhiBufferDesc storageDesc;
    storageDesc.debugName = "StaticMesh.BLAS.Build.Storage";
    storageDesc.size = buildSizes.accelerationStructureSize;
    storageDesc.usage = kAccelerationStructureStorageUsages;
    storageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    storageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
    storageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    RhiBufferHandle buildStorage = m_device->createBuffer(storageDesc, nullptr, 0u);

    RhiBufferDesc scratchDesc;
    scratchDesc.debugName = "StaticMesh.BLAS.Build.Scratch";
    scratchDesc.size = buildSizes.buildScratchSize;
    scratchDesc.usage = kScratchBufferUsages;
    scratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    scratchDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
    scratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    RhiBufferHandle scratchBuffer = m_device->createBuffer(scratchDesc, nullptr, 0u);
    RhiAccelerationStructureHandle buildAccelerationStructure;
    if (buildStorage.isValid()) {
        buildAccelerationStructure =
            m_device->createAccelerationStructure({"StaticMesh.BLAS.Build", RhiAccelerationStructureType::BottomLevel,
                                                   buildStorage, 0u, buildSizes.accelerationStructureSize});
    }
    const auto destroyBuildResources = [&]() {
        if (buildAccelerationStructure.isValid()) {
            m_device->destroyAccelerationStructure(buildAccelerationStructure);
            buildAccelerationStructure = {};
        }
        if (scratchBuffer.isValid()) {
            m_device->destroyBuffer(scratchBuffer);
            scratchBuffer = {};
        }
        if (buildStorage.isValid()) {
            m_device->destroyBuffer(buildStorage);
            buildStorage = {};
        }
        if (compactedSizeQuery.isValid()) {
            m_device->destroyQueryPool(compactedSizeQuery);
            compactedSizeQuery = {};
        }
    };
    if (!compactedSizeQuery.isValid() || !buildStorage.isValid() || !scratchBuffer.isValid() ||
        !buildAccelerationStructure.isValid()) {
        destroyBuildResources();
        setError("Static mesh BLAS build resource creation failed");
        return StaticMeshBlasBuildResult::Failed;
    }

    RhiCommandList* buildCommands = commandListPool.acquire(RhiCommandListType::Graphics);
    if (buildCommands == nullptr ||
        !buildCommands->begin({"StaticMesh.BLAS.BuildCommands", RhiCommandListType::Graphics})) {
        destroyBuildResources();
        setError("Static mesh BLAS build command-list acquisition failed");
        return StaticMeshBlasBuildResult::Failed;
    }
    for (const StaticMeshBlasGeometry& geometry : geometries) {
        buildCommands->bufferBarrier(
            {geometry.vertexBuffer, RhiResourceState::VertexBuffer, RhiResourceState::AccelerationStructureBuildInput});
        buildCommands->bufferBarrier(
            {geometry.indexBuffer, RhiResourceState::IndexBuffer, RhiResourceState::AccelerationStructureBuildInput});
    }
    buildCommands->resetQueryPool(compactedSizeQuery, 0u, 1u);
    const RhiAccelerationStructureBuildDesc buildDesc{
        buildInput, RhiAccelerationStructureBuildMode::Build, {}, buildAccelerationStructure, scratchBuffer, 0u};
    bool buildRecorded =
        buildCommands->buildAccelerationStructures(&buildDesc, 1u) &&
        buildCommands->accelerationStructureBarrier({buildAccelerationStructure,
                                                     RhiResourceState::AccelerationStructureBuildWrite,
                                                     RhiResourceState::AccelerationStructureRead}) &&
        buildCommands->writeAccelerationStructureProperties({&buildAccelerationStructure, 1u, compactedSizeQuery, 0u});
    for (const StaticMeshBlasGeometry& geometry : geometries) {
        buildCommands->bufferBarrier(
            {geometry.vertexBuffer, RhiResourceState::AccelerationStructureBuildInput, RhiResourceState::VertexBuffer});
        buildCommands->bufferBarrier(
            {geometry.indexBuffer, RhiResourceState::AccelerationStructureBuildInput, RhiResourceState::IndexBuffer});
    }
    const bool buildCommandsEnded = buildCommands->end();
    buildRecorded = buildRecorded && buildCommandsEnded;
    RhiSubmissionToken buildToken;
    RhiCommandList* buildSubmission[] = {buildCommands};
    if (!buildRecorded ||
        !m_device->submit({"StaticMesh.BLAS.BuildSubmit", buildSubmission, 1u, RhiQueueType::Graphics}, &buildToken) ||
        !buildToken.isValid() || !m_device->waitForSubmission(buildToken)) {
        destroyBuildResources();
        setError("Static mesh BLAS build submission failed");
        return StaticMeshBlasBuildResult::Failed;
    }

    uint64_t compactedSize = 0u;
    if (!m_device->areQueryResultsAvailable(compactedSizeQuery, 0u, 1u) ||
        !m_device->getQueryResults(compactedSizeQuery, 0u, 1u, &compactedSize) || compactedSize == 0u ||
        compactedSize > buildSizes.accelerationStructureSize) {
        destroyBuildResources();
        setError("Static mesh BLAS compacted-size query result is invalid");
        return StaticMeshBlasBuildResult::Failed;
    }
    m_device->destroyQueryPool(compactedSizeQuery);
    compactedSizeQuery = {};
    m_device->destroyBuffer(scratchBuffer);
    scratchBuffer = {};

    storageDesc.debugName = "StaticMesh.BLAS.Compact.Storage";
    storageDesc.size = compactedSize;
    RhiBufferHandle compactStorage = m_device->createBuffer(storageDesc, nullptr, 0u);
    RhiAccelerationStructureHandle compactAccelerationStructure;
    if (compactStorage.isValid()) {
        compactAccelerationStructure = m_device->createAccelerationStructure(
            {"StaticMesh.BLAS.Compact", RhiAccelerationStructureType::BottomLevel, compactStorage, 0u, compactedSize});
    }
    const auto destroyCompactResources = [&]() {
        if (compactAccelerationStructure.isValid()) {
            m_device->destroyAccelerationStructure(compactAccelerationStructure);
            compactAccelerationStructure = {};
        }
        if (compactStorage.isValid()) {
            m_device->destroyBuffer(compactStorage);
            compactStorage = {};
        }
    };
    if (!compactStorage.isValid() || !compactAccelerationStructure.isValid()) {
        destroyCompactResources();
        destroyBuildResources();
        setError("Static mesh BLAS compact resource creation failed");
        return StaticMeshBlasBuildResult::Failed;
    }

    RhiCommandList* compactCommands = commandListPool.acquire(RhiCommandListType::Graphics);
    if (compactCommands == nullptr ||
        !compactCommands->begin({"StaticMesh.BLAS.CompactCommands", RhiCommandListType::Graphics})) {
        destroyCompactResources();
        destroyBuildResources();
        setError("Static mesh BLAS compact command-list acquisition failed");
        return StaticMeshBlasBuildResult::Failed;
    }
    const bool compactRecorded =
        compactCommands->copyAccelerationStructure(
            {buildAccelerationStructure, compactAccelerationStructure, RhiAccelerationStructureCopyMode::Compact}) &&
        compactCommands->accelerationStructureBarrier({compactAccelerationStructure,
                                                       RhiResourceState::AccelerationStructureBuildWrite,
                                                       RhiResourceState::AccelerationStructureRead});
    const bool compactCommandsEnded = compactCommands->end();
    if (!compactRecorded || !compactCommandsEnded) {
        destroyCompactResources();
        destroyBuildResources();
        setError("Static mesh BLAS compact command recording failed");
        return StaticMeshBlasBuildResult::Failed;
    }
    RhiSubmissionToken compactToken;
    RhiCommandList* compactSubmission[] = {compactCommands};
    if (!m_device->submit({"StaticMesh.BLAS.CompactSubmit", compactSubmission, 1u, RhiQueueType::Graphics},
                          &compactToken) ||
        !compactToken.isValid() || !m_device->waitForSubmission(compactToken)) {
        destroyCompactResources();
        destroyBuildResources();
        setError("Static mesh BLAS compact submission failed");
        return StaticMeshBlasBuildResult::Failed;
    }

    const uint64_t deviceAddress = m_device->accelerationStructureDeviceAddress(compactAccelerationStructure);
    SceneBlasResourcePtr resource = SceneBlasResource::create(*m_device, compactAccelerationStructure, compactStorage,
                                                              deviceAddress, compactedSize, std::move(retainedBuffers));
    if (resource == nullptr) {
        destroyCompactResources();
        destroyBuildResources();
        setError("Static mesh BLAS shared resource creation failed");
        return StaticMeshBlasBuildResult::Failed;
    }
    compactAccelerationStructure = {};
    compactStorage = {};
    destroyBuildResources();

    std::vector<StaticMeshRayTracingGeometry> rayTracingGeometries;
    rayTracingGeometries.reserve(geometries.size());
    for (const StaticMeshBlasGeometry& source : geometries) {
        const std::optional<uint64_t> vertexAddress = resource->retainedBufferDeviceAddress(source.vertexBuffer);
        const std::optional<uint64_t> indexAddress = resource->retainedBufferDeviceAddress(source.indexBuffer);
        const std::optional<uint64_t> primitiveMetadataAddress =
            resource->retainedBufferDeviceAddress(source.primitiveMetadataBuffer);
        renderer::contracts::GpuSceneGeometryFlags flags =
            renderer::contracts::gpuSceneGeometryFlagBit(source.opaque
                                                             ? renderer::contracts::GpuSceneGeometryFlag::Opaque
                                                             : renderer::contracts::GpuSceneGeometryFlag::Cutout) |
            renderer::contracts::gpuSceneGeometryFlagBit(renderer::contracts::GpuSceneGeometryFlag::ShadowCaster) |
            renderer::contracts::gpuSceneGeometryFlagBit(renderer::contracts::GpuSceneGeometryFlag::ReflectionVisible) |
            renderer::contracts::gpuSceneGeometryFlagBit(renderer::contracts::GpuSceneGeometryFlag::RayTracingVisible);
        if (source.doubleSided) {
            flags |=
                renderer::contracts::gpuSceneGeometryFlagBit(renderer::contracts::GpuSceneGeometryFlag::DoubleSided);
        }
        if (!vertexAddress.has_value() || !indexAddress.has_value() || !primitiveMetadataAddress.has_value()) {
            resource.reset();
            setError("Static mesh BLAS retained geometry addresses are incomplete");
            return StaticMeshBlasBuildResult::Failed;
        }
        renderer::contracts::GpuSceneGeometryNormalizationInput input;
        input.vertexAddress = *vertexAddress;
        input.indexAddress = *indexAddress;
        input.primitiveMetadataAddress = *primitiveMetadataAddress;
        input.vertexStride = static_cast<uint32_t>(source.vertexStride);
        input.positionByteOffset = static_cast<uint32_t>(source.positionOffset);
        input.vertexCount = source.vertexCount;
        input.indexCount = source.indexCount;
        input.indexType = renderer::contracts::GpuSceneIndexType::Uint32;
        input.materialIndex = source.materialIndex;
        input.stableMaterialId = source.materialId;
        input.stableGeometryId = source.geometryId;
        input.primitiveMetadataStride = sizeof(renderer::contracts::StaticMeshPrimitiveMetadata);
        input.geometryRevision = source.geometryRevision;
        input.flags = flags;
        input.localBoundsMin = source.localBoundsMin;
        input.localBoundsMax = source.localBoundsMax;
        const renderer::contracts::GpuSceneGeometryNormalizationResult normalized =
            renderer::contracts::normalizeGpuSceneGeometry(input);
        if (!normalized.succeeded()) {
            resource.reset();
            setError("Static mesh BLAS GPU Scene geometry normalization failed");
            return StaticMeshBlasBuildResult::InvalidGeometry;
        }
        rayTracingGeometries.push_back(
            {normalized.geometry, source.vertexBuffer, source.indexBuffer, source.primitiveMetadataBuffer});
    }

    m_resource = std::move(resource);
    m_rayTracingGeometries = std::move(rayTracingGeometries);
    m_stats.supported = true;
    m_stats.resident = true;
    m_stats.geometryCount = static_cast<uint32_t>(geometries.size());
    m_stats.primitiveCount = primitiveCount;
    m_stats.uncompactedBlasBytes = buildSizes.accelerationStructureSize;
    m_stats.compactedBlasBytes = compactedSize;
    m_stats.containsOpaque = std::any_of(geometries.begin(), geometries.end(),
                                         [](const StaticMeshBlasGeometry& geometry) { return geometry.opaque; });
    m_stats.containsCutout = std::any_of(geometries.begin(), geometries.end(),
                                         [](const StaticMeshBlasGeometry& geometry) { return !geometry.opaque; });
    m_stats.containsDoubleSided =
        std::any_of(geometries.begin(), geometries.end(),
                    [](const StaticMeshBlasGeometry& geometry) { return geometry.doubleSided; });
    return StaticMeshBlasBuildResult::Built;
}

void StaticMeshBlasCache::setError(const char* message) {
    m_lastError = message;
}

} // namespace renderer::rt
