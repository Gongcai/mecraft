#include "renderer/rhi/SceneTlasCache.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/debug/RenderDebugService.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace renderer::rt {
namespace {

constexpr float kSceneOriginCellSizeMeters = 128.0f;

constexpr RhiBufferUsageFlags kInstanceBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::DeviceAddress) |
    rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
constexpr RhiBufferUsageFlags kTerrainHitDataBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
constexpr RhiBufferUsageFlags kGpuSceneHitDataBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferSrc) | rhiFlag(RhiBufferUsage::TransferDst);
constexpr RhiBufferUsageFlags kAccelerationStructureStorageUsages =
    rhiFlag(RhiBufferUsage::DeviceAddress) | rhiFlag(RhiBufferUsage::AccelerationStructureStorage);
constexpr RhiBufferUsageFlags kScratchBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::DeviceAddress);
constexpr RhiAccelerationStructureBuildFlags kTlasBuildFlags =
    rhiFlag(RhiAccelerationStructureBuildFlag::PreferFastTrace);
constexpr uint64_t kMaximumCustomIndexCount = 0x01000000ull;
constexpr uint8_t kKnownInstanceMask =
    sceneTlasMaskBit(SceneTlasInstanceMask::GiOpaque) | sceneTlasMaskBit(SceneTlasInstanceMask::GiCutout) |
    sceneTlasMaskBit(SceneTlasInstanceMask::ShadowCaster) | sceneTlasMaskBit(SceneTlasInstanceMask::ReflectionVisible) |
    sceneTlasMaskBit(SceneTlasInstanceMask::FirstPerson);

[[nodiscard]] bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool sameVector(const glm::vec3& left, const glm::vec3& right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool sameMatrix(const glm::mat4& left, const glm::mat4& right) {
    return std::memcmp(&left, &right, sizeof(glm::mat4)) == 0;
}

[[nodiscard]] bool validInstanceKey(const SceneTlasInstanceKey& key) {
    switch (key.kind) {
    case SceneTlasInstanceKind::Terrain:
        return key.secondary >= std::numeric_limits<int>::min() && key.secondary <= std::numeric_limits<int>::max();
    case SceneTlasInstanceKind::StaticMesh:
    case SceneTlasInstanceKind::FirstPerson: return key.primary > 0 && key.secondary >= 0;
    }
    return false;
}

[[nodiscard]] bool sameBuffer(const RhiBufferHandle left, const RhiBufferHandle right) {
    return left.index == right.index && left.generation == right.generation;
}

[[nodiscard]] bool staticMeshHitDataBelongsToBlas(const StaticMeshRayTracingResource& hitData,
                                                  const SceneBlasResource& blas) {
    for (const StaticMeshRayTracingGeometry& geometry : hitData.geometries()) {
        const std::optional<uint64_t> vertexAddress = blas.retainedBufferDeviceAddress(geometry.vertexBuffer);
        const std::optional<uint64_t> indexAddress = blas.retainedBufferDeviceAddress(geometry.indexBuffer);
        const std::optional<uint64_t> primitiveMetadataAddress =
            blas.retainedBufferDeviceAddress(geometry.primitiveMetadataBuffer);
        if (vertexAddress !=
                std::optional(renderer::contracts::unpackGpuSceneDeviceAddress(geometry.gpu.vertexAddress)) ||
            indexAddress !=
                std::optional(renderer::contracts::unpackGpuSceneDeviceAddress(geometry.gpu.indexAddress)) ||
            primitiveMetadataAddress != std::optional(renderer::contracts::unpackGpuSceneDeviceAddress(
                                            geometry.gpu.primitiveMetadataAddress))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<glm::vec4> staticMeshWorldBounds(const StaticMeshRayTracingResource& hitData,
                                                             const glm::mat4& transform) {
    const glm::vec3 localCenter = (hitData.localBoundsMin() + hitData.localBoundsMax()) * 0.5f;
    const glm::vec3 localExtent = (hitData.localBoundsMax() - hitData.localBoundsMin()) * 0.5f;
    const glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
    const glm::mat3 linear(transform);
    const glm::vec3 worldExtent =
        glm::abs(linear[0]) * localExtent.x + glm::abs(linear[1]) * localExtent.y + glm::abs(linear[2]) * localExtent.z;
    const float radius = glm::length(worldExtent);
    if (!std::isfinite(worldCenter.x) || !std::isfinite(worldCenter.y) || !std::isfinite(worldCenter.z) ||
        !std::isfinite(radius) || radius <= 0.0f) {
        return std::nullopt;
    }
    return glm::vec4(worldCenter, radius);
}

} // namespace

bool SceneTlasCache::NormalizedInput::operator==(const NormalizedInput& other) const {
    return source.key == other.source.key && source.blas->deviceAddress() == other.source.blas->deviceAddress() &&
           sameMatrix(source.transform, other.source.transform) && source.mask == other.source.mask &&
           source.doubleSided == other.source.doubleSided && source.terrainHitData == other.source.terrainHitData &&
           source.staticMeshHitData == other.source.staticMeshHitData;
}

bool SceneTlasCache::init(RhiDevice* device) {
    shutdown();
    if (device == nullptr) {
        setFatalError("Scene TLAS cache requires a valid device");
        return false;
    }
    m_device = device;
    m_initialized = true;
    m_healthy = true;
    m_supported = device->capabilities().accelerationStructure && device->capabilities().bufferDeviceAddress;
    return true;
}

void SceneTlasCache::shutdown() {
    if (m_device != nullptr && m_supported && (m_pending.has_value() || m_active.has_value() || !m_retired.empty())) {
        m_device->waitIdle();
    }
    if (m_pending.has_value()) {
        destroyGeneration(m_pending->generation);
    }
    if (m_active.has_value()) {
        destroyGeneration(*m_active);
    }
    for (RetiredGeneration& retired : m_retired) {
        destroyGeneration(retired.generation);
    }
    m_device = nullptr;
    m_initialized = false;
    m_supported = false;
    m_healthy = true;
    m_nextRevision = 1u;
    m_buildsRecorded = 0u;
    m_buildsCompleted = 0u;
    m_buildsRecordedThisFrame = 0u;
    m_instancesRecordedThisFrame = 0u;
    m_scratchBytesThisFrame = 0u;
    m_tlasBytesThisFrame = 0u;
    m_dynamicResourceBytesThisFrame = 0u;
    m_buildCpuMsThisFrame = 0.0;
    m_dynamicResourceCpuMsThisFrame = 0.0;
    m_desiredRevision = 0u;
    m_desiredSceneOrigin = glm::vec3(0.0f);
    m_desiredInputs.clear();
    m_pending.reset();
    m_active.reset();
    m_retired.clear();
    m_lastError.clear();
}

void SceneTlasCache::beginFrame() {
    m_buildsRecordedThisFrame = 0u;
    m_instancesRecordedThisFrame = 0u;
    m_scratchBytesThisFrame = 0u;
    m_tlasBytesThisFrame = 0u;
    m_dynamicResourceBytesThisFrame = 0u;
    m_buildCpuMsThisFrame = 0.0;
    m_dynamicResourceCpuMsThisFrame = 0.0;
    pollCompletedWork();
}

void SceneTlasCache::pollCompletedWork() {
    if (!m_supported || !m_healthy) {
        return;
    }
    m_lastError.clear();
    if (!pollSubmittedGeneration() || !pollRetiredGenerations()) {
        return;
    }
    applyEmptyDesiredGeneration();
}

SceneTlasSetResult SceneTlasCache::setInstances(std::vector<SceneTlasInstanceInput> instances,
                                                const glm::vec3& sceneOrigin) {
    if (!m_initialized || !m_supported) {
        return SceneTlasSetResult::Unsupported;
    }
    if (!m_healthy) {
        return SceneTlasSetResult::InvalidInstance;
    }
    if ((m_pending.has_value() && m_pending->state == PendingState::Recorded) || !finiteVector(sceneOrigin) ||
        instances.size() > kMaximumCustomIndexCount ||
        instances.size() > m_device->capabilities().maxAccelerationStructureInstanceCount) {
        setTransientError("Scene TLAS instance transaction is invalid");
        return SceneTlasSetResult::InvalidInstance;
    }

    std::sort(
        instances.begin(), instances.end(),
        [](const SceneTlasInstanceInput& left, const SceneTlasInstanceInput& right) { return left.key < right.key; });
    std::vector<NormalizedInput> normalized;
    normalized.reserve(instances.size());
    uint64_t staticMeshBindlessIdentity = 0u;
    for (std::size_t index = 0u; index < instances.size(); ++index) {
        SceneTlasInstanceInput& source = instances[index];
        const bool unknownMaskBits = (source.mask & static_cast<uint8_t>(~kKnownInstanceMask)) != 0u;
        const bool terrainInstance = source.key.kind == SceneTlasInstanceKind::Terrain;
        const bool staticMeshInstance = source.key.kind == SceneTlasInstanceKind::StaticMesh;
        if (!validInstanceKey(source.key) || (index != 0u && source.key == instances[index - 1u].key) ||
            source.blas == nullptr || source.mask == 0u || unknownMaskBits || source.blas->deviceAddress() == 0u ||
            !source.blas->accelerationStructure().isValid() || !source.blas->storageBuffer().isValid() ||
            source.blas->blasBytes() == 0u || terrainInstance != source.terrainHitData.has_value() ||
            staticMeshInstance != (source.staticMeshHitData != nullptr) ||
            (staticMeshInstance && static_cast<uint64_t>(source.key.primary) > std::numeric_limits<uint32_t>::max())) {
            setTransientError("Scene TLAS instance identity, mask, or BLAS is invalid");
            return SceneTlasSetResult::InvalidInstance;
        }
        if (source.terrainHitData.has_value()) {
            const std::optional<uint64_t> retainedVertexAddress =
                source.blas->retainedBufferDeviceAddress(source.terrainHitData->vertexBuffer);
            const std::optional<uint64_t> retainedPrimitiveMetadataAddress =
                source.blas->retainedBufferDeviceAddress(source.terrainHitData->primitiveMetadataBuffer);
            if (!renderer::contracts::validTerrainRayTracingHitData(source.terrainHitData->rayTracing) ||
                !source.terrainHitData->vertexBuffer.isValid() ||
                !source.terrainHitData->primitiveMetadataBuffer.isValid() ||
                sameBuffer(source.terrainHitData->vertexBuffer, source.terrainHitData->primitiveMetadataBuffer) ||
                retainedVertexAddress != std::optional(source.terrainHitData->rayTracing.vertexAddress) ||
                retainedPrimitiveMetadataAddress !=
                    std::optional(source.terrainHitData->rayTracing.primitiveMetadataAddress)) {
                setTransientError("Scene TLAS terrain hit-data snapshot does not belong to the referenced BLAS");
                return SceneTlasSetResult::InvalidInstance;
            }
        }
        if (source.staticMeshHitData != nullptr) {
            const uint64_t bindlessIdentity = source.staticMeshHitData->bindlessIdentity();
            if (bindlessIdentity == 0u ||
                (staticMeshBindlessIdentity != 0u && staticMeshBindlessIdentity != bindlessIdentity) ||
                !staticMeshHitDataBelongsToBlas(*source.staticMeshHitData, *source.blas)) {
                setTransientError("Scene TLAS static-mesh hit-data snapshot does not belong to the referenced BLAS");
                return SceneTlasSetResult::InvalidInstance;
            }
            staticMeshBindlessIdentity = bindlessIdentity;
        }

        glm::mat4 rebasedTransform;
        RhiAccelerationStructureInstance native;
        if (!rebaseTransform(source.transform, sceneOrigin, rebasedTransform) ||
            !encodeTransform(rebasedTransform, native.transform)) {
            setTransientError("Scene TLAS instance transform is invalid");
            return SceneTlasSetResult::InvalidInstance;
        }
        const auto packedIdentity =
            rhiPackAccelerationStructureInstanceCustomIndexAndMask(static_cast<uint32_t>(index), source.mask);
        const RhiAccelerationStructureInstanceFlags flags = instanceFlags(source.doubleSided);
        const auto packedFlags = rhiPackAccelerationStructureInstanceShaderBindingTableOffsetAndFlags(0u, flags);
        if (!packedIdentity.has_value() || !packedFlags.has_value()) {
            setTransientError("Scene TLAS instance packed fields are invalid");
            return SceneTlasSetResult::InvalidInstance;
        }
        native.customIndexAndMask = *packedIdentity;
        native.shaderBindingTableOffsetAndFlags = *packedFlags;
        native.accelerationStructureReference = source.blas->deviceAddress();
        normalized.push_back({std::move(source), native});
    }

    if (sameVector(sceneOrigin, m_desiredSceneOrigin) && normalized == m_desiredInputs) {
        return SceneTlasSetResult::Unchanged;
    }
    if (m_nextRevision == std::numeric_limits<uint64_t>::max()) {
        setFatalError("Scene TLAS revision space is exhausted");
        return SceneTlasSetResult::InvalidInstance;
    }
    m_desiredRevision = m_nextRevision++;
    m_desiredSceneOrigin = sceneOrigin;
    m_desiredInputs = std::move(normalized);
    applyEmptyDesiredGeneration();
    return SceneTlasSetResult::Accepted;
}

bool SceneTlasCache::recordFrame(RhiCommandList& commandList, RenderDebugService* const debugService) {
    if (!m_supported) {
        return true;
    }
    if (!m_healthy || (m_pending.has_value() && m_pending->state == PendingState::Recorded)) {
        if (m_healthy) {
            setTransientError("Scene TLAS graph completion callback was not received");
        }
        return false;
    }
    // Active-generation promotion and retired-resource reclamation belong to
    // beginFrame(). This callback runs after the render graph has imported the
    // active generation's buffers; changing or reclaiming generations here
    // would invalidate those imported handles before later passes record.
    if (m_desiredInputs.empty() || m_pending.has_value() ||
        (m_active.has_value() && m_active->revision == m_desiredRevision)) {
        return true;
    }

    const auto dynamicResourceCpuStart = std::chrono::steady_clock::now();

    std::vector<RhiAccelerationStructureInstance> nativeInstances;
    nativeInstances.reserve(m_desiredInputs.size());
    std::vector<renderer::contracts::TerrainRayTracingGpuInstance> terrainHitData;
    terrainHitData.reserve(m_desiredInputs.size());
    std::vector<renderer::contracts::GpuMaterial> gpuSceneMaterials;
    std::vector<renderer::contracts::GpuSceneGeometry> gpuSceneGeometries;
    std::vector<renderer::contracts::GpuSceneInstance> gpuSceneInstances(m_desiredInputs.size());
    const std::array<uint8_t, sizeof(renderer::contracts::GpuSceneInstance)> zeroGpuSceneInstance{};
    for (renderer::contracts::GpuSceneInstance& instance : gpuSceneInstances) {
        std::memcpy(&instance, zeroGpuSceneInstance.data(), zeroGpuSceneInstance.size());
    }
    struct StaticMeshTableRange final {
        uint32_t materialBase = 0u;
        uint32_t geometryBase = 0u;
    };
    std::unordered_map<const StaticMeshRayTracingResource*, StaticMeshTableRange> staticMeshTableRanges;
    staticMeshTableRanges.reserve(m_desiredInputs.size());
    Generation generation;
    generation.revision = m_desiredRevision;
    generation.sceneOrigin = m_desiredSceneOrigin;
    generation.instanceBytes = static_cast<uint64_t>(m_desiredInputs.size()) * sizeof(RhiAccelerationStructureInstance);
    generation.terrainHitDataBytes =
        static_cast<uint64_t>(m_desiredInputs.size()) * sizeof(renderer::contracts::TerrainRayTracingGpuInstance);
    generation.blasResources.reserve(m_desiredInputs.size());
    generation.mappings.reserve(m_desiredInputs.size());
    std::unordered_set<const SceneBlasResource*> uniqueBlasResources;
    uniqueBlasResources.reserve(m_desiredInputs.size());
    std::unordered_set<const StaticMeshRayTracingResource*> uniqueStaticMeshResources;
    uniqueStaticMeshResources.reserve(m_desiredInputs.size());
    for (std::size_t index = 0u; index < m_desiredInputs.size(); ++index) {
        const NormalizedInput& input = m_desiredInputs[index];
        nativeInstances.push_back(input.native);
        renderer::contracts::TerrainRayTracingGpuInstance gpuHitData;
        if (input.source.terrainHitData.has_value()) {
            const std::optional<renderer::contracts::TerrainRayTracingGpuInstance> encoded =
                renderer::contracts::encodeTerrainRayTracingGpuInstance(input.source.terrainHitData->rayTracing);
            if (!encoded.has_value()) {
                setTransientError("Scene TLAS terrain hit-data GPU encoding failed");
                return false;
            }
            gpuHitData = *encoded;
        }
        terrainHitData.push_back(gpuHitData);
        if (input.source.staticMeshHitData != nullptr) {
            const StaticMeshRayTracingResource& hitData = *input.source.staticMeshHitData;
            const auto inserted = staticMeshTableRanges.try_emplace(&hitData);
            StaticMeshTableRange& range = inserted.first->second;
            if (inserted.second) {
                if (hitData.materials().size() > std::numeric_limits<uint32_t>::max() - gpuSceneMaterials.size() ||
                    hitData.geometries().size() > std::numeric_limits<uint32_t>::max() - gpuSceneGeometries.size()) {
                    setTransientError("Scene TLAS GPU Scene table count exceeds the 32-bit contract");
                    return false;
                }
                range.materialBase = static_cast<uint32_t>(gpuSceneMaterials.size());
                range.geometryBase = static_cast<uint32_t>(gpuSceneGeometries.size());
                gpuSceneMaterials.insert(gpuSceneMaterials.end(), hitData.materials().begin(),
                                         hitData.materials().end());
                for (const StaticMeshRayTracingGeometry& geometry : hitData.geometries()) {
                    gpuSceneGeometries.push_back(geometry.gpu);
                }
            }
            if (generation.bindlessIdentity != 0u && generation.bindlessIdentity != hitData.bindlessIdentity()) {
                setTransientError("Scene TLAS static-mesh resources reference different Global Bindless generations");
                return false;
            }
            generation.bindlessIdentity = hitData.bindlessIdentity();
            if (uniqueStaticMeshResources.insert(&hitData).second) {
                generation.staticMeshResources.push_back(input.source.staticMeshHitData);
            }

            glm::mat4 rebasedTransform;
            if (!rebaseTransform(input.source.transform, generation.sceneOrigin, rebasedTransform)) {
                setTransientError("Scene TLAS static-mesh transform rebasing failed");
                return false;
            }
            const std::optional<glm::vec4> worldBounds = staticMeshWorldBounds(hitData, rebasedTransform);
            renderer::contracts::GpuSceneInstanceFlags instanceFlags =
                renderer::contracts::gpuSceneInstanceFlagBit(renderer::contracts::GpuSceneInstanceFlag::Enabled) |
                renderer::contracts::gpuSceneInstanceFlagBit(
                    renderer::contracts::GpuSceneInstanceFlag::RayTracingVisible);
            if ((input.source.mask & sceneTlasMaskBit(SceneTlasInstanceMask::ShadowCaster)) != 0u) {
                instanceFlags |= renderer::contracts::gpuSceneInstanceFlagBit(
                    renderer::contracts::GpuSceneInstanceFlag::ShadowCaster);
            }
            if ((input.source.mask & sceneTlasMaskBit(SceneTlasInstanceMask::ReflectionVisible)) != 0u) {
                instanceFlags |= renderer::contracts::gpuSceneInstanceFlagBit(
                    renderer::contracts::GpuSceneInstanceFlag::ReflectionVisible);
            }
            if (!worldBounds.has_value()) {
                setTransientError("Scene TLAS static-mesh world bounds are invalid");
                return false;
            }
            renderer::contracts::GpuSceneInstanceNormalizationInput sceneInput;
            sceneInput.worldFromObject = rebasedTransform;
            sceneInput.previousWorldFromObject = rebasedTransform;
            sceneInput.worldBoundsCenterAndRadius = *worldBounds;
            sceneInput.geometryBase = range.geometryBase;
            sceneInput.geometryCount = static_cast<uint32_t>(hitData.geometries().size());
            sceneInput.materialBase = range.materialBase;
            sceneInput.flags = instanceFlags;
            sceneInput.stableObjectId =
                renderer::contracts::StableObjectId{static_cast<uint32_t>(input.source.key.primary)};
            sceneInput.rayTracingInstanceId = static_cast<uint32_t>(index);
            const renderer::contracts::GpuSceneInstanceNormalizationResult normalizedScene =
                renderer::contracts::normalizeGpuSceneInstance(sceneInput);
            if (!normalizedScene.succeeded()) {
                setTransientError("Scene TLAS static-mesh GPU Scene instance normalization failed");
                return false;
            }
            gpuSceneInstances[index] = normalizedScene.instance;
        }
        if (uniqueBlasResources.insert(input.source.blas.get()).second) {
            if (generation.blasBytes > std::numeric_limits<uint64_t>::max() - input.source.blas->blasBytes()) {
                setTransientError("Scene TLAS referenced BLAS byte count overflows the 64-bit contract");
                return false;
            }
            generation.blasResources.push_back(input.source.blas);
            generation.blasBytes += input.source.blas->blasBytes();
        }
        generation.mappings.push_back({static_cast<uint32_t>(index), input.source.key, input.source.terrainHitData,
                                       input.source.staticMeshHitData});
    }
    generation.gpuSceneMaterialCount = static_cast<uint32_t>(gpuSceneMaterials.size());
    generation.gpuSceneGeometryCount = static_cast<uint32_t>(gpuSceneGeometries.size());
    generation.gpuSceneMaterialBytes = static_cast<uint64_t>(std::max<std::size_t>(gpuSceneMaterials.size(), 1u)) *
                                       sizeof(renderer::contracts::GpuMaterial);
    generation.gpuSceneGeometryBytes = static_cast<uint64_t>(std::max<std::size_t>(gpuSceneGeometries.size(), 1u)) *
                                       sizeof(renderer::contracts::GpuSceneGeometry);
    generation.gpuSceneInstanceBytes =
        static_cast<uint64_t>(gpuSceneInstances.size()) * sizeof(renderer::contracts::GpuSceneInstance);

    RhiBufferDesc instanceDesc;
    instanceDesc.debugName = "Scene.TLAS.Instances";
    instanceDesc.size = generation.instanceBytes;
    instanceDesc.usage = kInstanceBufferUsages;
    instanceDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    instanceDesc.initialState = RhiResourceState::TransferDst;
    instanceDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    generation.instanceBuffer = m_device->createBuffer(instanceDesc, nullptr, 0u);

    RhiBufferDesc terrainHitDataDesc;
    terrainHitDataDesc.debugName = "Scene.TLAS.TerrainHitData";
    terrainHitDataDesc.size = generation.terrainHitDataBytes;
    terrainHitDataDesc.usage = kTerrainHitDataBufferUsages;
    terrainHitDataDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    terrainHitDataDesc.initialState = RhiResourceState::TransferDst;
    terrainHitDataDesc.memoryCategory = RhiMemoryCategory::SceneData;
    generation.terrainHitDataBuffer = m_device->createBuffer(terrainHitDataDesc, nullptr, 0u);

    RhiBufferDesc gpuSceneMaterialDesc;
    gpuSceneMaterialDesc.debugName = "Scene.TLAS.GpuSceneMaterials";
    gpuSceneMaterialDesc.size = generation.gpuSceneMaterialBytes;
    gpuSceneMaterialDesc.usage = kGpuSceneHitDataBufferUsages;
    gpuSceneMaterialDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    gpuSceneMaterialDesc.initialState = RhiResourceState::TransferDst;
    gpuSceneMaterialDesc.memoryCategory = RhiMemoryCategory::SceneData;
    generation.gpuSceneMaterialBuffer = m_device->createBuffer(gpuSceneMaterialDesc, nullptr, 0u);

    RhiBufferDesc gpuSceneGeometryDesc;
    gpuSceneGeometryDesc.debugName = "Scene.TLAS.GpuSceneGeometries";
    gpuSceneGeometryDesc.size = generation.gpuSceneGeometryBytes;
    gpuSceneGeometryDesc.usage = kGpuSceneHitDataBufferUsages;
    gpuSceneGeometryDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    gpuSceneGeometryDesc.initialState = RhiResourceState::TransferDst;
    gpuSceneGeometryDesc.memoryCategory = RhiMemoryCategory::SceneData;
    generation.gpuSceneGeometryBuffer = m_device->createBuffer(gpuSceneGeometryDesc, nullptr, 0u);

    RhiBufferDesc gpuSceneInstanceDesc;
    gpuSceneInstanceDesc.debugName = "Scene.TLAS.GpuSceneInstances";
    gpuSceneInstanceDesc.size = generation.gpuSceneInstanceBytes;
    gpuSceneInstanceDesc.usage = kGpuSceneHitDataBufferUsages;
    gpuSceneInstanceDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    gpuSceneInstanceDesc.initialState = RhiResourceState::TransferDst;
    gpuSceneInstanceDesc.memoryCategory = RhiMemoryCategory::SceneData;
    generation.gpuSceneInstanceBuffer = m_device->createBuffer(gpuSceneInstanceDesc, nullptr, 0u);

    RhiAccelerationStructureGeometryDesc instanceGeometry;
    instanceGeometry.type = RhiAccelerationStructureGeometryType::Instances;
    instanceGeometry.instances.buffer = generation.instanceBuffer;
    RhiAccelerationStructureBuildRangeDesc instanceRange;
    instanceRange.primitiveCount = static_cast<uint32_t>(nativeInstances.size());
    RhiAccelerationStructureBuildInput buildInput;
    buildInput.type = RhiAccelerationStructureType::TopLevel;
    buildInput.flags = kTlasBuildFlags;
    buildInput.geometries = &instanceGeometry;
    buildInput.ranges = &instanceRange;
    buildInput.geometryCount = 1u;
    RhiAccelerationStructureBuildSizes buildSizes;
    if (!generation.instanceBuffer.isValid() || !generation.terrainHitDataBuffer.isValid() ||
        !generation.gpuSceneMaterialBuffer.isValid() || !generation.gpuSceneGeometryBuffer.isValid() ||
        !generation.gpuSceneInstanceBuffer.isValid() ||
        !m_device->queryAccelerationStructureBuildSizes(buildInput, buildSizes) ||
        buildSizes.accelerationStructureSize == 0u || buildSizes.buildScratchSize == 0u) {
        destroyGeneration(generation);
        setTransientError("Scene TLAS build-size query failed");
        return false;
    }

    RhiBufferDesc storageDesc;
    storageDesc.debugName = "Scene.TLAS.Storage";
    storageDesc.size = buildSizes.accelerationStructureSize;
    storageDesc.usage = kAccelerationStructureStorageUsages;
    storageDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    storageDesc.initialState = RhiResourceState::AccelerationStructureBuildWrite;
    storageDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    generation.storageBuffer = m_device->createBuffer(storageDesc, nullptr, 0u);

    RhiBufferDesc scratchDesc;
    scratchDesc.debugName = "Scene.TLAS.Scratch";
    scratchDesc.size = buildSizes.buildScratchSize;
    scratchDesc.usage = kScratchBufferUsages;
    scratchDesc.memoryUsage = RhiMemoryUsage::GpuOnly;
    scratchDesc.initialState = RhiResourceState::AccelerationStructureBuildScratch;
    scratchDesc.memoryCategory = RhiMemoryCategory::AccelerationStructure;
    generation.scratchBuffer = m_device->createBuffer(scratchDesc, nullptr, 0u);
    if (generation.storageBuffer.isValid()) {
        generation.accelerationStructure =
            m_device->createAccelerationStructure({"Scene.TLAS", RhiAccelerationStructureType::TopLevel,
                                                   generation.storageBuffer, 0u, buildSizes.accelerationStructureSize});
    }
    if (!generation.storageBuffer.isValid() || !generation.scratchBuffer.isValid() ||
        !generation.accelerationStructure.isValid()) {
        destroyGeneration(generation);
        setTransientError("Scene TLAS build resource creation failed");
        return false;
    }
    generation.tlasBytes = buildSizes.accelerationStructureSize;

    const GpuTimerSegmentToken dynamicResourceTimer =
        debugService != nullptr
            ? debugService->beginGpuTimer(commandList, GpuTimerPass::AccelerationStructureDynamicPrepare)
            : GpuTimerSegmentToken{};
    commandList.updateBuffer(generation.instanceBuffer, 0u, nativeInstances.data(), instanceDesc.size);
    commandList.updateBuffer(generation.terrainHitDataBuffer, 0u, terrainHitData.data(), terrainHitDataDesc.size);
    const std::array<uint8_t, sizeof(renderer::contracts::GpuMaterial)> zeroMaterialRecord{};
    const std::array<uint8_t, sizeof(renderer::contracts::GpuSceneGeometry)> zeroGeometryRecord{};
    const void* materialData = gpuSceneMaterials.empty() ? static_cast<const void*>(zeroMaterialRecord.data())
                                                         : static_cast<const void*>(gpuSceneMaterials.data());
    const void* geometryData = gpuSceneGeometries.empty() ? static_cast<const void*>(zeroGeometryRecord.data())
                                                          : static_cast<const void*>(gpuSceneGeometries.data());
    commandList.updateBuffer(generation.gpuSceneMaterialBuffer, 0u, materialData, gpuSceneMaterialDesc.size);
    commandList.updateBuffer(generation.gpuSceneGeometryBuffer, 0u, geometryData, gpuSceneGeometryDesc.size);
    commandList.updateBuffer(generation.gpuSceneInstanceBuffer, 0u, gpuSceneInstances.data(),
                             gpuSceneInstanceDesc.size);
    commandList.bufferBarrier(
        {generation.instanceBuffer, RhiResourceState::TransferDst, RhiResourceState::AccelerationStructureBuildInput});
    commandList.bufferBarrier(
        {generation.terrainHitDataBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    commandList.bufferBarrier(
        {generation.gpuSceneMaterialBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    commandList.bufferBarrier(
        {generation.gpuSceneGeometryBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    commandList.bufferBarrier(
        {generation.gpuSceneInstanceBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    if (debugService != nullptr) {
        debugService->endGpuTimer(commandList, dynamicResourceTimer);
    }
    m_dynamicResourceCpuMsThisFrame +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - dynamicResourceCpuStart).count();

    const RhiAccelerationStructureBuildDesc buildDesc{buildInput,
                                                      RhiAccelerationStructureBuildMode::Build,
                                                      {},
                                                      generation.accelerationStructure,
                                                      generation.scratchBuffer,
                                                      0u};
    const auto buildCpuStart = std::chrono::steady_clock::now();
    const GpuTimerSegmentToken buildTimer = debugService != nullptr
                                                ? debugService->beginGpuTimer(commandList, GpuTimerPass::SceneTlas)
                                                : GpuTimerSegmentToken{};
    if (!commandList.buildAccelerationStructures(&buildDesc, 1u)) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(buildTimer);
        }
        destroyGeneration(generation);
        setTransientError("Scene TLAS build command recording failed");
        return false;
    }
    m_pending = PendingGeneration{PendingState::Recorded, std::move(generation)};
    if (!commandList.accelerationStructureBarrier({m_pending->generation.accelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead})) {
        if (debugService != nullptr) {
            debugService->cancelGpuTimer(buildTimer);
        }
        setTransientError("Scene TLAS read barrier recording failed");
        return false;
    }
    if (debugService != nullptr) {
        debugService->endGpuTimer(commandList, buildTimer);
    }
    m_buildCpuMsThisFrame +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildCpuStart).count();
    ++m_buildsRecorded;
    ++m_buildsRecordedThisFrame;
    m_instancesRecordedThisFrame += m_pending->generation.mappings.size();
    m_scratchBytesThisFrame += buildSizes.buildScratchSize;
    m_tlasBytesThisFrame += m_pending->generation.tlasBytes;
    m_dynamicResourceBytesThisFrame += m_pending->generation.instanceBytes + m_pending->generation.terrainHitDataBytes +
                                       m_pending->generation.gpuSceneMaterialBytes +
                                       m_pending->generation.gpuSceneGeometryBytes +
                                       m_pending->generation.gpuSceneInstanceBytes;
    return true;
}

void SceneTlasCache::finishGraphExecution(const bool succeeded, const RhiSubmissionToken completionToken) {
    if (completionToken.isValid() && m_active.has_value()) {
        m_active->lastUseToken = completionToken;
    }
    if (!m_pending.has_value() || m_pending->state != PendingState::Recorded) {
        return;
    }

    if (succeeded && completionToken.isValid()) {
        m_pending->state = PendingState::Submitted;
        m_pending->generation.submissionToken = completionToken;
        if (m_pending->generation.scratchBuffer.isValid()) {
            m_device->destroyBuffer(m_pending->generation.scratchBuffer);
            m_pending->generation.scratchBuffer = {};
        }
        return;
    }

    if (succeeded) {
        setTransientError("Scene TLAS graph submission token is invalid");
    }
    if (completionToken.isValid()) {
        Generation generation = std::move(m_pending->generation);
        if (generation.scratchBuffer.isValid()) {
            m_device->destroyBuffer(generation.scratchBuffer);
            generation.scratchBuffer = {};
        }
        m_retired.push_back({std::move(generation), completionToken});
    } else {
        destroyGeneration(m_pending->generation);
    }
    m_pending.reset();
}

bool SceneTlasCache::isSettled() const {
    if (!m_healthy || m_pending.has_value() || !m_retired.empty()) {
        return false;
    }
    if (m_desiredInputs.empty()) {
        return !m_active.has_value();
    }
    return m_active.has_value() && m_active->revision == m_desiredRevision;
}

std::optional<SceneTlasView> SceneTlasCache::activeView() const {
    if (!m_active.has_value()) {
        return std::nullopt;
    }
    const Generation& active = *m_active;
    SceneTlasView view;
    view.revision = active.revision;
    view.sceneOrigin = active.sceneOrigin;
    view.accelerationStructure = active.accelerationStructure;
    view.instanceBuffer = active.instanceBuffer;
    view.terrainHitDataBuffer = active.terrainHitDataBuffer;
    view.gpuSceneMaterialBuffer = active.gpuSceneMaterialBuffer;
    view.gpuSceneGeometryBuffer = active.gpuSceneGeometryBuffer;
    view.gpuSceneInstanceBuffer = active.gpuSceneInstanceBuffer;
    view.deviceAddress = active.deviceAddress;
    view.bindlessIdentity = active.bindlessIdentity;
    view.instanceCount = static_cast<uint32_t>(active.mappings.size());
    view.blasCount = static_cast<uint32_t>(active.blasResources.size());
    view.gpuSceneMaterialCount = active.gpuSceneMaterialCount;
    view.gpuSceneGeometryCount = active.gpuSceneGeometryCount;
    view.instanceBytes = active.instanceBytes;
    view.terrainHitDataBytes = active.terrainHitDataBytes;
    view.gpuSceneMaterialBytes = active.gpuSceneMaterialBytes;
    view.gpuSceneGeometryBytes = active.gpuSceneGeometryBytes;
    view.gpuSceneInstanceBytes = active.gpuSceneInstanceBytes;
    view.blasBytes = active.blasBytes;
    view.tlasBytes = active.tlasBytes;
    view.mappings = active.mappings;
    return view;
}

SceneTlasStats SceneTlasCache::stats() const {
    SceneTlasStats result;
    result.supported = m_supported;
    result.healthy = m_healthy;
    result.active = m_active.has_value();
    result.pending = m_pending.has_value();
    result.desiredInstanceCount = static_cast<uint32_t>(m_desiredInputs.size());
    result.retiredGenerationCount = static_cast<uint32_t>(m_retired.size());
    result.desiredRevision = m_desiredRevision;
    result.buildsRecorded = m_buildsRecorded;
    result.buildsCompleted = m_buildsCompleted;
    result.buildsRecordedThisFrame = m_buildsRecordedThisFrame;
    result.instancesRecordedThisFrame = m_instancesRecordedThisFrame;
    result.scratchBytesThisFrame = m_scratchBytesThisFrame;
    result.tlasBytesThisFrame = m_tlasBytesThisFrame;
    result.dynamicResourceBytesThisFrame = m_dynamicResourceBytesThisFrame;
    result.buildCpuMsThisFrame = m_buildCpuMsThisFrame;
    result.dynamicResourceCpuMsThisFrame = m_dynamicResourceCpuMsThisFrame;
    if (m_active.has_value()) {
        result.activeInstanceCount = static_cast<uint32_t>(m_active->mappings.size());
        result.activeBlasCount = static_cast<uint32_t>(m_active->blasResources.size());
        result.activeRevision = m_active->revision;
        result.activeInstanceBytes = m_active->instanceBytes;
        result.activeTerrainHitDataBytes = m_active->terrainHitDataBytes;
        result.activeGpuSceneMaterialBytes = m_active->gpuSceneMaterialBytes;
        result.activeGpuSceneGeometryBytes = m_active->gpuSceneGeometryBytes;
        result.activeGpuSceneInstanceBytes = m_active->gpuSceneInstanceBytes;
        result.activeBlasBytes = m_active->blasBytes;
        result.activeTlasBytes = m_active->tlasBytes;
        result.activeSceneOrigin = m_active->sceneOrigin;
    }
    return result;
}

bool SceneTlasCache::encodeTransform(const glm::mat4& transform, std::array<float, 12u>& nativeTransform) {
    if (!finiteMatrix(transform) || std::abs(transform[0][3]) > 1e-6f || std::abs(transform[1][3]) > 1e-6f ||
        std::abs(transform[2][3]) > 1e-6f || std::abs(transform[3][3] - 1.0f) > 1e-6f) {
        return false;
    }
    const float determinant = glm::determinant(glm::mat3(transform));
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            nativeTransform[static_cast<std::size_t>(row * 4 + column)] = transform[column][row];
        }
    }
    return true;
}

RhiAccelerationStructureInstanceFlags SceneTlasCache::instanceFlags(const bool doubleSided) {
    RhiAccelerationStructureInstanceFlags flags =
        rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFrontCounterClockwise);
    if (doubleSided) {
        flags |= rhiFlag(RhiAccelerationStructureInstanceFlag::TriangleFacingCullDisable);
    }
    return flags;
}

std::optional<glm::vec3> SceneTlasCache::sceneOriginForCamera(const glm::vec3& cameraPosition) {
    if (!finiteVector(cameraPosition)) {
        return std::nullopt;
    }
    return glm::floor(cameraPosition / kSceneOriginCellSizeMeters) * kSceneOriginCellSizeMeters;
}

bool SceneTlasCache::rebaseTransform(const glm::mat4& transform, const glm::vec3& sceneOrigin,
                                     glm::mat4& rebasedTransform) {
    if (!finiteMatrix(transform) || !finiteVector(sceneOrigin) || std::abs(transform[0][3]) > 1.0e-6f ||
        std::abs(transform[1][3]) > 1.0e-6f || std::abs(transform[2][3]) > 1.0e-6f ||
        std::abs(transform[3][3] - 1.0f) > 1.0e-6f) {
        return false;
    }
    rebasedTransform = transform;
    const glm::dvec3 translated = glm::dvec3(transform[3]) - glm::dvec3(sceneOrigin);
    if (!std::isfinite(translated.x) || !std::isfinite(translated.y) || !std::isfinite(translated.z) ||
        std::abs(translated.x) > static_cast<double>(std::numeric_limits<float>::max()) ||
        std::abs(translated.y) > static_cast<double>(std::numeric_limits<float>::max()) ||
        std::abs(translated.z) > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    rebasedTransform[3] = glm::vec4(static_cast<float>(translated.x), static_cast<float>(translated.y),
                                    static_cast<float>(translated.z), 1.0f);
    return true;
}

bool SceneTlasCache::pollSubmittedGeneration() {
    if (!m_pending.has_value() || m_pending->state != PendingState::Submitted) {
        return true;
    }
    bool complete = false;
    if (!m_device->isSubmissionComplete(m_pending->generation.submissionToken, complete)) {
        setFatalError("Scene TLAS submission completion query failed");
        return false;
    }
    if (!complete) {
        return true;
    }

    const uint64_t deviceAddress =
        m_device->accelerationStructureDeviceAddress(m_pending->generation.accelerationStructure);
    if (deviceAddress == 0u) {
        setFatalError("Scene TLAS completed generation has no traversal address");
        return false;
    }

    if (m_active.has_value()) {
        retireActiveGeneration();
    }
    Generation completed = std::move(m_pending->generation);
    completed.deviceAddress = deviceAddress;
    completed.submissionToken = {};
    m_active = std::move(completed);
    m_pending.reset();
    ++m_buildsCompleted;
    return true;
}

bool SceneTlasCache::pollRetiredGenerations() {
    for (auto retiredIt = m_retired.begin(); retiredIt != m_retired.end();) {
        if (!retiredIt->completionToken.isValid()) {
            setFatalError("Scene TLAS retired generation has an invalid completion token");
            return false;
        }
        bool complete = false;
        if (!m_device->isSubmissionComplete(retiredIt->completionToken, complete)) {
            setFatalError("Scene TLAS retired-generation completion query failed");
            return false;
        }
        if (!complete) {
            ++retiredIt;
            continue;
        }
        destroyGeneration(retiredIt->generation);
        retiredIt = m_retired.erase(retiredIt);
    }
    return true;
}

void SceneTlasCache::retireActiveGeneration() {
    if (!m_active.has_value()) {
        return;
    }
    Generation generation = std::move(*m_active);
    const RhiSubmissionToken completionToken = generation.lastUseToken;
    m_active.reset();
    if (completionToken.isValid()) {
        m_retired.push_back({std::move(generation), completionToken});
    } else {
        destroyGeneration(generation);
    }
}

void SceneTlasCache::applyEmptyDesiredGeneration() {
    if (m_desiredRevision != 0u && m_desiredInputs.empty() && m_active.has_value()) {
        retireActiveGeneration();
    }
}

void SceneTlasCache::destroyGeneration(Generation& generation) {
    if (m_device != nullptr) {
        if (generation.accelerationStructure.isValid()) {
            m_device->destroyAccelerationStructure(generation.accelerationStructure);
        }
        if (generation.scratchBuffer.isValid()) {
            m_device->destroyBuffer(generation.scratchBuffer);
        }
        if (generation.storageBuffer.isValid()) {
            m_device->destroyBuffer(generation.storageBuffer);
        }
        if (generation.instanceBuffer.isValid()) {
            m_device->destroyBuffer(generation.instanceBuffer);
        }
        if (generation.terrainHitDataBuffer.isValid()) {
            m_device->destroyBuffer(generation.terrainHitDataBuffer);
        }
        if (generation.gpuSceneMaterialBuffer.isValid()) {
            m_device->destroyBuffer(generation.gpuSceneMaterialBuffer);
        }
        if (generation.gpuSceneGeometryBuffer.isValid()) {
            m_device->destroyBuffer(generation.gpuSceneGeometryBuffer);
        }
        if (generation.gpuSceneInstanceBuffer.isValid()) {
            m_device->destroyBuffer(generation.gpuSceneInstanceBuffer);
        }
    }
    generation = {};
}

void SceneTlasCache::setTransientError(const char* message) {
    m_lastError = message;
}

void SceneTlasCache::setFatalError(const char* message) {
    setTransientError(message);
    m_healthy = false;
}

} // namespace renderer::rt
