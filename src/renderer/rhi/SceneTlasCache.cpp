#include "renderer/rhi/SceneTlasCache.h"

#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

namespace renderer::rt {
namespace {

constexpr RhiBufferUsageFlags kInstanceBufferUsages =
    rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::DeviceAddress) |
    rhiFlag(RhiBufferUsage::AccelerationStructureBuildInput);
constexpr RhiBufferUsageFlags kTerrainHitDataBufferUsages =
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

} // namespace

bool SceneTlasCache::NormalizedInput::operator==(const NormalizedInput& other) const {
    return source.key == other.source.key && source.blas->deviceAddress() == other.source.blas->deviceAddress() &&
           sameMatrix(source.transform, other.source.transform) && source.mask == other.source.mask &&
           source.doubleSided == other.source.doubleSided && source.terrainHitData == other.source.terrainHitData;
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
    m_desiredRevision = 0u;
    m_desiredInputs.clear();
    m_pending.reset();
    m_active.reset();
    m_retired.clear();
    m_lastError.clear();
}

void SceneTlasCache::beginFrame() {
    if (!m_supported || !m_healthy) {
        return;
    }
    m_lastError.clear();
    if (!pollSubmittedGeneration() || !pollRetiredGenerations()) {
        return;
    }
    applyEmptyDesiredGeneration();
}

SceneTlasSetResult SceneTlasCache::setInstances(std::vector<SceneTlasInstanceInput> instances) {
    if (!m_initialized || !m_supported) {
        return SceneTlasSetResult::Unsupported;
    }
    if (!m_healthy) {
        return SceneTlasSetResult::InvalidInstance;
    }
    if ((m_pending.has_value() && m_pending->state == PendingState::Recorded) ||
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
    for (std::size_t index = 0u; index < instances.size(); ++index) {
        SceneTlasInstanceInput& source = instances[index];
        const bool unknownMaskBits = (source.mask & static_cast<uint8_t>(~kKnownInstanceMask)) != 0u;
        const bool terrainInstance = source.key.kind == SceneTlasInstanceKind::Terrain;
        if (!validInstanceKey(source.key) || (index != 0u && source.key == instances[index - 1u].key) ||
            source.blas == nullptr || source.mask == 0u || unknownMaskBits || source.blas->deviceAddress() == 0u ||
            !source.blas->accelerationStructure().isValid() || !source.blas->storageBuffer().isValid() ||
            source.blas->blasBytes() == 0u || terrainInstance != source.terrainHitData.has_value()) {
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

        RhiAccelerationStructureInstance native;
        if (!encodeTransform(source.transform, native.transform)) {
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

    if (normalized == m_desiredInputs) {
        return SceneTlasSetResult::Unchanged;
    }
    if (m_nextRevision == std::numeric_limits<uint64_t>::max()) {
        setFatalError("Scene TLAS revision space is exhausted");
        return SceneTlasSetResult::InvalidInstance;
    }
    m_desiredRevision = m_nextRevision++;
    m_desiredInputs = std::move(normalized);
    applyEmptyDesiredGeneration();
    return SceneTlasSetResult::Accepted;
}

bool SceneTlasCache::recordFrame(RhiCommandList& commandList) {
    if (!m_supported) {
        return true;
    }
    if (!m_healthy || (m_pending.has_value() && m_pending->state == PendingState::Recorded)) {
        if (m_healthy) {
            setTransientError("Scene TLAS graph completion callback was not received");
        }
        return false;
    }
    if (!pollSubmittedGeneration() || !pollRetiredGenerations()) {
        return false;
    }
    applyEmptyDesiredGeneration();
    if (m_desiredInputs.empty() || m_pending.has_value() ||
        (m_active.has_value() && m_active->revision == m_desiredRevision)) {
        return true;
    }

    std::vector<RhiAccelerationStructureInstance> nativeInstances;
    nativeInstances.reserve(m_desiredInputs.size());
    std::vector<renderer::contracts::TerrainRayTracingGpuInstance> terrainHitData;
    terrainHitData.reserve(m_desiredInputs.size());
    Generation generation;
    generation.revision = m_desiredRevision;
    generation.instanceBytes = static_cast<uint64_t>(m_desiredInputs.size()) * sizeof(RhiAccelerationStructureInstance);
    generation.terrainHitDataBytes =
        static_cast<uint64_t>(m_desiredInputs.size()) * sizeof(renderer::contracts::TerrainRayTracingGpuInstance);
    generation.blasResources.reserve(m_desiredInputs.size());
    generation.mappings.reserve(m_desiredInputs.size());
    std::unordered_set<const SceneBlasResource*> uniqueBlasResources;
    uniqueBlasResources.reserve(m_desiredInputs.size());
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
        if (uniqueBlasResources.insert(input.source.blas.get()).second) {
            if (generation.blasBytes > std::numeric_limits<uint64_t>::max() - input.source.blas->blasBytes()) {
                setTransientError("Scene TLAS referenced BLAS byte count overflows the 64-bit contract");
                return false;
            }
            generation.blasResources.push_back(input.source.blas);
            generation.blasBytes += input.source.blas->blasBytes();
        }
        generation.mappings.push_back({static_cast<uint32_t>(index), input.source.key, input.source.terrainHitData});
    }

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

    commandList.updateBuffer(generation.instanceBuffer, 0u, nativeInstances.data(), instanceDesc.size);
    commandList.updateBuffer(generation.terrainHitDataBuffer, 0u, terrainHitData.data(), terrainHitDataDesc.size);
    commandList.bufferBarrier(
        {generation.instanceBuffer, RhiResourceState::TransferDst, RhiResourceState::AccelerationStructureBuildInput});
    commandList.bufferBarrier(
        {generation.terrainHitDataBuffer, RhiResourceState::TransferDst, RhiResourceState::StorageBuffer});
    const RhiAccelerationStructureBuildDesc buildDesc{buildInput,
                                                      RhiAccelerationStructureBuildMode::Build,
                                                      {},
                                                      generation.accelerationStructure,
                                                      generation.scratchBuffer,
                                                      0u};
    if (!commandList.buildAccelerationStructures(&buildDesc, 1u)) {
        destroyGeneration(generation);
        setTransientError("Scene TLAS build command recording failed");
        return false;
    }
    m_pending = PendingGeneration{PendingState::Recorded, std::move(generation)};
    ++m_buildsRecorded;
    if (!commandList.accelerationStructureBarrier({m_pending->generation.accelerationStructure,
                                                   RhiResourceState::AccelerationStructureBuildWrite,
                                                   RhiResourceState::AccelerationStructureRead})) {
        setTransientError("Scene TLAS read barrier recording failed");
        return false;
    }
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
    return SceneTlasView{active.revision,
                         active.accelerationStructure,
                         active.instanceBuffer,
                         active.terrainHitDataBuffer,
                         active.deviceAddress,
                         static_cast<uint32_t>(active.mappings.size()),
                         static_cast<uint32_t>(active.blasResources.size()),
                         active.instanceBytes,
                         active.terrainHitDataBytes,
                         active.blasBytes,
                         active.tlasBytes,
                         active.mappings};
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
    if (m_active.has_value()) {
        result.activeInstanceCount = static_cast<uint32_t>(m_active->mappings.size());
        result.activeBlasCount = static_cast<uint32_t>(m_active->blasResources.size());
        result.activeRevision = m_active->revision;
        result.activeInstanceBytes = m_active->instanceBytes;
        result.activeTerrainHitDataBytes = m_active->terrainHitDataBytes;
        result.activeBlasBytes = m_active->blasBytes;
        result.activeTlasBytes = m_active->tlasBytes;
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
