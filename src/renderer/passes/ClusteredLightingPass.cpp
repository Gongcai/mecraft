#include "ClusteredLightingPass.h"

#include "Diagnostics.h"
#include "renderer/contracts/LocalShadowContract.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/rhi/RhiResources.h"
#include "renderer/rhi/RhiShaderSourceLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

constexpr uint32_t kStatsTotalIndexCount = 0u;
constexpr uint32_t kStatsMaxLightsPerCluster = 1u;
constexpr uint32_t kStatsNonEmptyClusterCount = 2u;
constexpr uint32_t kStatsBuildError = 3u;
constexpr uint32_t kStatsClusterCount = 4u;
constexpr uint32_t kStatsLightCount = 5u;
constexpr uint32_t kStatsIndexCapacity = 6u;
constexpr uint32_t kStatsContractVersion = 7u;

struct alignas(16) ClusterGridPushConstants final {
    glm::uvec4 gridAndLightCount{0u};
};

struct alignas(16) ClusterScanPushConstants final {
    glm::uvec4 offsetsAndCount{0u};
};

struct alignas(16) ClusterFillPushConstants final {
    glm::uvec4 gridAndLightCount{0u};
    glm::uvec4 capacity{0u};
};

static_assert(sizeof(ClusterGridPushConstants) == 16u);
static_assert(sizeof(ClusterScanPushConstants) == 16u);
static_assert(sizeof(ClusterFillPushConstants) == 32u);

[[nodiscard]] bool finite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] uint64_t alignBufferSize(const uint64_t size) {
    constexpr uint64_t kAlignment = 256u;
    if (size > std::numeric_limits<uint64_t>::max() - (kAlignment - 1u)) {
        return 0u;
    }
    return (size + kAlignment - 1u) & ~(kAlignment - 1u);
}

[[nodiscard]] bool multiplyBytes(const uint64_t count,
                                 const uint64_t stride,
                                 uint64_t& bytes) {
    if (count == 0u || stride == 0u ||
        count > std::numeric_limits<uint64_t>::max() / stride) {
        return false;
    }
    bytes = count * stride;
    return true;
}

void appendStorageBinding(RhiBindGroupDesc& desc,
                          const uint32_t binding,
                          const RhiBufferHandle buffer,
                          const uint64_t range) {
    RhiBindGroupEntry entry;
    entry.binding = binding;
    entry.resource.buffer.buffer = buffer;
    entry.resource.buffer.offset = 0u;
    entry.resource.buffer.range = range;
    desc.entries.push_back(entry);
}

void appendCombinedTextureSamplerBinding(
    RhiBindGroupDesc& desc,
    const uint32_t binding,
    const RhiTextureViewHandle textureView,
    const RhiSamplerHandle sampler) {
    RhiBindGroupEntry entry;
    entry.binding = binding;
    entry.resource.combinedTextureSampler.textureView = textureView;
    entry.resource.combinedTextureSampler.sampler = sampler;
    desc.entries.push_back(entry);
}

} // namespace

bool ClusteredLightingPass::setLights(
    std::vector<renderer::contracts::GpuLight> lights) {
    m_lights = std::move(lights);
    m_inputValid = validateLights();
    if (!m_inputValid) {
        m_lights.clear();
        m_prepared = false;
        return false;
    }
    m_prepared = false;
    return true;
}

bool ClusteredLightingPass::setLocalShadowResources(
    const LocalShadowResources& resources) {
    if (!resources.metadataBuffer.isValid() ||
        resources.metadataBufferBytes == 0u ||
        !resources.spotAtlasView.isValid() ||
        !resources.pointCubeArrayView.isValid() ||
        !resources.sampler.isValid()) {
        return false;
    }
    const bool changed =
        resources.metadataBuffer.index !=
            m_localShadowResources.metadataBuffer.index ||
        resources.metadataBuffer.generation !=
            m_localShadowResources.metadataBuffer.generation ||
        resources.metadataBufferBytes !=
            m_localShadowResources.metadataBufferBytes ||
        resources.spotAtlasView.index !=
            m_localShadowResources.spotAtlasView.index ||
        resources.spotAtlasView.generation !=
            m_localShadowResources.spotAtlasView.generation ||
        resources.pointCubeArrayView.index !=
            m_localShadowResources.pointCubeArrayView.index ||
        resources.pointCubeArrayView.generation !=
            m_localShadowResources.pointCubeArrayView.generation ||
        resources.sampler.index != m_localShadowResources.sampler.index ||
        resources.sampler.generation !=
            m_localShadowResources.sampler.generation;
    if (changed && m_rhiDevice != nullptr &&
        m_consumerBindGroup.isValid()) {
        m_rhiDevice->destroyBindGroup(m_consumerBindGroup);
        m_consumerBindGroup = {};
    }
    m_localShadowResources = resources;
    return true;
}

bool ClusteredLightingPass::validateLights() const {
    using namespace renderer::contracts;
    if (m_lights.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    std::unordered_set<uint32_t> stableIds;
    stableIds.reserve(m_lights.size());
    for (const GpuLight& light : m_lights) {
        const uint32_t type = light.classificationAndIdentity.x;
        const uint32_t stableId = light.classificationAndIdentity.y;
        const uint32_t shadowPolicy = light.classificationAndIdentity.z;
        const uint32_t shadowIndex = light.classificationAndIdentity.w;
        if (type > static_cast<uint32_t>(GpuLightType::Rect) ||
            stableId == 0u ||
            shadowPolicy >
                static_cast<uint32_t>(GpuLightShadowPolicy::RasterCached) ||
            light.resourcesAndFlags.w != kGpuLightContractVersion ||
            (light.resourcesAndFlags.z & ~kGpuLightKnownContributionFlags) != 0u ||
            !finite(light.positionAndRange) || !finite(light.direction) ||
            !finite(light.colorAndIntensity) ||
            !finite(light.spotCosinesAndRectSize) ||
            !stableIds.insert(stableId).second) {
            return false;
        }
        const bool noShadow =
            shadowPolicy == static_cast<uint32_t>(GpuLightShadowPolicy::None);
        if (noShadow != (shadowIndex == kGpuLightInvalidResourceIndex)) {
            return false;
        }
        if (!noShadow) {
            if (type == static_cast<uint32_t>(GpuLightType::Spot)) {
                if (shadowIndex >= kLocalShadowMaxSpotLightCount) {
                    return false;
                }
            } else if (type == static_cast<uint32_t>(GpuLightType::Point)) {
                if (shadowIndex < kLocalShadowPointMetadataBase ||
                    shadowIndex >= kLocalShadowMetadataCount) {
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    return true;
}

bool ClusteredLightingPass::prepareGraphFrame(RhiDevice& rhiDevice,
                                               const FrameContext& ctx,
                                               const uint32_t renderWidth,
                                               const uint32_t renderHeight) {
    if (rhiDevice.backend() != RhiBackend::Vulkan || !m_inputValid ||
        m_gpuBuildFailed ||
        !m_localShadowResources.metadataBuffer.isValid() ||
        m_localShadowResources.metadataBufferBytes == 0u ||
        !m_localShadowResources.spotAtlasView.isValid() ||
        !m_localShadowResources.pointCubeArrayView.isValid() ||
        !m_localShadowResources.sampler.isValid()) {
        return false;
    }
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    m_prepared = false;
    if (!consumeReadback(rhiDevice) || !validateLights() ||
        !buildCoverage(ctx, renderWidth, renderHeight) ||
        !buildScanPlan() || !ensurePipelines(rhiDevice) ||
        !ensureBuffers(rhiDevice) || !ensureBuildBindGroups(rhiDevice) ||
        !ensureConsumerBindGroup(rhiDevice)) {
        return false;
    }
    m_prepared = true;
    return true;
}

bool ClusteredLightingPass::consumeReadback(RhiDevice& rhiDevice) {
    const uint32_t ringIndex = m_statsReadbackWriteIndex;
    m_statsReadbackSlotAvailable = true;
    const RhiSubmissionToken token = m_statsReadbackTokens[ringIndex];
    if (!token.isValid()) {
        return true;
    }
    bool complete = false;
    if (!rhiDevice.isSubmissionComplete(token, complete)) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ClusteredLightingPass] Stats submission query failed\n");
        return false;
    }
    if (!complete) {
        m_statsReadbackSlotAvailable = false;
        return true;
    }
    const void* mapped = rhiDevice.mapBuffer(
        m_statsReadbackBuffers[ringIndex], 0u,
        sizeof(uint32_t) * kStatsWordCount);
    if (mapped == nullptr) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[ClusteredLightingPass] Stats readback mapping failed\n");
        return false;
    }
    uint32_t words[kStatsWordCount];
    std::memcpy(words, mapped, sizeof(words));
    rhiDevice.unmapBuffer(m_statsReadbackBuffers[ringIndex]);
    m_statsReadbackTokens[ringIndex] = {};

    m_frameStats.valid = true;
    m_frameStats.totalIndexCount = words[kStatsTotalIndexCount];
    m_frameStats.maxLightsPerCluster = words[kStatsMaxLightsPerCluster];
    m_frameStats.nonEmptyClusterCount = words[kStatsNonEmptyClusterCount];
    m_frameStats.buildError = words[kStatsBuildError];
    m_frameStats.clusterCount = words[kStatsClusterCount];
    m_frameStats.lightCount = words[kStatsLightCount];
    m_frameStats.indexCapacity = words[kStatsIndexCapacity];
    m_frameStats.averageLightsPerCluster = m_frameStats.clusterCount != 0u
        ? static_cast<float>(m_frameStats.totalIndexCount) /
              static_cast<float>(m_frameStats.clusterCount)
        : 0.0f;
    if (words[kStatsContractVersion] !=
            renderer::contracts::kGpuLightContractVersion ||
        m_frameStats.buildError != 0u) {
        m_gpuBuildFailed = true;
        MECRAFT_LOG_STREAM(
            std::cerr << "[ClusteredLightingPass] GPU build invariant failed: error="
                      << m_frameStats.buildError
                      << " totalIndices=" << m_frameStats.totalIndexCount
                      << " capacity=" << m_frameStats.indexCapacity << '\n');
        return false;
    }
    return true;
}

bool ClusteredLightingPass::buildCoverage(const FrameContext& ctx,
                                           const uint32_t renderWidth,
                                           const uint32_t renderHeight) {
    using namespace renderer::contracts;
    const std::optional<ClusterGrid> grid = buildClusterGrid(
        renderWidth, renderHeight, ctx.camera.nearPlane, ctx.camera.farPlane);
    if (!grid.has_value()) {
        return false;
    }
    m_grid = *grid;
    m_lightBounds.clear();
    m_lightBounds.reserve(m_lights.size());
    for (const GpuLight& light : m_lights) {
        const std::optional<GpuClusterLightBounds> bounds =
            buildGpuClusterLightBounds(
                light, m_grid, ctx.camera.view, ctx.camera.projection);
        if (!bounds.has_value()) {
            return false;
        }
        m_lightBounds.push_back(*bounds);
    }
    const std::optional<uint32_t> required =
        requiredClusterLightIndexCount(m_lightBounds);
    if (!required.has_value()) {
        return false;
    }
    m_requiredIndexCount = *required;
    m_zeroClusterWords.assign(m_grid.clusterCount, 0u);
    return true;
}

bool ClusteredLightingPass::buildScanPlan() {
    using namespace renderer::contracts;
    if (m_grid.clusterCount == 0u) {
        return false;
    }
    m_scanLevels.clear();
    uint64_t scratchCursor = 0u;
    ScanLevel first;
    first.elementCount = m_grid.clusterCount;
    first.groupCount =
        (first.elementCount + kClusterScanElementsPerWorkgroup - 1u) /
        kClusterScanElementsPerWorkgroup;
    first.blockSumOffsetWords = static_cast<uint32_t>(scratchCursor);
    scratchCursor += first.groupCount;
    m_scanLevels.push_back(first);

    while (m_scanLevels.back().groupCount > 1u) {
        const ScanLevel& previous = m_scanLevels.back();
        ScanLevel level;
        level.elementCount = previous.groupCount;
        level.groupCount =
            (level.elementCount + kClusterScanElementsPerWorkgroup - 1u) /
            kClusterScanElementsPerWorkgroup;
        level.inputOffsetWords = previous.blockSumOffsetWords;
        if (scratchCursor > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        level.outputOffsetWords = static_cast<uint32_t>(scratchCursor);
        scratchCursor += level.elementCount;
        if (scratchCursor > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        level.blockSumOffsetWords = static_cast<uint32_t>(scratchCursor);
        scratchCursor += level.groupCount;
        if (scratchCursor > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        m_scanLevels.push_back(level);
    }
    m_scanScratchWordCount = static_cast<uint32_t>(scratchCursor);
    return m_scanScratchWordCount != 0u;
}

bool ClusteredLightingPass::ensurePipelines(RhiDevice& rhiDevice) {
    if (m_countStage.pipeline.isValid() && m_scanStage.pipeline.isValid() &&
        m_scanAddStage.pipeline.isValid() &&
        m_finalizeStage.pipeline.isValid() && m_fillStage.pipeline.isValid() &&
        m_validateStage.pipeline.isValid() &&
        m_consumerBindGroupLayout.isValid()) {
        return true;
    }
    destroyPipelines();

    RhiBindGroupLayoutDesc consumerLayoutDesc;
    consumerLayoutDesc.debugName = "ClusteredLighting.ConsumerLayout";
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        consumerLayoutDesc.entries.push_back({
            binding, RhiBindingType::StorageBuffer,
            rhiFlag(RhiShaderStage::Fragment), 1u});
    }
    consumerLayoutDesc.entries.push_back({
        5u, RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment), 1u});
    consumerLayoutDesc.entries.push_back({
        6u, RhiBindingType::CombinedTextureSampler,
        rhiFlag(RhiShaderStage::Fragment), 1u});
    m_consumerBindGroupLayout =
        rhiDevice.createBindGroupLayout(consumerLayoutDesc);
    if (!m_consumerBindGroupLayout.isValid()) {
        return false;
    }

    const auto createStage = [&](ComputeStage& stage,
                                 const char* shaderPath,
                                 const char* debugName,
                                 const uint32_t bindingCount,
                                 const uint32_t pushConstantBytes) {
        const std::optional<std::string> source =
            renderer::rhi::loadShaderSource(shaderPath);
        if (!source.has_value()) {
            return false;
        }
        RhiShaderDesc shaderDesc;
        shaderDesc.debugName = debugName;
        shaderDesc.stage = RhiShaderStage::Compute;
        shaderDesc.source = source->c_str();
        shaderDesc.sourceSize = source->size();
        stage.shader = rhiDevice.createShader(shaderDesc);
        if (!stage.shader.isValid()) {
            return false;
        }
        RhiBindGroupLayoutDesc bindGroupLayoutDesc;
        bindGroupLayoutDesc.debugName = debugName;
        for (uint32_t binding = 0u; binding < bindingCount; ++binding) {
            bindGroupLayoutDesc.entries.push_back({
                binding, RhiBindingType::StorageBuffer,
                rhiFlag(RhiShaderStage::Compute), 1u});
        }
        stage.bindGroupLayout =
            rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
        if (!stage.bindGroupLayout.isValid()) {
            return false;
        }
        RhiPipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.debugName = debugName;
        pipelineLayoutDesc.bindGroupLayouts.push_back(stage.bindGroupLayout);
        pipelineLayoutDesc.pushConstantBytes = pushConstantBytes;
        pipelineLayoutDesc.pushConstantStages =
            rhiFlag(RhiShaderStage::Compute);
        stage.pipelineLayout =
            rhiDevice.createPipelineLayout(pipelineLayoutDesc);
        if (!stage.pipelineLayout.isValid()) {
            return false;
        }
        RhiComputePipelineDesc pipelineDesc;
        pipelineDesc.debugName = debugName;
        pipelineDesc.computeShader = stage.shader;
        pipelineDesc.layout = stage.pipelineLayout;
        stage.pipeline = rhiDevice.createComputePipeline(pipelineDesc);
        return stage.pipeline.isValid();
    };

    if (!createStage(m_countStage, "assets/shaders/cluster_count.comp",
                     "ClusteredLighting.Count", 2u,
                     sizeof(ClusterGridPushConstants)) ||
        !createStage(m_scanStage, "assets/shaders/cluster_scan.comp",
                     "ClusteredLighting.Scan", 3u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_scanAddStage, "assets/shaders/cluster_scan_add.comp",
                     "ClusteredLighting.ScanAdd", 2u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_finalizeStage, "assets/shaders/cluster_finalize.comp",
                     "ClusteredLighting.Finalize", 4u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_fillStage, "assets/shaders/cluster_fill.comp",
                     "ClusteredLighting.Fill", 5u,
                     sizeof(ClusterFillPushConstants)) ||
        !createStage(m_validateStage, "assets/shaders/cluster_validate.comp",
                     "ClusteredLighting.Validate", 3u,
                     sizeof(ClusterScanPushConstants))) {
        destroyPipelines();
        return false;
    }
    return true;
}

bool ClusteredLightingPass::ensureBuffers(RhiDevice& rhiDevice) {
    uint64_t lightBytes = 0u;
    uint64_t boundsBytes = 0u;
    uint64_t clusterWordBytes = 0u;
    uint64_t recordBytes = 0u;
    uint64_t indexBytes = 0u;
    uint64_t scratchBytes = 0u;
    if (!multiplyBytes(std::max<size_t>(m_lights.size(), 1u),
                       sizeof(renderer::contracts::GpuLight), lightBytes) ||
        !multiplyBytes(std::max<size_t>(m_lightBounds.size(), 1u),
                       sizeof(renderer::contracts::GpuClusterLightBounds),
                       boundsBytes) ||
        !multiplyBytes(m_grid.clusterCount, sizeof(uint32_t),
                       clusterWordBytes) ||
        !multiplyBytes(m_grid.clusterCount, sizeof(uint32_t) * 2u,
                       recordBytes) ||
        !multiplyBytes(std::max(m_requiredIndexCount, 1u), sizeof(uint32_t),
                       indexBytes) ||
        !multiplyBytes(std::max(m_scanScratchWordCount, 1u), sizeof(uint32_t),
                       scratchBytes)) {
        return false;
    }

    const bool buffersGrow =
        lightBytes > m_lightBuffer.capacityBytes ||
        boundsBytes > m_lightBoundsBuffer.capacityBytes ||
        clusterWordBytes > m_countBuffer.capacityBytes ||
        clusterWordBytes > m_offsetBuffer.capacityBytes ||
        recordBytes > m_recordBuffer.capacityBytes ||
        clusterWordBytes > m_cursorBuffer.capacityBytes ||
        indexBytes > m_compactIndexBuffer.capacityBytes ||
        scratchBytes > m_scanScratchBuffer.capacityBytes ||
        sizeof(uint32_t) * kStatsWordCount > m_statsBuffer.capacityBytes;
    if (buffersGrow) {
        destroyBuildBindGroups();
    }

    const RhiBufferUsageFlags storageUploadUsage =
        rhiFlag(RhiBufferUsage::Storage) |
        rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferUsageFlags storageUsage = rhiFlag(RhiBufferUsage::Storage);
    if (!ensureBuffer(rhiDevice, m_lightBuffer, lightBytes,
                      storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Lights") ||
        !ensureBuffer(rhiDevice, m_lightBoundsBuffer, boundsBytes,
                      storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.LightBounds") ||
        !ensureBuffer(rhiDevice, m_countBuffer, clusterWordBytes,
                      storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Counts") ||
        !ensureBuffer(rhiDevice, m_offsetBuffer, clusterWordBytes,
                      storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Offsets") ||
        !ensureBuffer(rhiDevice, m_recordBuffer, recordBytes,
                      storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Records") ||
        !ensureBuffer(rhiDevice, m_cursorBuffer, clusterWordBytes,
                      storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Cursors") ||
        !ensureBuffer(rhiDevice, m_compactIndexBuffer, indexBytes,
                      storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.CompactIndices") ||
        !ensureBuffer(rhiDevice, m_scanScratchBuffer, scratchBytes,
                      storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.ScanScratch") ||
        !ensureBuffer(rhiDevice, m_statsBuffer,
                      sizeof(uint32_t) * kStatsWordCount,
                      storageUploadUsage | rhiFlag(RhiBufferUsage::TransferSrc),
                      RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Stats") ||
        !ensureReadbackBuffers(rhiDevice)) {
        return false;
    }
    m_indexCapacity = static_cast<uint32_t>(
        std::min<uint64_t>(m_compactIndexBuffer.capacityBytes /
                               sizeof(uint32_t),
                           std::numeric_limits<uint32_t>::max()));
    return m_indexCapacity >= std::max(m_requiredIndexCount, 1u);
}

bool ClusteredLightingPass::ensureBuffer(
    RhiDevice& rhiDevice,
    BufferResource& resource,
    const uint64_t requiredBytes,
    const RhiBufferUsageFlags usage,
    const RhiMemoryCategory memoryCategory,
    const char* debugName) {
    if (requiredBytes == 0u) {
        return false;
    }
    if (resource.handle.isValid() &&
        resource.capacityBytes >= requiredBytes) {
        return true;
    }
    const uint64_t capacity = alignBufferSize(requiredBytes);
    if (capacity == 0u) {
        return false;
    }
    RhiBufferDesc desc;
    desc.debugName = debugName;
    desc.size = capacity;
    desc.usage = usage;
    desc.memoryUsage = RhiMemoryUsage::GpuOnly;
    desc.initialState = RhiResourceState::StorageBuffer;
    desc.memoryCategory = memoryCategory;
    const RhiBufferHandle created =
        rhiDevice.createBuffer(desc, nullptr, 0u);
    if (!created.isValid()) {
        return false;
    }
    if (resource.handle.isValid()) {
        rhiDevice.destroyBuffer(resource.handle);
    }
    resource.handle = created;
    resource.capacityBytes = capacity;
    return true;
}

bool ClusteredLightingPass::ensureReadbackBuffers(RhiDevice& rhiDevice) {
    for (RhiBufferHandle& readback : m_statsReadbackBuffers) {
        if (readback.isValid()) {
            continue;
        }
        RhiBufferDesc desc;
        desc.debugName = "ClusteredLighting.StatsReadback";
        desc.size = sizeof(uint32_t) * kStatsWordCount;
        desc.usage = rhiFlag(RhiBufferUsage::TransferDst) |
                     rhiFlag(RhiBufferUsage::MapRead);
        desc.memoryUsage = RhiMemoryUsage::GpuToCpu;
        desc.initialState = RhiResourceState::TransferDst;
        desc.memoryCategory = RhiMemoryCategory::Readback;
        readback = rhiDevice.createBuffer(desc, nullptr, 0u);
        if (!readback.isValid()) {
            return false;
        }
    }
    return true;
}

bool ClusteredLightingPass::ensureBuildBindGroups(RhiDevice& rhiDevice) {
    if (m_countBindGroup.isValid() &&
        m_scanBindGroups.size() == m_scanLevels.size() &&
        m_scanAddBindGroups.size() + 1u == m_scanLevels.size() &&
        m_finalizeBindGroup.isValid() && m_fillBindGroup.isValid() &&
        m_validateBindGroup.isValid()) {
        return true;
    }
    destroyBuildBindGroups();

    RhiBindGroupDesc countDesc;
    countDesc.layout = m_countStage.bindGroupLayout;
    appendStorageBinding(countDesc, 0u, m_lightBoundsBuffer.handle,
                         m_lightBoundsBuffer.capacityBytes);
    appendStorageBinding(countDesc, 1u, m_countBuffer.handle,
                         m_countBuffer.capacityBytes);
    m_countBindGroup = rhiDevice.createBindGroup(countDesc);
    if (!m_countBindGroup.isValid()) {
        return false;
    }

    m_scanBindGroups.reserve(m_scanLevels.size());
    for (uint32_t levelIndex = 0u;
         levelIndex < m_scanLevels.size(); ++levelIndex) {
        RhiBindGroupDesc desc;
        desc.layout = m_scanStage.bindGroupLayout;
        if (levelIndex == 0u) {
            appendStorageBinding(desc, 0u, m_countBuffer.handle,
                                 m_countBuffer.capacityBytes);
            appendStorageBinding(desc, 1u, m_offsetBuffer.handle,
                                 m_offsetBuffer.capacityBytes);
        } else {
            appendStorageBinding(desc, 0u, m_scanScratchBuffer.handle,
                                 m_scanScratchBuffer.capacityBytes);
            appendStorageBinding(desc, 1u, m_scanScratchBuffer.handle,
                                 m_scanScratchBuffer.capacityBytes);
        }
        appendStorageBinding(desc, 2u, m_scanScratchBuffer.handle,
                             m_scanScratchBuffer.capacityBytes);
        const RhiBindGroupHandle bindGroup = rhiDevice.createBindGroup(desc);
        if (!bindGroup.isValid()) {
            destroyBuildBindGroups();
            return false;
        }
        m_scanBindGroups.push_back(bindGroup);
    }

    m_scanAddBindGroups.reserve(m_scanLevels.size() - 1u);
    for (uint32_t childLevel = 0u;
         childLevel + 1u < m_scanLevels.size(); ++childLevel) {
        RhiBindGroupDesc desc;
        desc.layout = m_scanAddStage.bindGroupLayout;
        const BufferResource& data = childLevel == 0u
            ? m_offsetBuffer : m_scanScratchBuffer;
        appendStorageBinding(desc, 0u, data.handle, data.capacityBytes);
        appendStorageBinding(desc, 1u, m_scanScratchBuffer.handle,
                             m_scanScratchBuffer.capacityBytes);
        const RhiBindGroupHandle bindGroup = rhiDevice.createBindGroup(desc);
        if (!bindGroup.isValid()) {
            destroyBuildBindGroups();
            return false;
        }
        m_scanAddBindGroups.push_back(bindGroup);
    }

    RhiBindGroupDesc finalizeDesc;
    finalizeDesc.layout = m_finalizeStage.bindGroupLayout;
    appendStorageBinding(finalizeDesc, 0u, m_countBuffer.handle,
                         m_countBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 1u, m_offsetBuffer.handle,
                         m_offsetBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 2u, m_recordBuffer.handle,
                         m_recordBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 3u, m_statsBuffer.handle,
                         m_statsBuffer.capacityBytes);
    m_finalizeBindGroup = rhiDevice.createBindGroup(finalizeDesc);

    RhiBindGroupDesc fillDesc;
    fillDesc.layout = m_fillStage.bindGroupLayout;
    appendStorageBinding(fillDesc, 0u, m_lightBoundsBuffer.handle,
                         m_lightBoundsBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 1u, m_recordBuffer.handle,
                         m_recordBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 2u, m_cursorBuffer.handle,
                         m_cursorBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 3u, m_compactIndexBuffer.handle,
                         m_compactIndexBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 4u, m_statsBuffer.handle,
                         m_statsBuffer.capacityBytes);
    m_fillBindGroup = rhiDevice.createBindGroup(fillDesc);

    RhiBindGroupDesc validateDesc;
    validateDesc.layout = m_validateStage.bindGroupLayout;
    appendStorageBinding(validateDesc, 0u, m_recordBuffer.handle,
                         m_recordBuffer.capacityBytes);
    appendStorageBinding(validateDesc, 1u, m_cursorBuffer.handle,
                         m_cursorBuffer.capacityBytes);
    appendStorageBinding(validateDesc, 2u, m_statsBuffer.handle,
                         m_statsBuffer.capacityBytes);
    m_validateBindGroup = rhiDevice.createBindGroup(validateDesc);
    if (!m_finalizeBindGroup.isValid() || !m_fillBindGroup.isValid() ||
        !m_validateBindGroup.isValid()) {
        destroyBuildBindGroups();
        return false;
    }
    return true;
}

bool ClusteredLightingPass::ensureConsumerBindGroup(RhiDevice& rhiDevice) {
    if (m_consumerBindGroup.isValid()) {
        return true;
    }
    RhiBindGroupDesc desc;
    desc.layout = m_consumerBindGroupLayout;
    appendStorageBinding(desc, 0u, m_lightBuffer.handle,
                         m_lightBuffer.capacityBytes);
    appendStorageBinding(desc, 1u, m_recordBuffer.handle,
                         m_recordBuffer.capacityBytes);
    appendStorageBinding(desc, 2u, m_compactIndexBuffer.handle,
                         m_compactIndexBuffer.capacityBytes);
    appendStorageBinding(desc, 3u, m_statsBuffer.handle,
                         m_statsBuffer.capacityBytes);
    appendStorageBinding(desc, 4u,
                         m_localShadowResources.metadataBuffer,
                         m_localShadowResources.metadataBufferBytes);
    appendCombinedTextureSamplerBinding(
        desc, 5u, m_localShadowResources.spotAtlasView,
        m_localShadowResources.sampler);
    appendCombinedTextureSamplerBinding(
        desc, 6u, m_localShadowResources.pointCubeArrayView,
        m_localShadowResources.sampler);
    m_consumerBindGroup = rhiDevice.createBindGroup(desc);
    return m_consumerBindGroup.isValid();
}

bool ClusteredLightingPass::importGraphResources(
    RenderGraph& graph, GraphResources& resources) const {
    if (!m_prepared || m_rhiDevice == nullptr) {
        return false;
    }
    return importBuffer(graph, m_lightBuffer, resources.lights) &&
           importBuffer(graph, m_lightBoundsBuffer, resources.lightBounds) &&
           importBuffer(graph, m_countBuffer, resources.counts) &&
           importBuffer(graph, m_offsetBuffer, resources.offsets) &&
           importBuffer(graph, m_recordBuffer, resources.records) &&
           importBuffer(graph, m_cursorBuffer, resources.cursors) &&
           importBuffer(graph, m_compactIndexBuffer,
                        resources.compactIndices) &&
           importBuffer(graph, m_scanScratchBuffer, resources.scanScratch) &&
           importBuffer(graph, m_statsBuffer, resources.stats);
}

bool ClusteredLightingPass::importBuffer(
    RenderGraph& graph,
    const BufferResource& resource,
    RgBufferHandle& graphBuffer) const {
    if (m_rhiDevice == nullptr || !resource.handle.isValid()) {
        return false;
    }
    RhiBufferDesc desc;
    if (!m_rhiDevice->getBufferDesc(resource.handle, desc)) {
        return false;
    }
    RgImportedBufferDesc imported;
    imported.name = desc.debugName;
    imported.buffer = resource.handle;
    imported.desc = desc;
    imported.initialState = RhiResourceState::StorageBuffer;
    imported.finalState = RhiResourceState::StorageBuffer;
    graphBuffer = graph.importBuffer(imported);
    return graphBuffer.isValid();
}

RgPassHandle ClusteredLightingPass::addGraphPasses(
    RenderGraph& graph,
    const GraphResources& resources,
    const RgPassHandle dependency) {
    if (!m_prepared || !dependency.isValid()) {
        return {};
    }

    RenderGraphPassBuilder upload = graph.addPass(
        {"ClusteredLighting.Upload", RgPassType::Copy,
         RhiQueueType::Graphics});
    upload.dependsOn(dependency)
        .writeBuffer(resources.lights, RhiResourceState::TransferDst)
        .writeBuffer(resources.lightBounds, RhiResourceState::TransferDst)
        .writeBuffer(resources.counts, RhiResourceState::TransferDst)
        .writeBuffer(resources.cursors, RhiResourceState::TransferDst)
        .writeBuffer(resources.stats, RhiResourceState::TransferDst)
        .setExecute([this](RgPassContext& pass) {
            return recordUpload(pass.commandList());
        });
    RgPassHandle tail = upload.handle();

    RenderGraphPassBuilder count = graph.addPass(
        {"ClusteredLighting.Count", RgPassType::Compute,
         RhiQueueType::Compute});
    count.dependsOn(tail)
        .readBuffer(resources.lightBounds, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.counts, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) {
            return recordCount(pass.commandList());
        });
    tail = count.handle();

    for (uint32_t level = 0u; level < m_scanLevels.size(); ++level) {
        const std::string passName =
            "ClusteredLighting.Scan." + std::to_string(level);
        RenderGraphPassBuilder scan = graph.addPass(
            {passName.c_str(), RgPassType::Compute, RhiQueueType::Compute});
        scan.dependsOn(tail);
        if (level == 0u) {
            scan.readBuffer(resources.counts, RhiResourceState::StorageBuffer)
                .writeBuffer(resources.offsets,
                             RhiResourceState::StorageBuffer)
                .writeBuffer(resources.scanScratch,
                             RhiResourceState::StorageBuffer);
        } else {
            scan.readWriteBuffer(resources.scanScratch,
                                 RhiResourceState::StorageBuffer);
        }
        scan.setExecute([this, level](RgPassContext& pass) {
            return recordScan(pass.commandList(), level);
        });
        tail = scan.handle();
    }

    for (uint32_t child = static_cast<uint32_t>(m_scanLevels.size() - 1u);
         child > 0u; --child) {
        const uint32_t childLevel = child - 1u;
        const std::string passName =
            "ClusteredLighting.ScanAdd." + std::to_string(childLevel);
        RenderGraphPassBuilder add = graph.addPass(
            {passName.c_str(), RgPassType::Compute, RhiQueueType::Compute});
        add.dependsOn(tail);
        if (childLevel == 0u) {
            add.readWriteBuffer(resources.offsets,
                                RhiResourceState::StorageBuffer)
                .readBuffer(resources.scanScratch,
                            RhiResourceState::StorageBuffer);
        } else {
            add.readWriteBuffer(resources.scanScratch,
                                RhiResourceState::StorageBuffer);
        }
        add.setExecute([this, childLevel](RgPassContext& pass) {
            return recordScanAdd(pass.commandList(), childLevel);
        });
        tail = add.handle();
    }

    RenderGraphPassBuilder finalize = graph.addPass(
        {"ClusteredLighting.Finalize", RgPassType::Compute,
         RhiQueueType::Compute});
    finalize.dependsOn(tail)
        .readBuffer(resources.counts, RhiResourceState::StorageBuffer)
        .readBuffer(resources.offsets, RhiResourceState::StorageBuffer)
        .writeBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) {
            return recordFinalize(pass.commandList());
        });
    tail = finalize.handle();

    RenderGraphPassBuilder fill = graph.addPass(
        {"ClusteredLighting.Fill", RgPassType::Compute,
         RhiQueueType::Compute});
    fill.dependsOn(tail)
        .readBuffer(resources.lightBounds, RhiResourceState::StorageBuffer)
        .readBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.cursors, RhiResourceState::StorageBuffer)
        .writeBuffer(resources.compactIndices,
                     RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) {
            return recordFill(pass.commandList());
        });
    tail = fill.handle();

    RenderGraphPassBuilder validate = graph.addPass(
        {"ClusteredLighting.Validate", RgPassType::Compute,
         RhiQueueType::Compute});
    validate.dependsOn(tail)
        .readBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readBuffer(resources.cursors, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) {
            return recordValidateAndReadback(pass.commandList());
        });
    return validate.handle();
}

bool ClusteredLightingPass::recordUpload(RhiCommandList& commandList) const {
    if (!m_lights.empty()) {
        commandList.updateBuffer(m_lightBuffer.handle, 0u, m_lights.data(),
                                 m_lights.size() * sizeof(m_lights.front()));
        commandList.updateBuffer(
            m_lightBoundsBuffer.handle, 0u, m_lightBounds.data(),
            m_lightBounds.size() * sizeof(m_lightBounds.front()));
    }
    commandList.updateBuffer(
        m_countBuffer.handle, 0u, m_zeroClusterWords.data(),
        m_zeroClusterWords.size() * sizeof(m_zeroClusterWords.front()));
    commandList.updateBuffer(
        m_cursorBuffer.handle, 0u, m_zeroClusterWords.data(),
        m_zeroClusterWords.size() * sizeof(m_zeroClusterWords.front()));
    const uint32_t stats[kStatsWordCount] = {
        0u, 0u, 0u, 0u, m_grid.clusterCount,
        static_cast<uint32_t>(m_lights.size()), m_indexCapacity,
        renderer::contracts::kGpuLightContractVersion};
    commandList.updateBuffer(m_statsBuffer.handle, 0u, stats, sizeof(stats));
    return true;
}

bool ClusteredLightingPass::recordCount(RhiCommandList& commandList) const {
    if (m_lights.empty()) {
        return true;
    }
    ClusterGridPushConstants push;
    push.gridAndLightCount = {
        m_grid.tileCountX, m_grid.tileCountY, m_grid.depthSliceCount,
        static_cast<uint32_t>(m_lights.size())};
    commandList.setComputePipeline(m_countStage.pipeline);
    commandList.setBindGroup(0u, m_countBindGroup);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(
        (static_cast<uint32_t>(m_lights.size()) + 63u) / 64u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordScan(RhiCommandList& commandList,
                                        const uint32_t level) const {
    if (level >= m_scanLevels.size()) {
        return false;
    }
    const ScanLevel& scanLevel = m_scanLevels[level];
    ClusterScanPushConstants push;
    push.offsetsAndCount = {
        scanLevel.inputOffsetWords, scanLevel.outputOffsetWords,
        scanLevel.blockSumOffsetWords, scanLevel.elementCount};
    commandList.setComputePipeline(m_scanStage.pipeline);
    commandList.setBindGroup(0u, m_scanBindGroups[level]);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(scanLevel.groupCount, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordScanAdd(
    RhiCommandList& commandList,
    const uint32_t childLevel) const {
    if (childLevel + 1u >= m_scanLevels.size() ||
        childLevel >= m_scanAddBindGroups.size()) {
        return false;
    }
    const ScanLevel& child = m_scanLevels[childLevel];
    const ScanLevel& parent = m_scanLevels[childLevel + 1u];
    ClusterScanPushConstants push;
    push.offsetsAndCount = {
        child.outputOffsetWords, parent.outputOffsetWords,
        child.elementCount,
        renderer::contracts::kClusterScanElementsPerWorkgroup};
    commandList.setComputePipeline(m_scanAddStage.pipeline);
    commandList.setBindGroup(0u, m_scanAddBindGroups[childLevel]);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((child.elementCount + 255u) / 256u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordFinalize(RhiCommandList& commandList) const {
    ClusterScanPushConstants push;
    push.offsetsAndCount = {
        m_grid.clusterCount, m_indexCapacity,
        static_cast<uint32_t>(m_lights.size()), 0u};
    commandList.setComputePipeline(m_finalizeStage.pipeline);
    commandList.setBindGroup(0u, m_finalizeBindGroup);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((m_grid.clusterCount + 255u) / 256u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordFill(RhiCommandList& commandList) const {
    if (m_lights.empty()) {
        return true;
    }
    ClusterFillPushConstants push;
    push.gridAndLightCount = {
        m_grid.tileCountX, m_grid.tileCountY, m_grid.depthSliceCount,
        static_cast<uint32_t>(m_lights.size())};
    push.capacity = {m_indexCapacity, 0u, 0u, 0u};
    commandList.setComputePipeline(m_fillStage.pipeline);
    commandList.setBindGroup(0u, m_fillBindGroup);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(
        (static_cast<uint32_t>(m_lights.size()) + 63u) / 64u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordValidateAndReadback(
    RhiCommandList& commandList) {
    ClusterScanPushConstants push;
    push.offsetsAndCount = {m_grid.clusterCount, m_indexCapacity, 0u, 0u};
    commandList.setComputePipeline(m_validateStage.pipeline);
    commandList.setBindGroup(0u, m_validateBindGroup);
    commandList.pushConstants(&push, sizeof(push),
                              rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((m_grid.clusterCount + 255u) / 256u, 1u, 1u);
    if (!m_statsReadbackSlotAvailable) {
        return true;
    }

    const uint32_t ringIndex = m_statsReadbackWriteIndex;
    commandList.bufferBarrier({m_statsBuffer.handle,
                               RhiResourceState::StorageBuffer,
                               RhiResourceState::TransferSrc});
    if (m_statsReadbackWritten[ringIndex]) {
        commandList.bufferBarrier({m_statsReadbackBuffers[ringIndex],
                                   RhiResourceState::HostRead,
                                   RhiResourceState::TransferDst});
    }
    RhiBufferCopy copy;
    copy.src = m_statsBuffer.handle;
    copy.dst = m_statsReadbackBuffers[ringIndex];
    copy.size = sizeof(uint32_t) * kStatsWordCount;
    commandList.copyBuffer(copy);
    commandList.bufferBarrier({m_statsReadbackBuffers[ringIndex],
                               RhiResourceState::TransferDst,
                               RhiResourceState::HostRead});
    commandList.bufferBarrier({m_statsBuffer.handle,
                               RhiResourceState::TransferSrc,
                               RhiResourceState::StorageBuffer});
    m_pendingStatsReadbackIndex = ringIndex;
    m_statsReadbackPending = true;
    return true;
}

void ClusteredLightingPass::finishGraphExecution(
    const bool succeeded,
    const RhiSubmissionToken completionToken) {
    if (!m_statsReadbackPending) {
        return;
    }
    if (succeeded && completionToken.isValid()) {
        m_statsReadbackWritten[m_pendingStatsReadbackIndex] = true;
        m_statsReadbackTokens[m_pendingStatsReadbackIndex] =
            completionToken;
        m_statsReadbackWriteIndex =
            (m_pendingStatsReadbackIndex + 1u) % kStatsReadbackRingSize;
    } else if (succeeded) {
        m_gpuBuildFailed = true;
        MECRAFT_LOG_STREAM(
            std::cerr << "[ClusteredLightingPass] Stats readback submission token is invalid\n");
    }
    m_statsReadbackPending = false;
}

void ClusteredLightingPass::destroyBuildBindGroups() {
    if (m_rhiDevice != nullptr) {
        const RhiBindGroupHandle fixed[] = {
            m_countBindGroup, m_finalizeBindGroup, m_fillBindGroup,
            m_validateBindGroup, m_consumerBindGroup};
        for (const RhiBindGroupHandle bindGroup : fixed) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
        for (const RhiBindGroupHandle bindGroup : m_scanBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
        for (const RhiBindGroupHandle bindGroup : m_scanAddBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
    }
    m_countBindGroup = {};
    m_scanBindGroups.clear();
    m_scanAddBindGroups.clear();
    m_finalizeBindGroup = {};
    m_fillBindGroup = {};
    m_validateBindGroup = {};
    m_consumerBindGroup = {};
}

void ClusteredLightingPass::destroyComputeStage(ComputeStage& stage) {
    if (m_rhiDevice != nullptr) {
        if (stage.pipeline.isValid()) {
            m_rhiDevice->destroyPipeline(stage.pipeline);
        }
        if (stage.pipelineLayout.isValid()) {
            m_rhiDevice->destroyPipelineLayout(stage.pipelineLayout);
        }
        if (stage.bindGroupLayout.isValid()) {
            m_rhiDevice->destroyBindGroupLayout(stage.bindGroupLayout);
        }
        if (stage.shader.isValid()) {
            m_rhiDevice->destroyShader(stage.shader);
        }
    }
    stage = {};
}

void ClusteredLightingPass::destroyPipelines() {
    destroyComputeStage(m_countStage);
    destroyComputeStage(m_scanStage);
    destroyComputeStage(m_scanAddStage);
    destroyComputeStage(m_finalizeStage);
    destroyComputeStage(m_fillStage);
    destroyComputeStage(m_validateStage);
    if (m_rhiDevice != nullptr && m_consumerBindGroupLayout.isValid()) {
        m_rhiDevice->destroyBindGroupLayout(m_consumerBindGroupLayout);
    }
    m_consumerBindGroupLayout = {};
}

void ClusteredLightingPass::destroyBuffers() {
    if (m_rhiDevice != nullptr) {
        BufferResource* resources[] = {
            &m_lightBuffer, &m_lightBoundsBuffer, &m_countBuffer,
            &m_offsetBuffer, &m_recordBuffer, &m_cursorBuffer,
            &m_compactIndexBuffer, &m_scanScratchBuffer, &m_statsBuffer};
        for (BufferResource* resource : resources) {
            if (resource->handle.isValid()) {
                m_rhiDevice->destroyBuffer(resource->handle);
            }
            *resource = {};
        }
        for (RhiBufferHandle& readback : m_statsReadbackBuffers) {
            if (readback.isValid()) {
                m_rhiDevice->destroyBuffer(readback);
            }
            readback = {};
        }
    }
    m_statsReadbackWritten.fill(false);
    m_statsReadbackTokens.fill({});
    m_statsReadbackWriteIndex = 0u;
    m_pendingStatsReadbackIndex = 0u;
    m_statsReadbackSlotAvailable = true;
    m_statsReadbackPending = false;
}

void ClusteredLightingPass::shutdown() {
    destroyBuildBindGroups();
    destroyPipelines();
    destroyBuffers();
    m_rhiDevice = nullptr;
    m_prepared = false;
    m_gpuBuildFailed = false;
    m_lightBounds.clear();
    m_zeroClusterWords.clear();
    m_scanLevels.clear();
    m_grid = {};
    m_requiredIndexCount = 0u;
    m_indexCapacity = 0u;
    m_scanScratchWordCount = 0u;
    m_frameStats = {};
    m_localShadowResources = {};
}
