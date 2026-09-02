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
    glm::mat4 inverseProjection{1.0f};
    glm::uvec4 gridAndLightCount{0u};
    glm::vec4 depthParameters{0.0f};
};

struct alignas(16) ClusterScanPushConstants final {
    glm::uvec4 offsetsAndCount{0u};
};

struct alignas(16) ClusterFillPushConstants final {
    glm::mat4 inverseProjection{1.0f};
    glm::uvec4 gridAndLightCount{0u};
    glm::vec4 depthParameters{0.0f};
    glm::uvec4 capacity{0u};
};

static_assert(sizeof(ClusterGridPushConstants) == 96u);
static_assert(sizeof(ClusterScanPushConstants) == 16u);
static_assert(sizeof(ClusterFillPushConstants) == 112u);

[[nodiscard]] bool finite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool finite(const glm::mat4& value) {
    return finite(value[0]) && finite(value[1]) && finite(value[2]) && finite(value[3]);
}

[[nodiscard]] bool sameClusterGrid(const renderer::contracts::ClusterGrid& lhs,
                                   const renderer::contracts::ClusterGrid& rhs) {
    return lhs.renderWidth == rhs.renderWidth && lhs.renderHeight == rhs.renderHeight &&
           lhs.tileCountX == rhs.tileCountX && lhs.tileCountY == rhs.tileCountY &&
           lhs.depthSliceCount == rhs.depthSliceCount && lhs.clusterCount == rhs.clusterCount &&
           lhs.nearPlane == rhs.nearPlane && lhs.farPlane == rhs.farPlane && lhs.depthLogScale == rhs.depthLogScale &&
           lhs.depthLogBias == rhs.depthLogBias;
}

[[nodiscard]] uint64_t alignBufferSize(const uint64_t size) {
    constexpr uint64_t kAlignment = 256u;
    if (size > std::numeric_limits<uint64_t>::max() - (kAlignment - 1u)) {
        return 0u;
    }
    return (size + kAlignment - 1u) & ~(kAlignment - 1u);
}

[[nodiscard]] bool multiplyBytes(const uint64_t count, const uint64_t stride, uint64_t& bytes) {
    if (count == 0u || stride == 0u || count > std::numeric_limits<uint64_t>::max() / stride) {
        return false;
    }
    bytes = count * stride;
    return true;
}

void appendStorageBinding(RhiBindGroupDesc& desc, const uint32_t binding, const RhiBufferHandle buffer,
                          const uint64_t range) {
    RhiBindGroupEntry entry;
    entry.binding = binding;
    entry.resource.buffer.buffer = buffer;
    entry.resource.buffer.offset = 0u;
    entry.resource.buffer.range = range;
    desc.entries.push_back(entry);
}

void appendCombinedTextureSamplerBinding(RhiBindGroupDesc& desc, const uint32_t binding,
                                         const RhiTextureViewHandle textureView, const RhiSamplerHandle sampler) {
    RhiBindGroupEntry entry;
    entry.binding = binding;
    entry.resource.combinedTextureSampler.textureView = textureView;
    entry.resource.combinedTextureSampler.sampler = sampler;
    desc.entries.push_back(entry);
}

} // namespace

bool ClusteredLightingPass::setLights(std::vector<renderer::contracts::GpuLight> lights) {
    m_lights = std::move(lights);
    m_inputValid = validateLights();
    if (!m_inputValid) {
        m_lights.clear();
        m_worldLightGrid = {};
        m_prepared = false;
        m_emptyBuildReady = false;
        return false;
    }
    if (!m_lights.empty()) {
        m_emptyBuildReady = false;
    }
    m_prepared = false;
    return true;
}

bool ClusteredLightingPass::setLocalShadowResources(const LocalShadowResources& resources) {
    if (!resources.metadataBuffer.isValid() || resources.metadataBufferBytes == 0u ||
        !resources.spotAtlasView.isValid() || !resources.pointCubeArrayView.isValid() || !resources.sampler.isValid()) {
        return false;
    }
    const bool changed =
        resources.metadataBuffer.index != m_localShadowResources.metadataBuffer.index ||
        resources.metadataBuffer.generation != m_localShadowResources.metadataBuffer.generation ||
        resources.metadataBufferBytes != m_localShadowResources.metadataBufferBytes ||
        resources.spotAtlasView.index != m_localShadowResources.spotAtlasView.index ||
        resources.spotAtlasView.generation != m_localShadowResources.spotAtlasView.generation ||
        resources.pointCubeArrayView.index != m_localShadowResources.pointCubeArrayView.index ||
        resources.pointCubeArrayView.generation != m_localShadowResources.pointCubeArrayView.generation ||
        resources.sampler.index != m_localShadowResources.sampler.index ||
        resources.sampler.generation != m_localShadowResources.sampler.generation;
    if (changed && m_rhiDevice != nullptr) {
        for (BuildSlot& slot : m_buildSlots) {
            if (slot.consumerBindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(slot.consumerBindGroup);
                slot.consumerBindGroup = {};
            }
        }
    }
    m_localShadowResources = resources;
    return true;
}

bool ClusteredLightingPass::validateLights() const {
    using namespace renderer::contracts;
    if (m_lights.size() > kClusterMaxLightCount) {
        return false;
    }
    std::unordered_set<uint32_t> stableIds;
    stableIds.reserve(m_lights.size());
    for (const GpuLight& light : m_lights) {
        const uint32_t type = light.classificationAndIdentity.x;
        const uint32_t stableId = light.classificationAndIdentity.y;
        const uint32_t shadowPolicy = light.classificationAndIdentity.z;
        const uint32_t shadowIndex = light.classificationAndIdentity.w;
        if (type > static_cast<uint32_t>(GpuLightType::Rect) || stableId == 0u ||
            shadowPolicy > static_cast<uint32_t>(GpuLightShadowPolicy::RasterCached) ||
            light.resourcesAndFlags.w != kGpuLightContractVersion ||
            (light.resourcesAndFlags.z & ~kGpuLightKnownContributionFlags) != 0u || !gpuLightPackedRangeValid(light) ||
            !finite(light.positionAndRange) || !finite(light.direction) || !finite(light.colorAndIntensity) ||
            !finite(light.spotCosinesAndRectSize) || !stableIds.insert(stableId).second) {
            return false;
        }
        const bool noShadow = shadowPolicy == static_cast<uint32_t>(GpuLightShadowPolicy::None);
        if (noShadow != (shadowIndex == kGpuLightInvalidResourceIndex)) {
            return false;
        }
        if (!noShadow) {
            if (type == static_cast<uint32_t>(GpuLightType::Spot)) {
                if (shadowIndex >= kLocalShadowMaxSpotLightCount) {
                    return false;
                }
            } else if (type == static_cast<uint32_t>(GpuLightType::Point)) {
                if (shadowIndex < kLocalShadowPointMetadataBase || shadowIndex >= kLocalShadowMetadataCount) {
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    return true;
}

bool ClusteredLightingPass::prepareGraphFrame(RhiDevice& rhiDevice, const FrameContext& ctx, const uint32_t renderWidth,
                                              const uint32_t renderHeight) {
    if (rhiDevice.backend() != RhiBackend::Vulkan || !m_inputValid || m_gpuBuildFailed ||
        !m_localShadowResources.metadataBuffer.isValid() || m_localShadowResources.metadataBufferBytes == 0u ||
        !m_localShadowResources.spotAtlasView.isValid() || !m_localShadowResources.pointCubeArrayView.isValid() ||
        !m_localShadowResources.sampler.isValid()) {
        return false;
    }
    if (m_rhiDevice != nullptr && m_rhiDevice != &rhiDevice) {
        shutdown();
    }
    m_rhiDevice = &rhiDevice;
    m_prepared = false;
    // Build resources rotate across ring slots. The slot selected here was
    // last used kBuildSlotCount frames ago; that submission is normally
    // already complete because the swapchain frame-slot fence was waited on
    // during acquire, so this wait no longer serializes the CPU against the
    // previous frame's GPU execution. The wait is still required for
    // correctness: it guards uploads, zeroes and resizes against the last
    // frame that touched this slot's buffers.
    m_activeSlot = (m_activeSlot + 1u) % kBuildSlotCount;
    BuildSlot& slot = m_buildSlots[m_activeSlot];
    if (slot.lastUseToken.isValid() && !rhiDevice.waitForSubmission(slot.lastUseToken)) {
        return false;
    }
    slot.lastUseToken = {};
    if (!consumeReadback(rhiDevice) || !validateLights() || !buildCoverage(ctx, renderWidth, renderHeight) ||
        !buildScanPlan() || !ensurePipelines(rhiDevice) || !ensureBuffers(rhiDevice, slot) ||
        !ensureBuildBindGroups(rhiDevice, slot) || !ensureConsumerBindGroup(rhiDevice, slot)) {
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
        MECRAFT_LOG_STREAM(std::cerr << "[ClusteredLightingPass] Stats submission query failed\n");
        return false;
    }
    if (!complete) {
        m_statsReadbackSlotAvailable = false;
        return true;
    }
    const void* mapped = rhiDevice.mapBuffer(m_statsReadbackBuffers[ringIndex], 0u, sizeof(uint32_t) * kStatsWordCount);
    if (mapped == nullptr) {
        MECRAFT_LOG_STREAM(std::cerr << "[ClusteredLightingPass] Stats readback mapping failed\n");
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
    applyWorldLightGridStats(m_statsReadbackWorldSnapshots[ringIndex]);
    m_frameStats.averageLightsPerCluster =
        m_frameStats.clusterCount != 0u
            ? static_cast<float>(m_frameStats.totalIndexCount) / static_cast<float>(m_frameStats.clusterCount)
            : 0.0f;
    if (words[kStatsContractVersion] != renderer::contracts::kGpuLightContractVersion ||
        m_frameStats.buildError != 0u) {
        m_gpuBuildFailed = true;
        MECRAFT_LOG_STREAM(std::cerr << "[ClusteredLightingPass] GPU build invariant failed: error="
                                     << m_frameStats.buildError << " totalIndices=" << m_frameStats.totalIndexCount
                                     << " capacity=" << m_frameStats.indexCapacity
                                     << " clusters=" << m_frameStats.clusterCount
                                     << " lights=" << m_frameStats.lightCount
                                     << " maxLightsPerCluster=" << m_frameStats.maxLightsPerCluster
                                     << " nonEmptyClusters=" << m_frameStats.nonEmptyClusterCount << '\n');
        return false;
    }
    return true;
}

bool ClusteredLightingPass::buildCoverage(const FrameContext& ctx, const uint32_t renderWidth,
                                          const uint32_t renderHeight) {
    using namespace renderer::contracts;
    const std::optional<ClusterGrid> grid =
        buildClusterGrid(renderWidth, renderHeight, ctx.camera.nearPlane, ctx.camera.farPlane);
    if (!grid.has_value()) {
        return false;
    }
    if (!sameClusterGrid(m_grid, *grid)) {
        m_emptyBuildReady = false;
    }
    m_grid = *grid;
    m_inverseProjection = glm::inverse(ctx.camera.projection);
    if (!finite(m_inverseProjection)) {
        return false;
    }
    m_worldLightGrid = buildWorldLightGrid(m_lights);
    if (!m_worldLightGrid.succeeded()) {
        MECRAFT_LOG_STREAM(std::cerr << "[ClusteredLightingPass] World light grid build failed: "
                                     << worldLightGridBuildErrorStableId(m_worldLightGrid.error) << '\n');
        return false;
    }
    m_lightBounds.clear();
    m_lightBounds.reserve(m_lights.size());
    for (uint32_t lightIndex = 0u; lightIndex < static_cast<uint32_t>(m_lights.size()); ++lightIndex) {
        const GpuLight& light = m_lights[lightIndex];
        const std::optional<GpuClusterLightBounds> bounds =
            buildGpuClusterLightBounds(light, m_grid, ctx.camera.view, ctx.camera.projection);
        if (!bounds.has_value()) {
            return false;
        }
        if (bounds->minCluster.w == 0u) {
            continue;
        }
        GpuClusterLightBounds activeBounds = *bounds;
        activeBounds.maxCluster.w = lightIndex;
        m_lightBounds.push_back(activeBounds);
    }
    const std::optional<uint32_t> required = requiredClusterLightIndexCount(m_lightBounds);
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
    first.groupCount = (first.elementCount + kClusterScanElementsPerWorkgroup - 1u) / kClusterScanElementsPerWorkgroup;
    first.blockSumOffsetWords = static_cast<uint32_t>(scratchCursor);
    scratchCursor += first.groupCount;
    m_scanLevels.push_back(first);

    while (m_scanLevels.back().groupCount > 1u) {
        const ScanLevel& previous = m_scanLevels.back();
        ScanLevel level;
        level.elementCount = previous.groupCount;
        level.groupCount =
            (level.elementCount + kClusterScanElementsPerWorkgroup - 1u) / kClusterScanElementsPerWorkgroup;
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
    if (m_countStage.pipeline.isValid() && m_scanStage.pipeline.isValid() && m_scanScratchStage.pipeline.isValid() &&
        m_scanAddStage.pipeline.isValid() && m_finalizeStage.pipeline.isValid() && m_fillStage.pipeline.isValid() &&
        m_validateStage.pipeline.isValid() && m_consumerBindGroupLayout.isValid()) {
        return true;
    }
    destroyPipelines();

    RhiBindGroupLayoutDesc consumerLayoutDesc;
    consumerLayoutDesc.debugName = "ClusteredLighting.ConsumerLayout";
    const RhiShaderStageFlags consumerStages = rhiFlag(RhiShaderStage::Fragment) | rhiFlag(RhiShaderStage::Compute);
    for (uint32_t binding = 0u; binding < 5u; ++binding) {
        consumerLayoutDesc.entries.push_back({binding, RhiBindingType::StorageBuffer, consumerStages, 1u});
    }
    consumerLayoutDesc.entries.push_back({5u, RhiBindingType::CombinedTextureSampler, consumerStages, 1u});
    consumerLayoutDesc.entries.push_back({6u, RhiBindingType::CombinedTextureSampler, consumerStages, 1u});
    for (uint32_t binding = 7u; binding <= 9u; ++binding) {
        consumerLayoutDesc.entries.push_back({binding, RhiBindingType::StorageBuffer, consumerStages, 1u});
    }
    m_consumerBindGroupLayout = rhiDevice.createBindGroupLayout(consumerLayoutDesc);
    if (!m_consumerBindGroupLayout.isValid()) {
        return false;
    }

    const auto createStage = [&](ComputeStage& stage, const char* shaderPath, const char* debugName,
                                 const uint32_t bindingCount, const uint32_t pushConstantBytes) {
        const std::optional<std::string> source = renderer::rhi::loadShaderSource(shaderPath);
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
            bindGroupLayoutDesc.entries.push_back(
                {binding, RhiBindingType::StorageBuffer, rhiFlag(RhiShaderStage::Compute), 1u});
        }
        stage.bindGroupLayout = rhiDevice.createBindGroupLayout(bindGroupLayoutDesc);
        if (!stage.bindGroupLayout.isValid()) {
            return false;
        }
        RhiPipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.debugName = debugName;
        pipelineLayoutDesc.bindGroupLayouts.push_back(stage.bindGroupLayout);
        pipelineLayoutDesc.pushConstantBytes = pushConstantBytes;
        pipelineLayoutDesc.pushConstantStages = rhiFlag(RhiShaderStage::Compute);
        stage.pipelineLayout = rhiDevice.createPipelineLayout(pipelineLayoutDesc);
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

    if (!createStage(m_countStage, "assets/shaders/cluster_count.comp", "ClusteredLighting.Count", 2u,
                     sizeof(ClusterGridPushConstants)) ||
        !createStage(m_scanStage, "assets/shaders/cluster_scan.comp", "ClusteredLighting.Scan", 3u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_scanScratchStage, "assets/shaders/cluster_scan_scratch.comp", "ClusteredLighting.ScanScratch",
                     1u, sizeof(ClusterScanPushConstants)) ||
        !createStage(m_scanAddStage, "assets/shaders/cluster_scan_add.comp", "ClusteredLighting.ScanAdd", 2u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_finalizeStage, "assets/shaders/cluster_finalize.comp", "ClusteredLighting.Finalize", 4u,
                     sizeof(ClusterScanPushConstants)) ||
        !createStage(m_fillStage, "assets/shaders/cluster_fill.comp", "ClusteredLighting.Fill", 5u,
                     sizeof(ClusterFillPushConstants)) ||
        !createStage(m_validateStage, "assets/shaders/cluster_validate.comp", "ClusteredLighting.Validate", 3u,
                     sizeof(ClusterScanPushConstants))) {
        destroyPipelines();
        return false;
    }
    return true;
}

bool ClusteredLightingPass::ensureBuffers(RhiDevice& rhiDevice, BuildSlot& slot) {
    uint64_t lightBytes = 0u;
    uint64_t boundsBytes = 0u;
    uint64_t clusterWordBytes = 0u;
    uint64_t recordBytes = 0u;
    uint64_t indexBytes = 0u;
    uint64_t scratchBytes = 0u;
    uint64_t worldCellBytes = 0u;
    uint64_t worldIndexBytes = 0u;
    if (!multiplyBytes(std::max<size_t>(m_lights.size(), 1u), sizeof(renderer::contracts::GpuLight), lightBytes) ||
        !multiplyBytes(std::max<size_t>(m_lightBounds.size(), 1u), sizeof(renderer::contracts::GpuClusterLightBounds),
                       boundsBytes) ||
        !multiplyBytes(m_grid.clusterCount, sizeof(uint32_t), clusterWordBytes) ||
        !multiplyBytes(m_grid.clusterCount, sizeof(uint32_t) * 2u, recordBytes) ||
        !multiplyBytes(std::max(m_requiredIndexCount, 1u), sizeof(uint32_t), indexBytes) ||
        !multiplyBytes(std::max(m_scanScratchWordCount, 1u), sizeof(uint32_t), scratchBytes) ||
        !multiplyBytes(std::max<size_t>(m_worldLightGrid.cells.size(), 1u),
                       sizeof(renderer::contracts::GpuWorldLightCell), worldCellBytes) ||
        !multiplyBytes(std::max<size_t>(m_worldLightGrid.lightIndices.size(), 1u), sizeof(uint32_t), worldIndexBytes)) {
        return false;
    }

    const bool buffersGrow =
        lightBytes > slot.lightBuffer.capacityBytes || boundsBytes > slot.lightBoundsBuffer.capacityBytes ||
        clusterWordBytes > slot.countBuffer.capacityBytes || clusterWordBytes > slot.offsetBuffer.capacityBytes ||
        recordBytes > slot.recordBuffer.capacityBytes || clusterWordBytes > slot.cursorBuffer.capacityBytes ||
        indexBytes > slot.compactIndexBuffer.capacityBytes || scratchBytes > slot.scanScratchBuffer.capacityBytes ||
        sizeof(uint32_t) * kStatsWordCount > slot.statsBuffer.capacityBytes ||
        worldCellBytes > slot.worldCellBuffer.capacityBytes || worldIndexBytes > slot.worldIndexBuffer.capacityBytes ||
        sizeof(renderer::contracts::GpuWorldLightGridHeader) > slot.worldHeaderBuffer.capacityBytes;
    if (buffersGrow) {
        m_emptyBuildReady = false;
        destroyBuildBindGroups(slot);
    }

    const RhiBufferUsageFlags storageUploadUsage =
        rhiFlag(RhiBufferUsage::Storage) | rhiFlag(RhiBufferUsage::TransferDst);
    const RhiBufferUsageFlags storageUsage = rhiFlag(RhiBufferUsage::Storage);
    if (!ensureBuffer(rhiDevice, slot.lightBuffer, lightBytes, storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Lights") ||
        !ensureBuffer(rhiDevice, slot.lightBoundsBuffer, boundsBytes, storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.LightBounds") ||
        !ensureBuffer(rhiDevice, slot.countBuffer, clusterWordBytes, storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Counts") ||
        !ensureBuffer(rhiDevice, slot.offsetBuffer, clusterWordBytes, storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Offsets") ||
        !ensureBuffer(rhiDevice, slot.recordBuffer, recordBytes, storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Records") ||
        !ensureBuffer(rhiDevice, slot.cursorBuffer, clusterWordBytes, storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Cursors") ||
        !ensureBuffer(rhiDevice, slot.compactIndexBuffer, indexBytes, storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.CompactIndices") ||
        !ensureBuffer(rhiDevice, slot.scanScratchBuffer, scratchBytes, storageUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.ScanScratch") ||
        !ensureBuffer(rhiDevice, slot.statsBuffer, sizeof(uint32_t) * kStatsWordCount,
                      storageUploadUsage | rhiFlag(RhiBufferUsage::TransferSrc), RhiMemoryCategory::SceneData,
                      "ClusteredLighting.Stats") ||
        !ensureBuffer(rhiDevice, slot.worldCellBuffer, worldCellBytes, storageUploadUsage, RhiMemoryCategory::SceneData,
                      "ClusteredLighting.WorldCells") ||
        !ensureBuffer(rhiDevice, slot.worldIndexBuffer, worldIndexBytes, storageUploadUsage,
                      RhiMemoryCategory::SceneData, "ClusteredLighting.WorldIndices") ||
        !ensureBuffer(rhiDevice, slot.worldHeaderBuffer, sizeof(renderer::contracts::GpuWorldLightGridHeader),
                      storageUploadUsage, RhiMemoryCategory::SceneData, "ClusteredLighting.WorldHeader") ||
        !ensureReadbackBuffers(rhiDevice)) {
        return false;
    }
    m_indexCapacity = static_cast<uint32_t>(
        std::min<uint64_t>(slot.compactIndexBuffer.capacityBytes / sizeof(uint32_t), std::numeric_limits<uint32_t>::max()));
    return m_indexCapacity >= std::max(m_requiredIndexCount, 1u);
}

bool ClusteredLightingPass::ensureBuffer(RhiDevice& rhiDevice, BufferResource& resource, const uint64_t requiredBytes,
                                         const RhiBufferUsageFlags usage, const RhiMemoryCategory memoryCategory,
                                         const char* debugName) {
    if (requiredBytes == 0u) {
        return false;
    }
    if (resource.handle.isValid() && resource.capacityBytes >= requiredBytes) {
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
    const RhiBufferHandle created = rhiDevice.createBuffer(desc, nullptr, 0u);
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
        desc.usage = rhiFlag(RhiBufferUsage::TransferDst) | rhiFlag(RhiBufferUsage::MapRead);
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

bool ClusteredLightingPass::ensureBuildBindGroups(RhiDevice& rhiDevice, BuildSlot& slot) {
    if (slot.countBindGroup.isValid() && slot.scanBindGroups.size() == m_scanLevels.size() &&
        slot.scanAddBindGroups.size() + 1u == m_scanLevels.size() && slot.finalizeBindGroup.isValid() &&
        slot.fillBindGroup.isValid() && slot.validateBindGroup.isValid()) {
        return true;
    }
    destroyBuildBindGroups(slot);

    RhiBindGroupDesc countDesc;
    countDesc.layout = m_countStage.bindGroupLayout;
    appendStorageBinding(countDesc, 0u, slot.lightBoundsBuffer.handle, slot.lightBoundsBuffer.capacityBytes);
    appendStorageBinding(countDesc, 1u, slot.countBuffer.handle, slot.countBuffer.capacityBytes);
    slot.countBindGroup = rhiDevice.createBindGroup(countDesc);
    if (!slot.countBindGroup.isValid()) {
        return false;
    }

    slot.scanBindGroups.reserve(m_scanLevels.size());
    for (uint32_t levelIndex = 0u; levelIndex < m_scanLevels.size(); ++levelIndex) {
        RhiBindGroupDesc desc;
        desc.layout = levelIndex == 0u ? m_scanStage.bindGroupLayout : m_scanScratchStage.bindGroupLayout;
        if (levelIndex == 0u) {
            appendStorageBinding(desc, 0u, slot.countBuffer.handle, slot.countBuffer.capacityBytes);
            appendStorageBinding(desc, 1u, slot.offsetBuffer.handle, slot.offsetBuffer.capacityBytes);
            appendStorageBinding(desc, 2u, slot.scanScratchBuffer.handle, slot.scanScratchBuffer.capacityBytes);
        } else {
            appendStorageBinding(desc, 0u, slot.scanScratchBuffer.handle, slot.scanScratchBuffer.capacityBytes);
        }
        const RhiBindGroupHandle bindGroup = rhiDevice.createBindGroup(desc);
        if (!bindGroup.isValid()) {
            destroyBuildBindGroups(slot);
            return false;
        }
        slot.scanBindGroups.push_back(bindGroup);
    }

    slot.scanAddBindGroups.reserve(m_scanLevels.size() - 1u);
    for (uint32_t childLevel = 0u; childLevel + 1u < m_scanLevels.size(); ++childLevel) {
        RhiBindGroupDesc desc;
        desc.layout = m_scanAddStage.bindGroupLayout;
        const BufferResource& data = childLevel == 0u ? slot.offsetBuffer : slot.scanScratchBuffer;
        appendStorageBinding(desc, 0u, data.handle, data.capacityBytes);
        appendStorageBinding(desc, 1u, slot.scanScratchBuffer.handle, slot.scanScratchBuffer.capacityBytes);
        const RhiBindGroupHandle bindGroup = rhiDevice.createBindGroup(desc);
        if (!bindGroup.isValid()) {
            destroyBuildBindGroups(slot);
            return false;
        }
        slot.scanAddBindGroups.push_back(bindGroup);
    }

    RhiBindGroupDesc finalizeDesc;
    finalizeDesc.layout = m_finalizeStage.bindGroupLayout;
    appendStorageBinding(finalizeDesc, 0u, slot.countBuffer.handle, slot.countBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 1u, slot.offsetBuffer.handle, slot.offsetBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 2u, slot.recordBuffer.handle, slot.recordBuffer.capacityBytes);
    appendStorageBinding(finalizeDesc, 3u, slot.statsBuffer.handle, slot.statsBuffer.capacityBytes);
    slot.finalizeBindGroup = rhiDevice.createBindGroup(finalizeDesc);

    RhiBindGroupDesc fillDesc;
    fillDesc.layout = m_fillStage.bindGroupLayout;
    appendStorageBinding(fillDesc, 0u, slot.lightBoundsBuffer.handle, slot.lightBoundsBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 1u, slot.recordBuffer.handle, slot.recordBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 2u, slot.cursorBuffer.handle, slot.cursorBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 3u, slot.compactIndexBuffer.handle, slot.compactIndexBuffer.capacityBytes);
    appendStorageBinding(fillDesc, 4u, slot.statsBuffer.handle, slot.statsBuffer.capacityBytes);
    slot.fillBindGroup = rhiDevice.createBindGroup(fillDesc);

    RhiBindGroupDesc validateDesc;
    validateDesc.layout = m_validateStage.bindGroupLayout;
    appendStorageBinding(validateDesc, 0u, slot.recordBuffer.handle, slot.recordBuffer.capacityBytes);
    appendStorageBinding(validateDesc, 1u, slot.cursorBuffer.handle, slot.cursorBuffer.capacityBytes);
    appendStorageBinding(validateDesc, 2u, slot.statsBuffer.handle, slot.statsBuffer.capacityBytes);
    slot.validateBindGroup = rhiDevice.createBindGroup(validateDesc);
    if (!slot.finalizeBindGroup.isValid() || !slot.fillBindGroup.isValid() || !slot.validateBindGroup.isValid()) {
        destroyBuildBindGroups(slot);
        return false;
    }
    return true;
}

bool ClusteredLightingPass::ensureConsumerBindGroup(RhiDevice& rhiDevice, BuildSlot& slot) {
    if (slot.consumerBindGroup.isValid()) {
        return true;
    }
    RhiBindGroupDesc desc;
    desc.layout = m_consumerBindGroupLayout;
    appendStorageBinding(desc, 0u, slot.lightBuffer.handle, slot.lightBuffer.capacityBytes);
    appendStorageBinding(desc, 1u, slot.recordBuffer.handle, slot.recordBuffer.capacityBytes);
    appendStorageBinding(desc, 2u, slot.compactIndexBuffer.handle, slot.compactIndexBuffer.capacityBytes);
    appendStorageBinding(desc, 3u, slot.statsBuffer.handle, slot.statsBuffer.capacityBytes);
    appendStorageBinding(desc, 4u, m_localShadowResources.metadataBuffer, m_localShadowResources.metadataBufferBytes);
    appendCombinedTextureSamplerBinding(desc, 5u, m_localShadowResources.spotAtlasView, m_localShadowResources.sampler);
    appendCombinedTextureSamplerBinding(desc, 6u, m_localShadowResources.pointCubeArrayView,
                                        m_localShadowResources.sampler);
    appendStorageBinding(desc, 7u, slot.worldCellBuffer.handle, slot.worldCellBuffer.capacityBytes);
    appendStorageBinding(desc, 8u, slot.worldIndexBuffer.handle, slot.worldIndexBuffer.capacityBytes);
    appendStorageBinding(desc, 9u, slot.worldHeaderBuffer.handle, slot.worldHeaderBuffer.capacityBytes);
    slot.consumerBindGroup = rhiDevice.createBindGroup(desc);
    return slot.consumerBindGroup.isValid();
}

bool ClusteredLightingPass::importGraphResources(RenderGraph& graph, GraphResources& resources) const {
    if (!m_prepared || m_rhiDevice == nullptr) {
        return false;
    }
    const BuildSlot& slot = m_buildSlots[m_activeSlot];
    return importBuffer(graph, slot.lightBuffer, resources.lights) &&
           importBuffer(graph, slot.lightBoundsBuffer, resources.lightBounds) &&
           importBuffer(graph, slot.countBuffer, resources.counts) &&
           importBuffer(graph, slot.offsetBuffer, resources.offsets) &&
           importBuffer(graph, slot.recordBuffer, resources.records) &&
           importBuffer(graph, slot.cursorBuffer, resources.cursors) &&
           importBuffer(graph, slot.compactIndexBuffer, resources.compactIndices) &&
           importBuffer(graph, slot.scanScratchBuffer, resources.scanScratch) &&
           importBuffer(graph, slot.statsBuffer, resources.stats) &&
           importBuffer(graph, slot.worldCellBuffer, resources.worldCells) &&
           importBuffer(graph, slot.worldIndexBuffer, resources.worldIndices) &&
           importBuffer(graph, slot.worldHeaderBuffer, resources.worldHeader);
}

bool ClusteredLightingPass::importBuffer(RenderGraph& graph, const BufferResource& resource,
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

RgPassHandle ClusteredLightingPass::addGraphPasses(RenderGraph& graph, const GraphResources& resources,
                                                   const RgPassHandle dependency) {
    if (!m_prepared || !dependency.isValid()) {
        return {};
    }
    m_emptyBuildScheduled = false;
    if (m_lights.empty() && m_emptyBuildReady) {
        publishEmptyFrameStats();
        return dependency;
    }
    m_emptyBuildScheduled = m_lights.empty();

    RenderGraphPassBuilder upload =
        graph.addPass({"ClusteredLighting.Upload", RgPassType::Copy, RhiQueueType::Graphics});
    upload.dependsOn(dependency)
        .writeBuffer(resources.lights, RhiResourceState::TransferDst)
        .writeBuffer(resources.lightBounds, RhiResourceState::TransferDst)
        .writeBuffer(resources.counts, RhiResourceState::TransferDst)
        .writeBuffer(resources.cursors, RhiResourceState::TransferDst)
        .writeBuffer(resources.stats, RhiResourceState::TransferDst)
        .writeBuffer(resources.worldCells, RhiResourceState::TransferDst)
        .writeBuffer(resources.worldIndices, RhiResourceState::TransferDst)
        .writeBuffer(resources.worldHeader, RhiResourceState::TransferDst)
        .setExecute([this](RgPassContext& pass) { return recordUpload(pass.commandList()); });
    RgPassHandle tail = upload.handle();

    RenderGraphPassBuilder count =
        graph.addPass({"ClusteredLighting.Count", RgPassType::Compute, RhiQueueType::Compute});
    count.dependsOn(tail)
        .readBuffer(resources.lightBounds, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.counts, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) { return recordCount(pass.commandList()); });
    tail = count.handle();

    for (uint32_t level = 0u; level < m_scanLevels.size(); ++level) {
        const std::string passName = "ClusteredLighting.Scan." + std::to_string(level);
        RenderGraphPassBuilder scan = graph.addPass({passName.c_str(), RgPassType::Compute, RhiQueueType::Compute});
        scan.dependsOn(tail);
        if (level == 0u) {
            scan.readBuffer(resources.counts, RhiResourceState::StorageBuffer)
                .writeBuffer(resources.offsets, RhiResourceState::StorageBuffer)
                .writeBuffer(resources.scanScratch, RhiResourceState::StorageBuffer);
        } else {
            scan.readWriteBuffer(resources.scanScratch, RhiResourceState::StorageBuffer);
        }
        scan.setExecute([this, level](RgPassContext& pass) { return recordScan(pass.commandList(), level); });
        tail = scan.handle();
    }

    for (uint32_t child = static_cast<uint32_t>(m_scanLevels.size() - 1u); child > 0u; --child) {
        const uint32_t childLevel = child - 1u;
        const std::string passName = "ClusteredLighting.ScanAdd." + std::to_string(childLevel);
        RenderGraphPassBuilder add = graph.addPass({passName.c_str(), RgPassType::Compute, RhiQueueType::Compute});
        add.dependsOn(tail);
        if (childLevel == 0u) {
            add.readWriteBuffer(resources.offsets, RhiResourceState::StorageBuffer)
                .readBuffer(resources.scanScratch, RhiResourceState::StorageBuffer);
        } else {
            add.readWriteBuffer(resources.scanScratch, RhiResourceState::StorageBuffer);
        }
        add.setExecute(
            [this, childLevel](RgPassContext& pass) { return recordScanAdd(pass.commandList(), childLevel); });
        tail = add.handle();
    }

    RenderGraphPassBuilder finalize =
        graph.addPass({"ClusteredLighting.Finalize", RgPassType::Compute, RhiQueueType::Compute});
    finalize.dependsOn(tail)
        .readBuffer(resources.counts, RhiResourceState::StorageBuffer)
        .readBuffer(resources.offsets, RhiResourceState::StorageBuffer)
        .writeBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) { return recordFinalize(pass.commandList()); });
    tail = finalize.handle();

    RenderGraphPassBuilder fill = graph.addPass({"ClusteredLighting.Fill", RgPassType::Compute, RhiQueueType::Compute});
    fill.dependsOn(tail)
        .readBuffer(resources.lightBounds, RhiResourceState::StorageBuffer)
        .readBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.cursors, RhiResourceState::StorageBuffer)
        .writeBuffer(resources.compactIndices, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) { return recordFill(pass.commandList()); });
    tail = fill.handle();

    RenderGraphPassBuilder validate =
        graph.addPass({"ClusteredLighting.Validate", RgPassType::Compute, RhiQueueType::Compute});
    validate.dependsOn(tail)
        .readBuffer(resources.records, RhiResourceState::StorageBuffer)
        .readBuffer(resources.cursors, RhiResourceState::StorageBuffer)
        .readWriteBuffer(resources.stats, RhiResourceState::StorageBuffer)
        .setExecute([this](RgPassContext& pass) { return recordValidateAndReadback(pass.commandList()); });
    return validate.handle();
}

bool ClusteredLightingPass::recordUpload(RhiCommandList& commandList) const {
    const BuildSlot& slot = m_buildSlots[m_activeSlot];
    if (!m_lights.empty()) {
        commandList.updateBuffer(slot.lightBuffer.handle, 0u, m_lights.data(), m_lights.size() * sizeof(m_lights.front()));
    }
    if (!m_lightBounds.empty()) {
        commandList.updateBuffer(slot.lightBoundsBuffer.handle, 0u, m_lightBounds.data(),
                                 m_lightBounds.size() * sizeof(m_lightBounds.front()));
    }
    if (!m_worldLightGrid.cells.empty()) {
        commandList.updateBuffer(slot.worldCellBuffer.handle, 0u, m_worldLightGrid.cells.data(),
                                 m_worldLightGrid.cells.size() * sizeof(m_worldLightGrid.cells.front()));
    }
    if (!m_worldLightGrid.lightIndices.empty()) {
        commandList.updateBuffer(slot.worldIndexBuffer.handle, 0u, m_worldLightGrid.lightIndices.data(),
                                 m_worldLightGrid.lightIndices.size() * sizeof(m_worldLightGrid.lightIndices.front()));
    }
    commandList.updateBuffer(slot.worldHeaderBuffer.handle, 0u, &m_worldLightGrid.header, sizeof(m_worldLightGrid.header));
    commandList.updateBuffer(slot.countBuffer.handle, 0u, m_zeroClusterWords.data(),
                             m_zeroClusterWords.size() * sizeof(m_zeroClusterWords.front()));
    commandList.updateBuffer(slot.cursorBuffer.handle, 0u, m_zeroClusterWords.data(),
                             m_zeroClusterWords.size() * sizeof(m_zeroClusterWords.front()));
    const uint32_t stats[kStatsWordCount] = {0u,
                                             0u,
                                             0u,
                                             0u,
                                             m_grid.clusterCount,
                                             static_cast<uint32_t>(m_lights.size()),
                                             m_indexCapacity,
                                             renderer::contracts::kGpuLightContractVersion};
    commandList.updateBuffer(slot.statsBuffer.handle, 0u, stats, sizeof(stats));
    return true;
}

bool ClusteredLightingPass::recordCount(RhiCommandList& commandList) const {
    if (m_lightBounds.empty()) {
        return true;
    }
    ClusterGridPushConstants push;
    push.inverseProjection = m_inverseProjection;
    push.gridAndLightCount = {m_grid.tileCountX, m_grid.tileCountY, m_grid.depthSliceCount,
                              static_cast<uint32_t>(m_lightBounds.size())};
    push.depthParameters = {m_grid.nearPlane, m_grid.farPlane, m_grid.depthLogScale, m_grid.depthLogBias};
    commandList.setComputePipeline(m_countStage.pipeline);
    commandList.setBindGroup(0u, m_buildSlots[m_activeSlot].countBindGroup);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(static_cast<uint32_t>(m_lightBounds.size()), m_grid.depthSliceCount, 1u);
    return true;
}

bool ClusteredLightingPass::recordScan(RhiCommandList& commandList, const uint32_t level) const {
    if (level >= m_scanLevels.size()) {
        return false;
    }
    const ScanLevel& scanLevel = m_scanLevels[level];
    ClusterScanPushConstants push;
    push.offsetsAndCount = {scanLevel.inputOffsetWords, scanLevel.outputOffsetWords, scanLevel.blockSumOffsetWords,
                            scanLevel.elementCount};
    commandList.setComputePipeline(level == 0u ? m_scanStage.pipeline : m_scanScratchStage.pipeline);
    commandList.setBindGroup(0u, m_buildSlots[m_activeSlot].scanBindGroups[level]);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(scanLevel.groupCount, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordScanAdd(RhiCommandList& commandList, const uint32_t childLevel) const {
    const BuildSlot& slot = m_buildSlots[m_activeSlot];
    if (childLevel + 1u >= m_scanLevels.size() || childLevel >= slot.scanAddBindGroups.size()) {
        return false;
    }
    const ScanLevel& child = m_scanLevels[childLevel];
    const ScanLevel& parent = m_scanLevels[childLevel + 1u];
    ClusterScanPushConstants push;
    push.offsetsAndCount = {child.outputOffsetWords, parent.outputOffsetWords, child.elementCount,
                            renderer::contracts::kClusterScanElementsPerWorkgroup};
    commandList.setComputePipeline(m_scanAddStage.pipeline);
    commandList.setBindGroup(0u, slot.scanAddBindGroups[childLevel]);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((child.elementCount + 255u) / 256u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordFinalize(RhiCommandList& commandList) const {
    ClusterScanPushConstants push;
    push.offsetsAndCount = {m_grid.clusterCount, m_indexCapacity, static_cast<uint32_t>(m_lights.size()), 0u};
    commandList.setComputePipeline(m_finalizeStage.pipeline);
    commandList.setBindGroup(0u, m_buildSlots[m_activeSlot].finalizeBindGroup);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((m_grid.clusterCount + 255u) / 256u, 1u, 1u);
    return true;
}

bool ClusteredLightingPass::recordFill(RhiCommandList& commandList) const {
    if (m_lightBounds.empty()) {
        return true;
    }
    ClusterFillPushConstants push;
    push.inverseProjection = m_inverseProjection;
    push.gridAndLightCount = {m_grid.tileCountX, m_grid.tileCountY, m_grid.depthSliceCount,
                              static_cast<uint32_t>(m_lightBounds.size())};
    push.depthParameters = {m_grid.nearPlane, m_grid.farPlane, m_grid.depthLogScale, m_grid.depthLogBias};
    push.capacity = {m_indexCapacity, 0u, 0u, 0u};
    commandList.setComputePipeline(m_fillStage.pipeline);
    commandList.setBindGroup(0u, m_buildSlots[m_activeSlot].fillBindGroup);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch(static_cast<uint32_t>(m_lightBounds.size()), m_grid.depthSliceCount, 1u);
    return true;
}

bool ClusteredLightingPass::recordValidateAndReadback(RhiCommandList& commandList) {
    const BuildSlot& slot = m_buildSlots[m_activeSlot];
    ClusterScanPushConstants push;
    push.offsetsAndCount = {m_grid.clusterCount, m_indexCapacity, 0u, 0u};
    commandList.setComputePipeline(m_validateStage.pipeline);
    commandList.setBindGroup(0u, slot.validateBindGroup);
    commandList.pushConstants(&push, sizeof(push), rhiFlag(RhiShaderStage::Compute));
    commandList.dispatch((m_grid.clusterCount + 255u) / 256u, 1u, 1u);
    if (!m_statsReadbackSlotAvailable) {
        return true;
    }

    const uint32_t ringIndex = m_statsReadbackWriteIndex;
    commandList.bufferBarrier({slot.statsBuffer.handle, RhiResourceState::StorageBuffer, RhiResourceState::TransferSrc});
    if (m_statsReadbackWritten[ringIndex]) {
        commandList.bufferBarrier(
            {m_statsReadbackBuffers[ringIndex], RhiResourceState::HostRead, RhiResourceState::TransferDst});
    }
    RhiBufferCopy copy;
    copy.src = slot.statsBuffer.handle;
    copy.dst = m_statsReadbackBuffers[ringIndex];
    copy.size = sizeof(uint32_t) * kStatsWordCount;
    commandList.copyBuffer(copy);
    commandList.bufferBarrier(
        {m_statsReadbackBuffers[ringIndex], RhiResourceState::TransferDst, RhiResourceState::HostRead});
    commandList.bufferBarrier({slot.statsBuffer.handle, RhiResourceState::TransferSrc, RhiResourceState::StorageBuffer});
    m_statsReadbackWorldSnapshots[ringIndex] = captureWorldLightGridStats();
    m_pendingStatsReadbackIndex = ringIndex;
    m_statsReadbackPending = true;
    return true;
}

ClusteredLightingPass::WorldLightGridStatsSnapshot ClusteredLightingPass::captureWorldLightGridStats() const {
    return {static_cast<uint32_t>(m_worldLightGrid.cells.size()),
            static_cast<uint32_t>(m_worldLightGrid.lightIndices.size()), m_worldLightGrid.header.countsAndVersion.z,
            m_worldLightGrid.maxLightsPerCell};
}

void ClusteredLightingPass::applyWorldLightGridStats(const WorldLightGridStatsSnapshot& snapshot) {
    m_frameStats.worldCellCount = snapshot.cellCount;
    m_frameStats.worldIndexCount = snapshot.indexCount;
    m_frameStats.worldGlobalLightCount = snapshot.globalLightCount;
    m_frameStats.maxWorldLightsPerCell = snapshot.maxLightsPerCell;
}

void ClusteredLightingPass::publishEmptyFrameStats() {
    m_frameStats = {};
    m_frameStats.valid = true;
    m_frameStats.clusterCount = m_grid.clusterCount;
    m_frameStats.indexCapacity = m_indexCapacity;
    applyWorldLightGridStats(captureWorldLightGridStats());
}

void ClusteredLightingPass::finishGraphExecution(const bool succeeded, const RhiSubmissionToken completionToken) {
    if (!succeeded) {
        m_emptyBuildReady = false;
    }
    // Record the token even for failed frames: partial submissions may have
    // already touched this slot's buffers, so the next reuse must still wait.
    if (completionToken.isValid()) {
        m_buildSlots[m_activeSlot].lastUseToken = completionToken;
    }
    const bool emptyBuildScheduled = m_emptyBuildScheduled;
    m_emptyBuildScheduled = false;
    if (!m_statsReadbackPending) {
        if (emptyBuildScheduled && succeeded) {
            m_emptyBuildReady = true;
            publishEmptyFrameStats();
        }
        return;
    }
    bool completionValid = succeeded;
    if (succeeded && completionToken.isValid()) {
        m_statsReadbackWritten[m_pendingStatsReadbackIndex] = true;
        m_statsReadbackTokens[m_pendingStatsReadbackIndex] = completionToken;
        m_statsReadbackWriteIndex = (m_pendingStatsReadbackIndex + 1u) % kStatsReadbackRingSize;
    } else if (succeeded) {
        m_gpuBuildFailed = true;
        completionValid = false;
        MECRAFT_LOG_STREAM(std::cerr << "[ClusteredLightingPass] Stats readback submission token is invalid\n");
    }
    m_statsReadbackPending = false;
    if (emptyBuildScheduled) {
        m_emptyBuildReady = completionValid;
        if (m_emptyBuildReady) {
            publishEmptyFrameStats();
        }
    }
}

void ClusteredLightingPass::destroyBuildBindGroups(BuildSlot& slot) {
    if (m_rhiDevice != nullptr) {
        const RhiBindGroupHandle fixed[] = {slot.countBindGroup, slot.finalizeBindGroup, slot.fillBindGroup,
                                            slot.validateBindGroup, slot.consumerBindGroup};
        for (const RhiBindGroupHandle bindGroup : fixed) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
        for (const RhiBindGroupHandle bindGroup : slot.scanBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
        for (const RhiBindGroupHandle bindGroup : slot.scanAddBindGroups) {
            if (bindGroup.isValid()) {
                m_rhiDevice->destroyBindGroup(bindGroup);
            }
        }
    }
    slot.countBindGroup = {};
    slot.scanBindGroups.clear();
    slot.scanAddBindGroups.clear();
    slot.finalizeBindGroup = {};
    slot.fillBindGroup = {};
    slot.validateBindGroup = {};
    slot.consumerBindGroup = {};
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
    destroyComputeStage(m_scanScratchStage);
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
        for (BuildSlot& slot : m_buildSlots) {
            BufferResource* resources[] = {&slot.lightBuffer,        &slot.lightBoundsBuffer, &slot.countBuffer,
                                           &slot.offsetBuffer,       &slot.recordBuffer,      &slot.cursorBuffer,
                                           &slot.compactIndexBuffer, &slot.scanScratchBuffer, &slot.statsBuffer,
                                           &slot.worldCellBuffer,    &slot.worldIndexBuffer,  &slot.worldHeaderBuffer};
            for (BufferResource* resource : resources) {
                if (resource->handle.isValid()) {
                    m_rhiDevice->destroyBuffer(resource->handle);
                }
                *resource = {};
            }
            slot.lastUseToken = {};
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
    m_statsReadbackWorldSnapshots.fill(WorldLightGridStatsSnapshot{});
    m_statsReadbackWriteIndex = 0u;
    m_pendingStatsReadbackIndex = 0u;
    m_statsReadbackSlotAvailable = true;
    m_statsReadbackPending = false;
    m_emptyBuildReady = false;
    m_emptyBuildScheduled = false;
}

void ClusteredLightingPass::shutdown() {
    for (BuildSlot& slot : m_buildSlots) {
        destroyBuildBindGroups(slot);
    }
    destroyPipelines();
    destroyBuffers();
    m_rhiDevice = nullptr;
    m_prepared = false;
    m_gpuBuildFailed = false;
    m_emptyBuildReady = false;
    m_emptyBuildScheduled = false;
    m_lightBounds.clear();
    m_zeroClusterWords.clear();
    m_scanLevels.clear();
    m_grid = {};
    m_requiredIndexCount = 0u;
    m_indexCapacity = 0u;
    m_scanScratchWordCount = 0u;
    m_frameStats = {};
    m_localShadowResources = {};
    m_activeSlot = 0u;
}
