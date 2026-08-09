#include "RenderDebugService.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

double gpuTimerPassMilliseconds(const GpuFrameStats& stats, const GpuTimerPass pass) {
    switch (pass) {
    case GpuTimerPass::GBuffer: return stats.gbufferMs;
    case GpuTimerPass::Shadow: return stats.shadowMs;
    case GpuTimerPass::Ssao: return stats.ssaoMs;
    case GpuTimerPass::Ssgi: return stats.ssgiMs;
    case GpuTimerPass::Rtgi: return stats.rtgiMs;
    case GpuTimerPass::Nrd: return stats.nrdMs;
    case GpuTimerPass::RtgiTrace: return stats.rtgiTraceMs;
    case GpuTimerPass::RtgiSignalPack: return stats.rtgiSignalPackMs;
    case GpuTimerPass::NrdGuidePrep: return stats.nrdGuidePrepMs;
    case GpuTimerPass::NrdDispatch: return stats.nrdDispatchMs;
    case GpuTimerPass::SceneTlas: return stats.sceneTlasMs;
    case GpuTimerPass::TerrainBlasBuild: return stats.terrainBlasBuildMs;
    case GpuTimerPass::TerrainBlasCompaction: return stats.terrainBlasCompactionMs;
    case GpuTimerPass::AccelerationStructureDynamicPrepare: return stats.accelerationStructureDynamicPrepareMs;
    case GpuTimerPass::RtgiSceneTlasBootstrap: return stats.rtgiSceneTlasBootstrapMs;
    case GpuTimerPass::Lighting: return stats.lightingMs;
    case GpuTimerPass::Transparent: return stats.transparentMs;
    case GpuTimerPass::Volumetric: return stats.volumetricMs;
    case GpuTimerPass::Reflection: return stats.reflectionMs;
    case GpuTimerPass::Cloud: return stats.cloudMs;
    case GpuTimerPass::Water: return stats.waterMs;
    case GpuTimerPass::Post: return stats.postMs;
    case GpuTimerPass::Count: break;
    }
    std::abort();
}

bool gpuTimerPassIncludedInTrackedTotal(const GpuTimerPass pass) {
    return pass != GpuTimerPass::RtgiTrace && pass != GpuTimerPass::RtgiSignalPack &&
           pass != GpuTimerPass::NrdGuidePrep && pass != GpuTimerPass::NrdDispatch &&
           pass != GpuTimerPass::RtgiSceneTlasBootstrap;
}

GpuTimingPercentiles calculatePercentiles(const std::array<double, GpuTimingHistory::kCapacity>& samples,
                                          const size_t sampleCount) {
    if (sampleCount == 0u) {
        return {};
    }
    std::array<double, GpuTimingHistory::kCapacity> sortedSamples{};
    std::copy_n(samples.begin(), sampleCount, sortedSamples.begin());
    std::sort(sortedSamples.begin(), sortedSamples.begin() + sampleCount);
    const auto nearestRank = [&](const size_t percentile) {
        const size_t rank = (sampleCount * percentile + 99u) / 100u;
        return sortedSamples[rank - 1u];
    };
    return {nearestRank(50u), nearestRank(95u), nearestRank(99u)};
}

bool exactPartition(const uint64_t first, const uint64_t second, const uint64_t total) {
    return first <= total && second == total - first;
}

double renderGraphCpuTimingMilliseconds(const RenderGraphFrameStats& stats, const RenderGraphCpuTimingStage stage) {
    switch (stage) {
    case RenderGraphCpuTimingStage::Build: return stats.cpuBuildMs;
    case RenderGraphCpuTimingStage::Compile: return stats.cpuCompileMs;
    case RenderGraphCpuTimingStage::Execute: return stats.cpuExecuteMs;
    case RenderGraphCpuTimingStage::Record: return stats.cpuRecordMs;
    case RenderGraphCpuTimingStage::Submit: return stats.cpuSubmitMs;
    case RenderGraphCpuTimingStage::ShadowPrep: return stats.cpuShadowPrepMs;
    case RenderGraphCpuTimingStage::Context: return stats.cpuContextMs;
    case RenderGraphCpuTimingStage::TerrainPrep: return stats.cpuTerrainPrepMs;
    case RenderGraphCpuTimingStage::Count: break;
    }
    std::abort();
}

double renderGraphGpuTimingMilliseconds(const RenderGraphFrameStats& stats, const RenderGraphGpuTimingMetric metric) {
    switch (metric) {
    case RenderGraphGpuTimingMetric::Total: return stats.gpuTotalMs;
    case RenderGraphGpuTimingMetric::Span: return stats.gpuSpanMs;
    case RenderGraphGpuTimingMetric::Idle: return stats.gpuIdleMs;
    case RenderGraphGpuTimingMetric::Overlap: return std::max(0.0, stats.gpuTotalMs - stats.gpuSpanMs);
    case RenderGraphGpuTimingMetric::Count: break;
    }
    std::abort();
}

} // namespace

const char* gpuTimerPassName(const GpuTimerPass pass) {
    switch (pass) {
    case GpuTimerPass::GBuffer: return "GBuffer";
    case GpuTimerPass::Shadow: return "Shadow";
    case GpuTimerPass::Ssao: return "SSAO";
    case GpuTimerPass::Ssgi: return "SSGI";
    case GpuTimerPass::Rtgi: return "RTGI";
    case GpuTimerPass::Nrd: return "NRD";
    case GpuTimerPass::Lighting: return "Lighting";
    case GpuTimerPass::Transparent: return "Transparent";
    case GpuTimerPass::Volumetric: return "Volumetric";
    case GpuTimerPass::Reflection: return "Reflection";
    case GpuTimerPass::Cloud: return "Cloud";
    case GpuTimerPass::Water: return "Water";
    case GpuTimerPass::Post: return "Post";
    case GpuTimerPass::RtgiTrace: return "RTGI.Trace";
    case GpuTimerPass::RtgiSignalPack: return "RTGI.SignalPack";
    case GpuTimerPass::NrdGuidePrep: return "NRD.GuidePrep";
    case GpuTimerPass::NrdDispatch: return "NRD.Dispatch";
    case GpuTimerPass::SceneTlas: return "SceneTLAS";
    case GpuTimerPass::TerrainBlasBuild: return "TerrainBLAS.Build";
    case GpuTimerPass::TerrainBlasCompaction: return "TerrainBLAS.Compaction";
    case GpuTimerPass::AccelerationStructureDynamicPrepare: return "AS.DynamicResourcePreparation";
    case GpuTimerPass::RtgiSceneTlasBootstrap: return "RTGI.SceneTLASBootstrap";
    case GpuTimerPass::Count: break;
    }
    std::abort();
}

const char* accelerationStructureStageName(const AccelerationStructureStage stage) {
    switch (stage) {
    case AccelerationStructureStage::SceneTlas: return "SceneTLAS";
    case AccelerationStructureStage::TerrainBlasBuild: return "TerrainBLAS.Build";
    case AccelerationStructureStage::TerrainBlasCompaction: return "TerrainBLAS.Compaction";
    case AccelerationStructureStage::DynamicResourcePreparation: return "AS.DynamicResourcePreparation";
    case AccelerationStructureStage::RtgiSceneTlasBootstrap: return "RTGI.SceneTLASBootstrap";
    case AccelerationStructureStage::Count: break;
    }
    std::abort();
}

const char* renderGraphCpuTimingStageName(const RenderGraphCpuTimingStage stage) {
    switch (stage) {
    case RenderGraphCpuTimingStage::Build: return "Build";
    case RenderGraphCpuTimingStage::Compile: return "Compile";
    case RenderGraphCpuTimingStage::Execute: return "Execute";
    case RenderGraphCpuTimingStage::Record: return "Record";
    case RenderGraphCpuTimingStage::Submit: return "Submit";
    case RenderGraphCpuTimingStage::ShadowPrep: return "ShadowPrep";
    case RenderGraphCpuTimingStage::Context: return "Context";
    case RenderGraphCpuTimingStage::TerrainPrep: return "TerrainPrep";
    case RenderGraphCpuTimingStage::Count: break;
    }
    std::abort();
}

const char* renderGraphGpuTimingMetricName(const RenderGraphGpuTimingMetric metric) {
    switch (metric) {
    case RenderGraphGpuTimingMetric::Total: return "Total";
    case RenderGraphGpuTimingMetric::Span: return "Span";
    case RenderGraphGpuTimingMetric::Idle: return "Idle";
    case RenderGraphGpuTimingMetric::Overlap: return "Overlap";
    case RenderGraphGpuTimingMetric::Count: break;
    }
    std::abort();
}

void GpuTimingHistory::reset() {
    m_nextSample = 0u;
    m_sampleCount = 0u;
    m_observedSampleCount = 0u;
    m_lastSequence = 0u;
}

bool GpuTimingHistory::record(const GpuFrameStats& stats) {
    if (!stats.supported || !stats.valid || stats.sequence == 0u || stats.sequence <= m_lastSequence) {
        return false;
    }

    std::array<double, static_cast<size_t>(GpuTimerPass::Count)> passValues{};
    double totalMs = 0.0;
    for (size_t passIndex = 0u; passIndex < static_cast<size_t>(GpuTimerPass::Count); ++passIndex) {
        const auto pass = static_cast<GpuTimerPass>(passIndex);
        const double milliseconds = gpuTimerPassMilliseconds(stats, pass);
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return false;
        }
        passValues[passIndex] = milliseconds;
        if (gpuTimerPassIncludedInTrackedTotal(pass)) {
            totalMs += milliseconds;
        }
    }
    for (size_t passIndex = 0u; passIndex < passValues.size(); ++passIndex) {
        m_passSamples[passIndex][m_nextSample] = passValues[passIndex];
    }
    m_totalSamples[m_nextSample] = totalMs;
    m_nextSample = (m_nextSample + 1u) % kCapacity;
    m_sampleCount = std::min(m_sampleCount + 1u, kCapacity);
    ++m_observedSampleCount;
    m_lastSequence = stats.sequence;
    return true;
}

GpuTimingWindowStats GpuTimingHistory::snapshot() const {
    GpuTimingWindowStats stats;
    stats.valid = m_sampleCount > 0u;
    stats.sampleCount = m_sampleCount;
    stats.capacity = kCapacity;
    stats.observedSampleCount = m_observedSampleCount;
    stats.totalTrackedGpuMs = calculatePercentiles(m_totalSamples, m_sampleCount);
    for (size_t passIndex = 0u; passIndex < static_cast<size_t>(GpuTimerPass::Count); ++passIndex) {
        GpuTimerPassWindowStats& passStats = stats.passes[passIndex];
        passStats.pass = static_cast<GpuTimerPass>(passIndex);
        passStats.gpuMs = calculatePercentiles(m_passSamples[passIndex], m_sampleCount);
    }
    return stats;
}

void AccelerationStructureHistory::reset() {
    for (auto& samples : m_cpuSamples) {
        samples.fill(0.0);
    }
    for (auto& samples : m_operationSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_instanceSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_primitiveSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_scratchByteSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_structureByteSamples) {
        samples.fill(0u);
    }
    m_activeSceneTlasInstanceSamples.fill(0u);
    m_activeSceneTlasBlasSamples.fill(0u);
    m_activeTerrainBlasSamples.fill(0u);
    m_activeSceneTlasByteSamples.fill(0u);
    m_activeSceneReferencedBlasByteSamples.fill(0u);
    m_activeTerrainBlasByteSamples.fill(0u);
    m_activeTerrainPrimitiveSamples.fill(0u);
    m_sceneTlasGenerationAllocationSamples.fill(0u);
    m_sceneTlasGenerationReuseSamples.fill(0u);
    m_sceneTlasGenerationReuseWaitSamples.fill(0u);
    m_retiredSceneTlasGenerationSamples.fill(0u);
    m_activeTerrainOpaquePrimitiveSamples.fill(0u);
    m_activeTerrainCutoutPrimitiveSamples.fill(0u);
    m_builtTerrainOpaquePrimitiveSamples.fill(0u);
    m_builtTerrainCutoutPrimitiveSamples.fill(0u);
    m_activeTerrainOpacityMicromapSamples.fill(0u);
    m_activeTerrainOpacityMicromapByteSamples.fill(0u);
    m_activeTerrainOpacityOpaqueMicroTriangleSamples.fill(0u);
    m_activeTerrainOpacityTransparentMicroTriangleSamples.fill(0u);
    m_activeTerrainOpacityUnknownMicroTriangleSamples.fill(0u);
    m_builtTerrainOpacityMicromapSamples.fill(0u);
    m_builtTerrainOpacityPrimitiveSamples.fill(0u);
    m_terrainOpacityBuildInputByteSamples.fill(0u);
    m_terrainOpacityBuildStorageByteSamples.fill(0u);
    m_terrainOpacityBuildScratchByteSamples.fill(0u);
    m_builtTerrainOpacityOpaqueMicroTriangleSamples.fill(0u);
    m_builtTerrainOpacityTransparentMicroTriangleSamples.fill(0u);
    m_builtTerrainOpacityUnknownMicroTriangleSamples.fill(0u);
    for (auto& samples : m_dynamicBlasActionSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_dynamicBlasRigidReuseRejectSamples) {
        samples.fill(0u);
    }
    for (auto& samples : m_dynamicBlasUpdateRejectSamples) {
        samples.fill(0u);
    }
    m_nextSample = 0u;
    m_sampleCount = 0u;
    m_observedSampleCount = 0u;
    m_lastSequence = 0u;
    m_latest = {};
}

bool AccelerationStructureHistory::record(const AccelerationStructureFrameStats& stats) {
    if (!stats.valid || stats.sequence == 0u || stats.sequence <= m_lastSequence) {
        return false;
    }

    for (const AccelerationStructureStageFrameStats& stage : stats.stages) {
        if (!std::isfinite(stage.cpuMs) || stage.cpuMs < 0.0) {
            return false;
        }
    }
    const StaticBlasFrameStats& staticBlas = stats.staticBlas;
    const TerrainOpacityMicromapFrameStats& opacityMicromaps = stats.terrainOpacityMicromaps;
    if (!std::isfinite(staticBlas.buildCpuMs) || staticBlas.buildCpuMs < 0.0 || !std::isfinite(staticBlas.buildGpuMs) ||
        staticBlas.buildGpuMs < 0.0 || !std::isfinite(staticBlas.compactionCpuMs) || staticBlas.compactionCpuMs < 0.0 ||
        !std::isfinite(staticBlas.compactionGpuMs) || staticBlas.compactionGpuMs < 0.0 ||
        !exactPartition(staticBlas.opaquePrimitiveCount, staticBlas.cutoutPrimitiveCount, staticBlas.primitiveCount) ||
        !exactPartition(stats.terrainBuckets.activeOpaquePrimitives, stats.terrainBuckets.activeCutoutPrimitives,
                        stats.activeTerrainPrimitiveCount) ||
        !exactPartition(
            stats.terrainBuckets.builtOpaquePrimitives, stats.terrainBuckets.builtCutoutPrimitives,
            stats.stages[static_cast<size_t>(AccelerationStructureStage::TerrainBlasBuild)].primitiveCount) ||
        opacityMicromaps.activeMicromapCount > stats.activeTerrainBlas ||
        opacityMicromaps.builtMicromapCount >
            stats.stages[static_cast<size_t>(AccelerationStructureStage::TerrainBlasBuild)].operationCount ||
        (!opacityMicromaps.enabled &&
         (opacityMicromaps.activeMicromapCount != 0u || opacityMicromaps.activeMicromapBytes != 0u ||
          opacityMicromaps.builtMicromapCount != 0u || opacityMicromaps.builtPrimitiveCount != 0u ||
          opacityMicromaps.buildInputBytes != 0u || opacityMicromaps.buildStorageBytes != 0u ||
          opacityMicromaps.buildScratchBytes != 0u)) ||
        (opacityMicromaps.enabled && (opacityMicromaps.subdivisionLevel > 10u ||
                                      opacityMicromaps.alphaTextureHash == 0u || opacityMicromaps.profileHash == 0u))) {
        return false;
    }

    for (size_t stageIndex = 0u; stageIndex < stats.stages.size(); ++stageIndex) {
        const AccelerationStructureStageFrameStats& stage = stats.stages[stageIndex];
        m_cpuSamples[stageIndex][m_nextSample] = stage.cpuMs;
        m_operationSamples[stageIndex][m_nextSample] = stage.operationCount;
        m_instanceSamples[stageIndex][m_nextSample] = stage.instanceCount;
        m_primitiveSamples[stageIndex][m_nextSample] = stage.primitiveCount;
        m_scratchByteSamples[stageIndex][m_nextSample] = stage.scratchBytes;
        m_structureByteSamples[stageIndex][m_nextSample] = stage.structureBytes;
    }
    m_activeSceneTlasInstanceSamples[m_nextSample] = stats.activeSceneTlasInstances;
    m_activeSceneTlasBlasSamples[m_nextSample] = stats.activeSceneTlasBlas;
    m_activeTerrainBlasSamples[m_nextSample] = stats.activeTerrainBlas;
    m_activeSceneTlasByteSamples[m_nextSample] = stats.activeSceneTlasBytes;
    m_activeSceneReferencedBlasByteSamples[m_nextSample] = stats.activeSceneReferencedBlasBytes;
    m_activeTerrainBlasByteSamples[m_nextSample] = stats.activeTerrainBlasBytes;
    m_activeTerrainPrimitiveSamples[m_nextSample] = stats.activeTerrainPrimitiveCount;
    m_sceneTlasGenerationAllocationSamples[m_nextSample] = stats.sceneTlasGenerationAllocations;
    m_sceneTlasGenerationReuseSamples[m_nextSample] = stats.sceneTlasGenerationReuses;
    m_sceneTlasGenerationReuseWaitSamples[m_nextSample] = stats.sceneTlasGenerationReuseWaits;
    m_retiredSceneTlasGenerationSamples[m_nextSample] = stats.retiredSceneTlasGenerations;
    m_activeTerrainOpaquePrimitiveSamples[m_nextSample] = stats.terrainBuckets.activeOpaquePrimitives;
    m_activeTerrainCutoutPrimitiveSamples[m_nextSample] = stats.terrainBuckets.activeCutoutPrimitives;
    m_builtTerrainOpaquePrimitiveSamples[m_nextSample] = stats.terrainBuckets.builtOpaquePrimitives;
    m_builtTerrainCutoutPrimitiveSamples[m_nextSample] = stats.terrainBuckets.builtCutoutPrimitives;
    m_activeTerrainOpacityMicromapSamples[m_nextSample] = opacityMicromaps.activeMicromapCount;
    m_activeTerrainOpacityMicromapByteSamples[m_nextSample] = opacityMicromaps.activeMicromapBytes;
    m_activeTerrainOpacityOpaqueMicroTriangleSamples[m_nextSample] = opacityMicromaps.activeOpaqueMicroTriangles;
    m_activeTerrainOpacityTransparentMicroTriangleSamples[m_nextSample] =
        opacityMicromaps.activeTransparentMicroTriangles;
    m_activeTerrainOpacityUnknownMicroTriangleSamples[m_nextSample] = opacityMicromaps.activeUnknownMicroTriangles;
    m_builtTerrainOpacityMicromapSamples[m_nextSample] = opacityMicromaps.builtMicromapCount;
    m_builtTerrainOpacityPrimitiveSamples[m_nextSample] = opacityMicromaps.builtPrimitiveCount;
    m_terrainOpacityBuildInputByteSamples[m_nextSample] = opacityMicromaps.buildInputBytes;
    m_terrainOpacityBuildStorageByteSamples[m_nextSample] = opacityMicromaps.buildStorageBytes;
    m_terrainOpacityBuildScratchByteSamples[m_nextSample] = opacityMicromaps.buildScratchBytes;
    m_builtTerrainOpacityOpaqueMicroTriangleSamples[m_nextSample] = opacityMicromaps.builtOpaqueMicroTriangles;
    m_builtTerrainOpacityTransparentMicroTriangleSamples[m_nextSample] =
        opacityMicromaps.builtTransparentMicroTriangles;
    m_builtTerrainOpacityUnknownMicroTriangleSamples[m_nextSample] = opacityMicromaps.builtUnknownMicroTriangles;
    for (size_t index = 0u; index < stats.dynamicBlas.actionCounts.size(); ++index) {
        m_dynamicBlasActionSamples[index][m_nextSample] = stats.dynamicBlas.actionCounts[index];
    }
    for (size_t index = 0u; index < stats.dynamicBlas.rigidReuseRejectCounts.size(); ++index) {
        m_dynamicBlasRigidReuseRejectSamples[index][m_nextSample] = stats.dynamicBlas.rigidReuseRejectCounts[index];
    }
    for (size_t index = 0u; index < stats.dynamicBlas.updateRejectCounts.size(); ++index) {
        m_dynamicBlasUpdateRejectSamples[index][m_nextSample] = stats.dynamicBlas.updateRejectCounts[index];
    }
    m_nextSample = (m_nextSample + 1u) % kCapacity;
    m_sampleCount = std::min(m_sampleCount + 1u, kCapacity);
    ++m_observedSampleCount;
    m_lastSequence = stats.sequence;
    m_latest = stats;
    return true;
}

AccelerationStructureWindowStats AccelerationStructureHistory::snapshot() const {
    AccelerationStructureWindowStats stats;
    stats.valid = m_sampleCount > 0u;
    stats.sampleCount = m_sampleCount;
    stats.capacity = kCapacity;
    stats.observedSampleCount = m_observedSampleCount;
    stats.latest = m_latest;
    stats.dynamicBlas.producerConnected = m_latest.dynamicBlas.producerConnected;

    for (size_t stageIndex = 0u; stageIndex < stats.stages.size(); ++stageIndex) {
        AccelerationStructureStageWindowStats& stage = stats.stages[stageIndex];
        stage.cpuMs = calculatePercentiles(m_cpuSamples[stageIndex], m_sampleCount);
        for (size_t sampleIndex = 0u; sampleIndex < m_sampleCount; ++sampleIndex) {
            stage.operationCount += m_operationSamples[stageIndex][sampleIndex];
            stage.peakOperationsPerFrame =
                std::max(stage.peakOperationsPerFrame, m_operationSamples[stageIndex][sampleIndex]);
            stage.peakInstancesPerFrame =
                std::max(stage.peakInstancesPerFrame, m_instanceSamples[stageIndex][sampleIndex]);
            stage.peakPrimitivesPerFrame =
                std::max(stage.peakPrimitivesPerFrame, m_primitiveSamples[stageIndex][sampleIndex]);
            stage.peakScratchBytesPerFrame =
                std::max(stage.peakScratchBytesPerFrame, m_scratchByteSamples[stageIndex][sampleIndex]);
            stage.peakStructureBytesPerFrame =
                std::max(stage.peakStructureBytesPerFrame, m_structureByteSamples[stageIndex][sampleIndex]);
        }
    }

    for (size_t sampleIndex = 0u; sampleIndex < m_sampleCount; ++sampleIndex) {
        stats.peakActiveSceneTlasInstances =
            std::max(stats.peakActiveSceneTlasInstances, m_activeSceneTlasInstanceSamples[sampleIndex]);
        stats.peakActiveSceneTlasBlas =
            std::max(stats.peakActiveSceneTlasBlas, m_activeSceneTlasBlasSamples[sampleIndex]);
        stats.peakActiveTerrainBlas = std::max(stats.peakActiveTerrainBlas, m_activeTerrainBlasSamples[sampleIndex]);
        stats.peakActiveSceneTlasBytes =
            std::max(stats.peakActiveSceneTlasBytes, m_activeSceneTlasByteSamples[sampleIndex]);
        stats.peakActiveSceneReferencedBlasBytes =
            std::max(stats.peakActiveSceneReferencedBlasBytes, m_activeSceneReferencedBlasByteSamples[sampleIndex]);
        stats.peakActiveTerrainBlasBytes =
            std::max(stats.peakActiveTerrainBlasBytes, m_activeTerrainBlasByteSamples[sampleIndex]);
        stats.peakActiveTerrainPrimitiveCount =
            std::max(stats.peakActiveTerrainPrimitiveCount, m_activeTerrainPrimitiveSamples[sampleIndex]);
        stats.sceneTlasGenerationAllocationCount += m_sceneTlasGenerationAllocationSamples[sampleIndex];
        stats.sceneTlasGenerationReuseCount += m_sceneTlasGenerationReuseSamples[sampleIndex];
        stats.sceneTlasGenerationReuseWaitCount += m_sceneTlasGenerationReuseWaitSamples[sampleIndex];
        stats.peakSceneTlasGenerationAllocationsPerFrame = std::max(
            stats.peakSceneTlasGenerationAllocationsPerFrame, m_sceneTlasGenerationAllocationSamples[sampleIndex]);
        stats.peakSceneTlasGenerationReusesPerFrame =
            std::max(stats.peakSceneTlasGenerationReusesPerFrame, m_sceneTlasGenerationReuseSamples[sampleIndex]);
        stats.peakSceneTlasGenerationReuseWaitsPerFrame = std::max(stats.peakSceneTlasGenerationReuseWaitsPerFrame,
                                                                   m_sceneTlasGenerationReuseWaitSamples[sampleIndex]);
        stats.peakRetiredSceneTlasGenerations =
            std::max(stats.peakRetiredSceneTlasGenerations, m_retiredSceneTlasGenerationSamples[sampleIndex]);
        stats.terrainBuckets.builtOpaquePrimitives += m_builtTerrainOpaquePrimitiveSamples[sampleIndex];
        stats.terrainBuckets.builtCutoutPrimitives += m_builtTerrainCutoutPrimitiveSamples[sampleIndex];
        stats.terrainBuckets.peakBuiltOpaquePrimitivesPerFrame = std::max(
            stats.terrainBuckets.peakBuiltOpaquePrimitivesPerFrame, m_builtTerrainOpaquePrimitiveSamples[sampleIndex]);
        stats.terrainBuckets.peakBuiltCutoutPrimitivesPerFrame = std::max(
            stats.terrainBuckets.peakBuiltCutoutPrimitivesPerFrame, m_builtTerrainCutoutPrimitiveSamples[sampleIndex]);
        stats.terrainBuckets.peakActiveOpaquePrimitives = std::max(stats.terrainBuckets.peakActiveOpaquePrimitives,
                                                                   m_activeTerrainOpaquePrimitiveSamples[sampleIndex]);
        stats.terrainBuckets.peakActiveCutoutPrimitives = std::max(stats.terrainBuckets.peakActiveCutoutPrimitives,
                                                                   m_activeTerrainCutoutPrimitiveSamples[sampleIndex]);
        stats.terrainOpacityMicromaps.builtMicromapCount += m_builtTerrainOpacityMicromapSamples[sampleIndex];
        stats.terrainOpacityMicromaps.builtPrimitiveCount += m_builtTerrainOpacityPrimitiveSamples[sampleIndex];
        stats.terrainOpacityMicromaps.buildInputBytes += m_terrainOpacityBuildInputByteSamples[sampleIndex];
        stats.terrainOpacityMicromaps.buildStorageBytes += m_terrainOpacityBuildStorageByteSamples[sampleIndex];
        stats.terrainOpacityMicromaps.buildScratchBytes += m_terrainOpacityBuildScratchByteSamples[sampleIndex];
        stats.terrainOpacityMicromaps.builtOpaqueMicroTriangles +=
            m_builtTerrainOpacityOpaqueMicroTriangleSamples[sampleIndex];
        stats.terrainOpacityMicromaps.builtTransparentMicroTriangles +=
            m_builtTerrainOpacityTransparentMicroTriangleSamples[sampleIndex];
        stats.terrainOpacityMicromaps.builtUnknownMicroTriangles +=
            m_builtTerrainOpacityUnknownMicroTriangleSamples[sampleIndex];
        stats.terrainOpacityMicromaps.peakActiveMicromapCount = std::max(
            stats.terrainOpacityMicromaps.peakActiveMicromapCount, m_activeTerrainOpacityMicromapSamples[sampleIndex]);
        stats.terrainOpacityMicromaps.peakActiveMicromapBytes =
            std::max(stats.terrainOpacityMicromaps.peakActiveMicromapBytes,
                     m_activeTerrainOpacityMicromapByteSamples[sampleIndex]);
        stats.terrainOpacityMicromaps.peakActiveOpaqueMicroTriangles =
            std::max(stats.terrainOpacityMicromaps.peakActiveOpaqueMicroTriangles,
                     m_activeTerrainOpacityOpaqueMicroTriangleSamples[sampleIndex]);
        stats.terrainOpacityMicromaps.peakActiveTransparentMicroTriangles =
            std::max(stats.terrainOpacityMicromaps.peakActiveTransparentMicroTriangles,
                     m_activeTerrainOpacityTransparentMicroTriangleSamples[sampleIndex]);
        stats.terrainOpacityMicromaps.peakActiveUnknownMicroTriangles =
            std::max(stats.terrainOpacityMicromaps.peakActiveUnknownMicroTriangles,
                     m_activeTerrainOpacityUnknownMicroTriangleSamples[sampleIndex]);
        for (size_t index = 0u; index < stats.dynamicBlas.actionCounts.size(); ++index) {
            stats.dynamicBlas.actionCounts[index] += m_dynamicBlasActionSamples[index][sampleIndex];
            stats.dynamicBlas.peakActionsPerFrame[index] =
                std::max(stats.dynamicBlas.peakActionsPerFrame[index], m_dynamicBlasActionSamples[index][sampleIndex]);
        }
        for (size_t index = 0u; index < stats.dynamicBlas.rigidReuseRejectCounts.size(); ++index) {
            stats.dynamicBlas.rigidReuseRejectCounts[index] += m_dynamicBlasRigidReuseRejectSamples[index][sampleIndex];
        }
        for (size_t index = 0u; index < stats.dynamicBlas.updateRejectCounts.size(); ++index) {
            stats.dynamicBlas.updateRejectCounts[index] += m_dynamicBlasUpdateRejectSamples[index][sampleIndex];
        }
    }
    return stats;
}

void RtgiTraceCounterHistory::reset() {
    m_pixelSamples.fill(0u);
    m_candidateSamples.fill(0u);
    m_confirmedSamples.fill(0u);
    m_peakCandidateSamples.fill(0u);
    m_peakConfirmedSamples.fill(0u);
    m_nextSample = 0u;
    m_sampleCount = 0u;
    m_observedSampleCount = 0u;
    m_lastSequence = 0u;
    m_latest = {};
}

bool RtgiTraceCounterHistory::record(const renderer::contracts::RtgiTraceCounterFrameStats& stats) {
    const uint64_t expectedPixels = static_cast<uint64_t>(stats.width) * static_cast<uint64_t>(stats.height);
    if (!stats.supported || !stats.valid || stats.sequence == 0u || stats.sequence <= m_lastSequence ||
        stats.width == 0u || stats.height == 0u || stats.pixelCount != expectedPixels ||
        stats.confirmedCount > stats.candidateCount ||
        stats.peakCandidateCountPerPixel > renderer::contracts::kRtgiTraceValidationCandidateMask ||
        stats.peakConfirmedCountPerPixel > renderer::contracts::kRtgiTraceValidationConfirmedMask ||
        stats.peakConfirmedCountPerPixel > stats.peakCandidateCountPerPixel ||
        (stats.candidateCount == 0u) != (stats.peakCandidateCountPerPixel == 0u) ||
        (stats.confirmedCount == 0u) != (stats.peakConfirmedCountPerPixel == 0u)) {
        return false;
    }

    m_pixelSamples[m_nextSample] = stats.pixelCount;
    m_candidateSamples[m_nextSample] = stats.candidateCount;
    m_confirmedSamples[m_nextSample] = stats.confirmedCount;
    m_peakCandidateSamples[m_nextSample] = stats.peakCandidateCountPerPixel;
    m_peakConfirmedSamples[m_nextSample] = stats.peakConfirmedCountPerPixel;
    m_nextSample = (m_nextSample + 1u) % kCapacity;
    m_sampleCount = std::min(m_sampleCount + 1u, kCapacity);
    ++m_observedSampleCount;
    m_lastSequence = stats.sequence;
    m_latest = stats;
    return true;
}

RtgiTraceCounterWindowStats RtgiTraceCounterHistory::snapshot() const {
    RtgiTraceCounterWindowStats stats;
    stats.sampleCount = m_sampleCount;
    stats.capacity = kCapacity;
    stats.observedSampleCount = m_observedSampleCount;
    stats.latest = m_latest;
    for (size_t index = 0u; index < m_sampleCount; ++index) {
        if (stats.pixelCount > std::numeric_limits<uint64_t>::max() - m_pixelSamples[index] ||
            stats.candidateCount > std::numeric_limits<uint64_t>::max() - m_candidateSamples[index] ||
            stats.confirmedCount > std::numeric_limits<uint64_t>::max() - m_confirmedSamples[index]) {
            return stats;
        }
        stats.pixelCount += m_pixelSamples[index];
        stats.candidateCount += m_candidateSamples[index];
        stats.confirmedCount += m_confirmedSamples[index];
        stats.peakCandidateCountPerPixel = std::max(stats.peakCandidateCountPerPixel, m_peakCandidateSamples[index]);
        stats.peakConfirmedCountPerPixel = std::max(stats.peakConfirmedCountPerPixel, m_peakConfirmedSamples[index]);
    }
    stats.confirmationRate = stats.candidateCount != 0u
                                 ? static_cast<double>(stats.confirmedCount) / static_cast<double>(stats.candidateCount)
                                 : 0.0;
    stats.valid = m_sampleCount != 0u;
    return stats;
}

void RenderGraphTimingHistory::reset() {
    for (auto& samples : m_cpuSamples) {
        samples.fill(0.0);
    }
    for (auto& samples : m_gpuSamples) {
        samples.fill(0.0);
    }
    m_completeGpuFrameSamples.fill(0.0);
    m_cpuNextSample = 0u;
    m_cpuSampleCount = 0u;
    m_gpuNextSample = 0u;
    m_gpuSampleCount = 0u;
    m_observedGpuSampleCount = 0u;
    m_lastGpuExecution = 0u;
    m_completeGpuFrameNextSample = 0u;
    m_completeGpuFrameSampleCount = 0u;
    m_observedCompleteGpuFrameSampleCount = 0u;
    m_lastCompleteGpuFrameSequence = 0u;
    m_latest = {};
}

bool RenderGraphTimingHistory::record(const RenderGraphFrameStats& stats) {
    if (!stats.valid) {
        return false;
    }

    std::array<double, static_cast<size_t>(RenderGraphCpuTimingStage::Count)> cpuValues{};
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphCpuTimingStage::Count); ++index) {
        const double milliseconds =
            renderGraphCpuTimingMilliseconds(stats, static_cast<RenderGraphCpuTimingStage>(index));
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
            return false;
        }
        cpuValues[index] = milliseconds;
    }

    const bool gpuCandidate = stats.execution != 0u && !stats.passes.empty();
    const bool acceptGpu = gpuCandidate && stats.execution > m_lastGpuExecution;
    std::array<double, static_cast<size_t>(RenderGraphGpuTimingMetric::Count)> gpuValues{};
    if (acceptGpu) {
        for (size_t index = 0u; index < static_cast<size_t>(RenderGraphGpuTimingMetric::Count); ++index) {
            const double milliseconds =
                renderGraphGpuTimingMilliseconds(stats, static_cast<RenderGraphGpuTimingMetric>(index));
            if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
                return false;
            }
            gpuValues[index] = milliseconds;
        }
    }

    const bool completeGpuFrameCandidate =
        stats.completeGpuFrame.supported && stats.completeGpuFrame.valid && stats.completeGpuFrame.sequence != 0u;
    const bool acceptCompleteGpuFrame =
        completeGpuFrameCandidate && stats.completeGpuFrame.sequence > m_lastCompleteGpuFrameSequence;
    if (completeGpuFrameCandidate &&
        (!std::isfinite(stats.completeGpuFrame.spanMs) || stats.completeGpuFrame.spanMs < 0.0)) {
        return false;
    }

    for (size_t index = 0u; index < cpuValues.size(); ++index) {
        m_cpuSamples[index][m_cpuNextSample] = cpuValues[index];
    }
    m_cpuNextSample = (m_cpuNextSample + 1u) % kCapacity;
    m_cpuSampleCount = std::min(m_cpuSampleCount + 1u, kCapacity);

    m_latest.valid = true;
    m_latest.execution = stats.execution;
    m_latest.passCount = stats.passCount;
    m_latest.batchCount = stats.batchCount;
    m_latest.submitCount = stats.submitCount;
    m_latest.workerRecordedBatches = stats.workerRecordedBatches;
    m_latest.gpuValid = gpuCandidate;
    m_latest.completeGpuFrameValid = completeGpuFrameCandidate;
    m_latest.completeGpuFrameSequence = stats.completeGpuFrame.sequence;
    m_latest.completeGpuFrameSpanMs = stats.completeGpuFrame.spanMs;

    if (acceptGpu) {
        for (size_t index = 0u; index < gpuValues.size(); ++index) {
            m_gpuSamples[index][m_gpuNextSample] = gpuValues[index];
        }
        m_gpuNextSample = (m_gpuNextSample + 1u) % kCapacity;
        m_gpuSampleCount = std::min(m_gpuSampleCount + 1u, kCapacity);
        ++m_observedGpuSampleCount;
        m_lastGpuExecution = stats.execution;
    }
    if (acceptCompleteGpuFrame) {
        m_completeGpuFrameSamples[m_completeGpuFrameNextSample] = stats.completeGpuFrame.spanMs;
        m_completeGpuFrameNextSample = (m_completeGpuFrameNextSample + 1u) % kCapacity;
        m_completeGpuFrameSampleCount = std::min(m_completeGpuFrameSampleCount + 1u, kCapacity);
        ++m_observedCompleteGpuFrameSampleCount;
        m_lastCompleteGpuFrameSequence = stats.completeGpuFrame.sequence;
    }
    return true;
}

RenderGraphTimingWindowStats RenderGraphTimingHistory::snapshot() const {
    RenderGraphTimingWindowStats stats;
    stats.cpuValid = m_cpuSampleCount > 0u;
    stats.gpuValid = m_gpuSampleCount > 0u;
    stats.capacity = kCapacity;
    stats.cpuSampleCount = m_cpuSampleCount;
    stats.gpuSampleCount = m_gpuSampleCount;
    stats.observedGpuSampleCount = m_observedGpuSampleCount;
    stats.completeGpuFrameValid = m_completeGpuFrameSampleCount > 0u;
    stats.completeGpuFrameSampleCount = m_completeGpuFrameSampleCount;
    stats.observedCompleteGpuFrameSampleCount = m_observedCompleteGpuFrameSampleCount;
    stats.completeGpuFrameSpanMs = calculatePercentiles(m_completeGpuFrameSamples, m_completeGpuFrameSampleCount);
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphCpuTimingStage::Count); ++index) {
        stats.cpu[index] = calculatePercentiles(m_cpuSamples[index], m_cpuSampleCount);
    }
    for (size_t index = 0u; index < static_cast<size_t>(RenderGraphGpuTimingMetric::Count); ++index) {
        stats.gpu[index] = calculatePercentiles(m_gpuSamples[index], m_gpuSampleCount);
    }
    stats.latest = m_latest;
    return stats;
}

uint32_t RenderDebugService::gpuTimerQueryIndex(const size_t frameIndex, const size_t passIndex,
                                                const size_t segmentIndex, const size_t pointIndex) {
    return static_cast<uint32_t>(
        (((frameIndex * GPU_TIMER_PASS_COUNT + passIndex) * GPU_TIMER_MAX_SEGMENTS_PER_PASS + segmentIndex) *
         GPU_TIMER_POINTS_PER_SEGMENT) +
        pointIndex);
}

uint32_t RenderDebugService::gpuFrameSpanQueryIndex(const size_t frameIndex, const GpuFrameSpanPoint point) {
    return static_cast<uint32_t>(frameIndex * GPU_FRAME_SPAN_POINT_COUNT + static_cast<size_t>(point));
}

uint32_t RenderDebugService::shadowQueryIndex(const size_t frameIndex, const size_t cascadeIndex,
                                              const size_t pointIndex) {
    return static_cast<uint32_t>((frameIndex * SHADOW_TIMER_CASCADE_COUNT + cascadeIndex) * SHADOW_TIMER_POINT_COUNT +
                                 pointIndex);
}

void RenderDebugService::init(RhiDevice& rhiDevice) {
    if (m_gpuTimersInitialized) {
        return;
    }

    RhiQueryPoolDesc gpuTimerPoolDesc;
    gpuTimerPoolDesc.debugName = "RenderDebug.PassTimestamps";
    gpuTimerPoolDesc.queryCount = GPU_TIMER_QUERY_COUNT;
    m_gpuTimerQueryPool = rhiDevice.createQueryPool(gpuTimerPoolDesc);
    if (!m_gpuTimerQueryPool.isValid()) {
        std::abort();
    }

    RhiQueryPoolDesc gpuFrameSpanPoolDesc;
    gpuFrameSpanPoolDesc.debugName = "RenderDebug.FrameSpanTimestamps";
    gpuFrameSpanPoolDesc.queryCount = GPU_FRAME_SPAN_QUERY_COUNT;
    m_gpuFrameSpanQueryPool = rhiDevice.createQueryPool(gpuFrameSpanPoolDesc);
    if (!m_gpuFrameSpanQueryPool.isValid()) {
        std::abort();
    }

    RhiQueryPoolDesc shadowPoolDesc;
    shadowPoolDesc.debugName = "RenderDebug.ShadowTimestamps";
    shadowPoolDesc.queryCount = SHADOW_TIMER_QUERY_COUNT;
    m_shadowTimestampQueryPool = rhiDevice.createQueryPool(shadowPoolDesc);
    if (!m_shadowTimestampQueryPool.isValid()) {
        std::abort();
    }
    m_rhiDevice = &rhiDevice;
    for (auto& counts : m_gpuTimerAllocatedSegmentCounts) {
        counts.fill(0u);
    }
    for (auto& frame : m_gpuTimerSegmentStates) {
        for (auto& pass : frame) {
            pass.fill(GpuTimerSegmentState::Unused);
        }
    }
    m_gpuFrameSpanStates.fill(GpuFrameSpanState::Unused);
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuFrameStats = {};
    m_gpuFrameStats.supported = true;
    m_gpuFrameSequence = 0u;
    m_gpuFrameSpanStats = {};
    m_gpuFrameSpanStats.supported = true;
    m_gpuFrameSpanSequence = 0u;
    m_gpuTimingHistory.reset();
    m_shadowFrameStats.supported = true;
    m_gpuTimersInitialized = true;
}

void RenderDebugService::shutdown() {
    if (!m_gpuTimersInitialized) {
        return;
    }
    if (m_rhiDevice != nullptr && m_gpuTimerQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_gpuTimerQueryPool);
    }
    m_gpuTimerQueryPool = {};
    if (m_rhiDevice != nullptr && m_gpuFrameSpanQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_gpuFrameSpanQueryPool);
    }
    m_gpuFrameSpanQueryPool = {};
    if (m_rhiDevice != nullptr && m_shadowTimestampQueryPool.isValid()) {
        m_rhiDevice->destroyQueryPool(m_shadowTimestampQueryPool);
    }
    m_shadowTimestampQueryPool = {};
    m_rhiDevice = nullptr;
    for (auto& counts : m_gpuTimerAllocatedSegmentCounts) {
        counts.fill(0u);
    }
    for (auto& frame : m_gpuTimerSegmentStates) {
        for (auto& pass : frame) {
            pass.fill(GpuTimerSegmentState::Unused);
        }
    }
    m_gpuFrameSpanStates.fill(GpuFrameSpanState::Unused);
    for (auto& frame : m_shadowTimestampIssued) {
        for (auto& cascade : frame) {
            cascade.fill(false);
        }
    }
    m_shadowFrameIssued.fill(false);
    m_gpuTimersInitialized = false;
    m_gpuFrameStats = {};
    m_gpuFrameSequence = 0u;
    m_gpuFrameSpanStats = {};
    m_gpuFrameSpanSequence = 0u;
    m_gpuTimingHistory.reset();
    m_shadowFrameStats.valid = false;
    m_shadowFrameActive = false;
}

void RenderDebugService::beginFrame(RhiCommandList& commandList) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled) {
        m_gpuFrameStats.supported = m_gpuTimersInitialized && m_gpuFrameStats.supported;
        m_gpuFrameStats.valid = false;
        m_gpuFrameSpanStats.supported = m_gpuTimersInitialized && m_gpuFrameSpanStats.supported;
        m_gpuFrameSpanStats.valid = false;
        m_gpuFrameSpanStats.sequence = 0u;
        m_gpuFrameSpanStats.spanMs = 0.0;
        m_shadowFrameStats.supported = m_gpuFrameStats.supported;
        m_shadowFrameStats.valid = false;
        m_gpuTimerCanIssueThisFrame = false;
        return;
    }

    m_shadowFrameStats.supported = true;
    m_gpuFrameSpanStats.supported = true;
    m_gpuFrameSpanStats.valid = false;
    m_gpuFrameSpanStats.sequence = 0u;
    m_gpuFrameSpanStats.spanMs = 0.0;

    const size_t readIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    bool anyPassIssued = false;
    for (const auto& pass : m_gpuTimerSegmentStates[readIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            anyPassIssued = anyPassIssued || state == GpuTimerSegmentState::Issued;
        }
    }

    if (anyPassIssued) {
        bool allAvailable = true;
        for (size_t pass = 0; pass < GPU_TIMER_PASS_COUNT; ++pass) {
            for (size_t segment = 0; segment < GPU_TIMER_MAX_SEGMENTS_PER_PASS; ++segment) {
                if (m_gpuTimerSegmentStates[readIndex][pass][segment] != GpuTimerSegmentState::Issued) {
                    continue;
                }
                const uint32_t firstQuery = gpuTimerQueryIndex(readIndex, pass, segment, 0u);
                if (!m_rhiDevice->areQueryResultsAvailable(m_gpuTimerQueryPool, firstQuery,
                                                           static_cast<uint32_t>(GPU_TIMER_POINTS_PER_SEGMENT))) {
                    allAvailable = false;
                    break;
                }
            }
            if (!allAvailable) {
                break;
            }
        }

        if (allAvailable) {
            const auto readMs = [&](const GpuTimerPass pass) {
                const size_t passIndex = static_cast<size_t>(pass);
                uint64_t elapsedNs = 0u;
                for (size_t segment = 0; segment < GPU_TIMER_MAX_SEGMENTS_PER_PASS; ++segment) {
                    if (m_gpuTimerSegmentStates[readIndex][passIndex][segment] != GpuTimerSegmentState::Issued) {
                        continue;
                    }
                    std::array<uint64_t, GPU_TIMER_POINTS_PER_SEGMENT> timestamps{};
                    const uint32_t firstQuery = gpuTimerQueryIndex(readIndex, passIndex, segment, 0u);
                    if (!m_rhiDevice->getQueryResults(m_gpuTimerQueryPool, firstQuery,
                                                      static_cast<uint32_t>(GPU_TIMER_POINTS_PER_SEGMENT),
                                                      timestamps.data())) {
                        std::abort();
                    }
                    const uint64_t start = timestamps[0];
                    const uint64_t end = timestamps[1];
                    if (end < start) {
                        std::abort();
                    }
                    elapsedNs += end - start;
                }
                return static_cast<double>(elapsedNs) / 1000000.0;
            };

            m_gpuFrameStats.supported = true;
            m_gpuFrameStats.valid = true;
            m_gpuFrameStats.gbufferMs = readMs(GpuTimerPass::GBuffer);
            m_gpuFrameStats.shadowMs = readMs(GpuTimerPass::Shadow);
            m_gpuFrameStats.ssaoMs = readMs(GpuTimerPass::Ssao);
            m_gpuFrameStats.ssgiMs = readMs(GpuTimerPass::Ssgi);
            m_gpuFrameStats.rtgiTraceMs = readMs(GpuTimerPass::RtgiTrace);
            m_gpuFrameStats.rtgiSignalPackMs = readMs(GpuTimerPass::RtgiSignalPack);
            m_gpuFrameStats.nrdGuidePrepMs = readMs(GpuTimerPass::NrdGuidePrep);
            m_gpuFrameStats.nrdDispatchMs = readMs(GpuTimerPass::NrdDispatch);
            m_gpuFrameStats.sceneTlasMs = readMs(GpuTimerPass::SceneTlas);
            m_gpuFrameStats.terrainBlasBuildMs = readMs(GpuTimerPass::TerrainBlasBuild);
            m_gpuFrameStats.terrainBlasCompactionMs = readMs(GpuTimerPass::TerrainBlasCompaction);
            m_gpuFrameStats.accelerationStructureDynamicPrepareMs =
                readMs(GpuTimerPass::AccelerationStructureDynamicPrepare);
            m_gpuFrameStats.rtgiSceneTlasBootstrapMs = readMs(GpuTimerPass::RtgiSceneTlasBootstrap);
            m_gpuFrameStats.rtgiMs = m_gpuFrameStats.rtgiTraceMs + m_gpuFrameStats.rtgiSignalPackMs;
            m_gpuFrameStats.nrdMs = m_gpuFrameStats.nrdGuidePrepMs + m_gpuFrameStats.nrdDispatchMs;
            m_gpuFrameStats.lightingMs = readMs(GpuTimerPass::Lighting);
            m_gpuFrameStats.transparentMs = readMs(GpuTimerPass::Transparent);
            m_gpuFrameStats.volumetricMs = readMs(GpuTimerPass::Volumetric);
            m_gpuFrameStats.reflectionMs = readMs(GpuTimerPass::Reflection);
            m_gpuFrameStats.cloudMs = readMs(GpuTimerPass::Cloud);
            m_gpuFrameStats.waterMs = readMs(GpuTimerPass::Water);
            m_gpuFrameStats.postMs = readMs(GpuTimerPass::Post);
            m_gpuFrameStats.sequence = ++m_gpuFrameSequence;
            if (!m_gpuTimingHistory.record(m_gpuFrameStats)) {
                std::abort();
            }
            m_gpuTimerAllocatedSegmentCounts[readIndex].fill(0u);
            for (auto& pass : m_gpuTimerSegmentStates[readIndex]) {
                pass.fill(GpuTimerSegmentState::Unused);
            }
        }
    }

    if (m_shadowFrameIssued[readIndex]) {
        const ShadowFrameStats& pendingStats = m_shadowFrameSlots[readIndex];
        const int cascadeCount =
            std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT), std::max(0, pendingStats.cascadeCount));
        const uint32_t firstQuery = shadowQueryIndex(readIndex, 0u, 0u);
        const uint32_t queryCount =
            static_cast<uint32_t>(cascadeCount) * static_cast<uint32_t>(SHADOW_TIMER_POINT_COUNT);
        const bool allAvailable = queryCount > 0u && m_rhiDevice->areQueryResultsAvailable(m_shadowTimestampQueryPool,
                                                                                           firstQuery, queryCount);

        if (allAvailable) {
            std::array<uint64_t, SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT> timestamps{};
            if (!m_rhiDevice->getQueryResults(m_shadowTimestampQueryPool, firstQuery, queryCount, timestamps.data())) {
                std::abort();
            }
            ShadowFrameStats stats = pendingStats;
            stats.supported = true;
            stats.valid = true;
            stats.gpuTotalMs = 0.0;
            for (int cascade = 0; cascade < cascadeCount; ++cascade) {
                const auto readTimestamp = [&](const ShadowTimestampPoint point) {
                    const size_t localIndex =
                        static_cast<size_t>(cascade) * SHADOW_TIMER_POINT_COUNT + static_cast<size_t>(point);
                    return timestamps[localIndex];
                };
                const uint64_t start = readTimestamp(ShadowTimestampPoint::Start);
                const uint64_t opaqueEnd = readTimestamp(ShadowTimestampPoint::OpaqueEnd);
                const uint64_t end = readTimestamp(ShadowTimestampPoint::End);
                ShadowCascadeStats& cascadeStats = stats.cascades[static_cast<size_t>(cascade)];
                if (start > 0 && opaqueEnd >= start) {
                    cascadeStats.gpuOpaqueMs = static_cast<double>(opaqueEnd - start) / 1000000.0;
                }
                if (end > 0 && end >= opaqueEnd) {
                    cascadeStats.gpuTransparentMs = static_cast<double>(end - opaqueEnd) / 1000000.0;
                }
                if (end > 0 && end >= start) {
                    cascadeStats.gpuTotalMs = static_cast<double>(end - start) / 1000000.0;
                }
                stats.gpuTotalMs += cascadeStats.gpuTotalMs;
            }
            m_shadowFrameStats = stats;
            // Disabled verbose shadow stats logging
            // if (++m_shadowStatsPublishCount % 120 == 0) {
            //     MECRAFT_LOG_PRINTF("[shadow:csm:gpu] total=%.3fms res=%d submitted=%d culled=%d maxDist=%.1f cascades=%d\n",
            //                        stats.gpuTotalMs,
            //                        stats.shadowResolution,
            //                        stats.submitted,
            //                        stats.culled,
            //                        stats.maxCasterDistance,
            //                        stats.cascadeCount);
            //     for (int cascade = 0; cascade < cascadeCount; ++cascade) {
            //         const ShadowCascadeStats& cascadeStats = stats.cascades[static_cast<size_t>(cascade)];
            //         MECRAFT_LOG_PRINTF("[shadow:csm:gpu:c%d] total=%.3fms opaque=%.3fms transparent=%.3fms split=%.1f-%.1f texel=%.4f cmds(o=%zu c=%zu t=%zu) verts(o=%llu c=%llu t=%llu)\n",
            //                            cascade,
            //                            cascadeStats.gpuTotalMs,
            //                            cascadeStats.gpuOpaqueMs,
            //                            cascadeStats.gpuTransparentMs,
            //                            cascadeStats.splitNear,
            //                            cascadeStats.splitFar,
            //                            cascadeStats.texelWorldSize,
            //                            cascadeStats.opaqueCommands,
            //                            cascadeStats.cutoutCommands,
            //                            cascadeStats.transparentCommands,
            //                            static_cast<unsigned long long>(cascadeStats.opaqueVertices),
            //                            static_cast<unsigned long long>(cascadeStats.cutoutVertices),
            //                            static_cast<unsigned long long>(cascadeStats.transparentVertices));
            //     }
            // }
            for (auto& cascade : m_shadowTimestampIssued[readIndex]) {
                cascade.fill(false);
            }
            m_shadowFrameIssued[readIndex] = false;
        }
    }

    const GpuFrameSpanState spanState = m_gpuFrameSpanStates[readIndex];
    if (spanState == GpuFrameSpanState::Issued) {
        const uint32_t firstQuery = gpuFrameSpanQueryIndex(readIndex, GpuFrameSpanPoint::Start);
        if (m_rhiDevice->areQueryResultsAvailable(m_gpuFrameSpanQueryPool, firstQuery,
                                                  static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT))) {
            std::array<uint64_t, GPU_FRAME_SPAN_POINT_COUNT> timestamps{};
            if (!m_rhiDevice->getQueryResults(m_gpuFrameSpanQueryPool, firstQuery,
                                              static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT), timestamps.data())) {
                std::abort();
            }
            const uint64_t start = timestamps[static_cast<size_t>(GpuFrameSpanPoint::Start)];
            const uint64_t end = timestamps[static_cast<size_t>(GpuFrameSpanPoint::End)];
            if (end < start) {
                std::abort();
            }
            m_gpuFrameSpanStats.valid = true;
            m_gpuFrameSpanStats.sequence = ++m_gpuFrameSpanSequence;
            m_gpuFrameSpanStats.spanMs = static_cast<double>(end - start) / 1000000.0;
            m_gpuFrameSpanStates[readIndex] = GpuFrameSpanState::Unused;
        }
    } else if (spanState == GpuFrameSpanState::Started) {
        // A frame that reached the ring without a terminal marker is incomplete.
        m_gpuFrameSpanStates[readIndex] = GpuFrameSpanState::Unused;
    }

    bool slotStillPending = false;
    for (const auto& pass : m_gpuTimerSegmentStates[readIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            if (state == GpuTimerSegmentState::Recording) {
                std::abort();
            }
            if (state == GpuTimerSegmentState::Issued) {
                slotStillPending = true;
            }
        }
    }
    if (m_shadowFrameIssued[readIndex]) {
        slotStillPending = true;
    }
    if (m_gpuFrameSpanStates[readIndex] == GpuFrameSpanState::Issued) {
        slotStillPending = true;
    }
    m_gpuTimerCanIssueThisFrame = !slotStillPending;
    if (!m_gpuTimerCanIssueThisFrame) {
        return;
    }

    for (const auto& pass : m_gpuTimerSegmentStates[m_gpuTimerWriteIndex]) {
        for (const GpuTimerSegmentState state : pass) {
            if (state == GpuTimerSegmentState::Recording) {
                std::abort();
            }
        }
    }
    m_gpuTimerWriteIndex = (m_gpuTimerWriteIndex + 1) % GPU_TIMER_RING_SIZE;
    m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex].fill(0u);
    for (auto& pass : m_gpuTimerSegmentStates[m_gpuTimerWriteIndex]) {
        pass.fill(GpuTimerSegmentState::Unused);
    }
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = ShadowFrameStats{};
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = false;
    m_shadowFrameActive = false;
    if (m_gpuFrameSpanStates[m_gpuTimerWriteIndex] == GpuFrameSpanState::Started) {
        std::abort();
    }
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Unused;

    const uint32_t timerSlotFirstQuery = gpuTimerQueryIndex(m_gpuTimerWriteIndex, 0u, 0u, 0u);
    const uint32_t timerSlotQueryCount =
        static_cast<uint32_t>(GPU_TIMER_PASS_COUNT * GPU_TIMER_MAX_SEGMENTS_PER_PASS * GPU_TIMER_POINTS_PER_SEGMENT);
    commandList.resetQueryPool(m_gpuTimerQueryPool, timerSlotFirstQuery, timerSlotQueryCount);

    const uint32_t shadowSlotFirstQuery = shadowQueryIndex(m_gpuTimerWriteIndex, 0u, 0u);
    const uint32_t shadowSlotQueryCount = static_cast<uint32_t>(SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT);
    commandList.resetQueryPool(m_shadowTimestampQueryPool, shadowSlotFirstQuery, shadowSlotQueryCount);

    const uint32_t spanSlotFirstQuery = gpuFrameSpanQueryIndex(m_gpuTimerWriteIndex, GpuFrameSpanPoint::Start);
    commandList.resetQueryPool(m_gpuFrameSpanQueryPool, spanSlotFirstQuery,
                               static_cast<uint32_t>(GPU_FRAME_SPAN_POINT_COUNT));
    commandList.writeTimestamp(m_gpuFrameSpanQueryPool, spanSlotFirstQuery);
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Started;
}

void RenderDebugService::endGpuFrameSpan(RhiCommandList& commandList) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (m_gpuFrameSpanStates[m_gpuTimerWriteIndex] != GpuFrameSpanState::Started) {
        std::abort();
    }
    const uint32_t endQuery = gpuFrameSpanQueryIndex(m_gpuTimerWriteIndex, GpuFrameSpanPoint::End);
    commandList.writeTimestamp(m_gpuFrameSpanQueryPool, endQuery);
    m_gpuFrameSpanStates[m_gpuTimerWriteIndex] = GpuFrameSpanState::Issued;
}

void RenderDebugService::cancelGpuFrameSpan() {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    GpuFrameSpanState& state = m_gpuFrameSpanStates[m_gpuTimerWriteIndex];
    if (state == GpuFrameSpanState::Started || state == GpuFrameSpanState::Issued) {
        state = GpuFrameSpanState::Unused;
    }
}

GpuTimerSegmentToken RenderDebugService::beginGpuTimer(RhiCommandList& commandList, const GpuTimerPass pass) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(pass);
    if (passIndex >= GPU_TIMER_PASS_COUNT) {
        std::abort();
    }
    const size_t segmentIndex = m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
    if (segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS) {
        std::abort();
    }
    m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] = static_cast<uint8_t>(segmentIndex + 1u);
    const uint32_t firstQuery = gpuTimerQueryIndex(m_gpuTimerWriteIndex, passIndex, segmentIndex, 0u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, firstQuery);
    m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segmentIndex] = GpuTimerSegmentState::Recording;
    return {pass, static_cast<uint8_t>(m_gpuTimerWriteIndex), static_cast<uint8_t>(segmentIndex), true};
}

void RenderDebugService::endGpuTimer(RhiCommandList& commandList, const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex || passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] != GpuTimerSegmentState::Recording) {
        std::abort();
    }
    const uint32_t endQuery = gpuTimerQueryIndex(token.frameIndex, passIndex, token.segmentIndex, 1u);
    commandList.writeTimestamp(m_gpuTimerQueryPool, endQuery);
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] = GpuTimerSegmentState::Issued;
}

void RenderDebugService::cancelGpuTimer(const GpuTimerSegmentToken token) {
    if (!token.valid) {
        return;
    }

    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    const size_t passIndex = static_cast<size_t>(token.pass);
    if (!m_gpuTimersInitialized || token.frameIndex != m_gpuTimerWriteIndex || passIndex >= GPU_TIMER_PASS_COUNT ||
        token.segmentIndex >= GPU_TIMER_MAX_SEGMENTS_PER_PASS ||
        m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] != GpuTimerSegmentState::Recording) {
        std::abort();
    }
    m_gpuTimerSegmentStates[token.frameIndex][passIndex][token.segmentIndex] = GpuTimerSegmentState::Unused;
}

GpuTimerCheckpoint RenderDebugService::gpuTimerCheckpoint() const {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame) {
        return {};
    }
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    return {m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex], static_cast<uint8_t>(m_gpuTimerWriteIndex), true};
}

void RenderDebugService::cancelGpuTimersSince(const GpuTimerCheckpoint& checkpoint) {
    if (!checkpoint.valid) {
        return;
    }
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (!m_gpuTimersInitialized || checkpoint.frameIndex != m_gpuTimerWriteIndex) {
        std::abort();
    }
    for (size_t passIndex = 0; passIndex < GPU_TIMER_PASS_COUNT; ++passIndex) {
        const size_t firstSegment = checkpoint.segmentCounts[passIndex];
        const size_t allocatedSegments = m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex];
        if (firstSegment > allocatedSegments) {
            std::abort();
        }
        for (size_t segment = firstSegment; segment < allocatedSegments; ++segment) {
            GpuTimerSegmentState& state = m_gpuTimerSegmentStates[m_gpuTimerWriteIndex][passIndex][segment];
            state = GpuTimerSegmentState::Unused;
        }
        m_gpuTimerAllocatedSegmentCounts[m_gpuTimerWriteIndex][passIndex] = checkpoint.segmentCounts[passIndex];
    }
}

bool RenderDebugService::beginShadowFrame(const int cascadeCount, const int shadowResolution) {
    if (!m_gpuTimersInitialized || !m_gpuTimerEnabled || !m_gpuTimerCanIssueThisFrame || m_shadowFrameActive) {
        return false;
    }

    ShadowFrameStats stats;
    stats.supported = true;
    stats.valid = false;
    stats.cascadeCount = std::min(static_cast<int>(SHADOW_TIMER_CASCADE_COUNT), std::max(0, cascadeCount));
    stats.shadowResolution = shadowResolution;
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = stats;
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameActive = true;
    return true;
}

void RenderDebugService::recordShadowCascadeStats(const int cascadeIndex, const ShadowCascadeStats& stats) {
    if (!m_shadowFrameActive || cascadeIndex < 0 || cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.cascades[static_cast<size_t>(cascadeIndex)] = stats;
}

void RenderDebugService::recordShadowFrameTotals(const int submitted, const int culled, const float maxCasterDistance) {
    if (!m_shadowFrameActive) {
        return;
    }

    ShadowFrameStats& frame = m_shadowFrameSlots[m_gpuTimerWriteIndex];
    frame.submitted = submitted;
    frame.culled = culled;
    frame.maxCasterDistance = maxCasterDistance;
}

void RenderDebugService::markShadowTimestamp(RhiCommandList& commandList, const int cascadeIndex,
                                             const ShadowTimestampPoint point) {
    const std::lock_guard<std::mutex> timerLock(m_gpuTimerMutex);
    if (!m_shadowFrameActive || cascadeIndex < 0 || cascadeIndex >= static_cast<int>(SHADOW_TIMER_CASCADE_COUNT)) {
        return;
    }
    const size_t pointIndex = static_cast<size_t>(point);
    if (pointIndex >= SHADOW_TIMER_POINT_COUNT) {
        return;
    }

    const uint32_t queryIndex = shadowQueryIndex(m_gpuTimerWriteIndex, static_cast<size_t>(cascadeIndex), pointIndex);
    commandList.writeTimestamp(m_shadowTimestampQueryPool, queryIndex);
    m_shadowTimestampIssued[m_gpuTimerWriteIndex][static_cast<size_t>(cascadeIndex)][pointIndex] = true;
}

void RenderDebugService::endShadowFrame() {
    if (!m_shadowFrameActive) {
        return;
    }

    const int cascadeCount = m_shadowFrameSlots[m_gpuTimerWriteIndex].cascadeCount;
    for (int cascade = 0; cascade < cascadeCount; ++cascade) {
        for (size_t point = 0; point < SHADOW_TIMER_POINT_COUNT; ++point) {
            if (!m_shadowTimestampIssued[m_gpuTimerWriteIndex][static_cast<size_t>(cascade)][point]) {
                std::abort();
            }
        }
    }
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = true;
    m_shadowFrameActive = false;
}

void RenderDebugService::cancelShadowFrame() {
    if (!m_shadowFrameActive) {
        return;
    }
    for (auto& cascade : m_shadowTimestampIssued[m_gpuTimerWriteIndex]) {
        cascade.fill(false);
    }
    m_shadowFrameSlots[m_gpuTimerWriteIndex] = ShadowFrameStats{};
    m_shadowFrameIssued[m_gpuTimerWriteIndex] = false;
    m_shadowFrameActive = false;
}

void RenderDebugService::resetCullingStats() {
    m_cullingStats = CullingFrameStats{};
}

void RenderDebugService::recordRegionCull(bool passed) {
    ++m_cullingStats.regionTests;
    if (passed) {
        ++m_cullingStats.regionPassed;
    }
}

void RenderDebugService::recordColumnCull(bool passed) {
    ++m_cullingStats.columnTests;
    if (passed) {
        ++m_cullingStats.columnPassed;
    }
}

void RenderDebugService::recordChunkCull(bool passed, FrustumPlane culledPlane) {
    ++m_cullingStats.chunkTests;
    if (passed) {
        ++m_cullingStats.chunkPassed;
    } else {
        ++m_cullingStats.chunkCulled;
        if (culledPlane != FrustumPlane::Count) {
            ++m_cullingStats.chunkCulledByPlane[static_cast<size_t>(culledPlane)];
        }
    }
}
