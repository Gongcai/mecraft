#include "renderer/debug/RenderDebugService.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool requireTrue(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[gpu_timing_history_test] FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool requireNear(const double actual, const double expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-9) {
        std::cerr << "[gpu_timing_history_test] FAIL: " << message << " actual=" << actual << " expected=" << expected
                  << '\n';
        return false;
    }
    return true;
}

bool testStableStageNames() {
    const char* expected[] = {"GBuffer",
                              "Shadow",
                              "SSAO",
                              "SSGI",
                              "RTGI",
                              "NRD",
                              "Lighting",
                              "Transparent",
                              "Volumetric",
                              "Reflection",
                              "Cloud",
                              "Water",
                              "Post",
                              "RTGI.Trace",
                              "RTGI.SignalPack",
                              "NRD.GuidePrep",
                              "NRD.Dispatch",
                              "SceneTLAS",
                              "TerrainBLAS.Build",
                              "TerrainBLAS.Compaction",
                              "AS.DynamicResourcePreparation",
                              "RTGI.SceneTLASBootstrap"};
    for (size_t index = 0u; index < static_cast<size_t>(GpuTimerPass::Count); ++index) {
        if (!requireTrue(std::string(gpuTimerPassName(static_cast<GpuTimerPass>(index))) == expected[index],
                         "GPU stage names must remain stable")) {
            return false;
        }
    }
    return true;
}

bool testFixedWindowPercentiles() {
    GpuTimingHistory history;
    const GpuTimingWindowStats empty = history.snapshot();
    if (!requireTrue(!empty.valid && empty.sampleCount == 0u && empty.capacity == GpuTimingHistory::kCapacity,
                     "empty history must expose its fixed capacity")) {
        return false;
    }

    GpuFrameStats invalid;
    invalid.supported = true;
    invalid.sequence = 1u;
    if (!requireTrue(!history.record(invalid), "incomplete GPU frames must be rejected")) {
        return false;
    }

    GpuFrameStats frame;
    frame.supported = true;
    frame.valid = true;
    for (uint64_t sequence = 1u; sequence <= 1002u; ++sequence) {
        frame.sequence = sequence;
        frame.gbufferMs = static_cast<double>(sequence);
        frame.shadowMs = static_cast<double>(sequence) * 2.0;
        frame.rtgiMs = static_cast<double>(sequence) * 0.5;
        frame.nrdMs = static_cast<double>(sequence) * 0.25;
        frame.sceneTlasMs = static_cast<double>(sequence) * 0.1;
        frame.terrainBlasBuildMs = static_cast<double>(sequence) * 0.2;
        frame.terrainBlasCompactionMs = static_cast<double>(sequence) * 0.3;
        frame.accelerationStructureDynamicPrepareMs = static_cast<double>(sequence) * 0.4;
        frame.rtgiSceneTlasBootstrapMs = static_cast<double>(sequence) * 10.0;
        if (!requireTrue(history.record(frame), "unique completed GPU frames must be recorded")) {
            return false;
        }
    }
    if (!requireTrue(!history.record(frame), "duplicate GPU frame sequences must be rejected")) {
        return false;
    }
    frame.sequence = 1001u;
    if (!requireTrue(!history.record(frame), "stale GPU frame sequences must be rejected")) {
        return false;
    }
    frame.sequence = 1003u;
    frame.gbufferMs = -1.0;
    if (!requireTrue(!history.record(frame), "negative GPU durations must be rejected")) {
        return false;
    }

    const GpuTimingWindowStats stats = history.snapshot();
    if (!requireTrue(stats.valid && stats.sampleCount == 1000u && stats.observedSampleCount == 1002u,
                     "history must retain the latest fixed-size window")) {
        return false;
    }

    const auto& gbuffer = stats.passes[static_cast<size_t>(GpuTimerPass::GBuffer)].gpuMs;
    const auto& shadow = stats.passes[static_cast<size_t>(GpuTimerPass::Shadow)].gpuMs;
    const auto& rtgi = stats.passes[static_cast<size_t>(GpuTimerPass::Rtgi)].gpuMs;
    const auto& nrd = stats.passes[static_cast<size_t>(GpuTimerPass::Nrd)].gpuMs;
    const auto& sceneTlas = stats.passes[static_cast<size_t>(GpuTimerPass::SceneTlas)].gpuMs;
    return requireNear(gbuffer.p50Ms, 502.0, "GBuffer p50 must use nearest-rank selection") &&
           requireNear(gbuffer.p95Ms, 952.0, "GBuffer p95 must use nearest-rank selection") &&
           requireNear(gbuffer.p99Ms, 992.0, "GBuffer p99 must use nearest-rank selection") &&
           requireNear(shadow.p50Ms, 1004.0, "Shadow p50 must preserve stage values") &&
           requireNear(shadow.p95Ms, 1904.0, "Shadow p95 must preserve stage values") &&
           requireNear(shadow.p99Ms, 1984.0, "Shadow p99 must preserve stage values") &&
           requireNear(rtgi.p50Ms, 251.0, "RTGI p50 must preserve stage values") &&
           requireNear(nrd.p95Ms, 238.0, "NRD p95 must preserve stage values") &&
           requireNear(sceneTlas.p95Ms, 95.2, "Scene TLAS p95 must preserve stage values") &&
           requireNear(stats.totalTrackedGpuMs.p50Ms, 2384.5, "tracked total p50 must include disjoint AS work") &&
           requireNear(stats.totalTrackedGpuMs.p95Ms, 4522.0, "tracked total p95 must exclude bootstrap overlap") &&
           requireNear(stats.totalTrackedGpuMs.p99Ms, 4712.0, "tracked total p99 must exclude bootstrap overlap");
}

bool testAccelerationStructureHistory() {
    AccelerationStructureHistory history;
    const AccelerationStructureWindowStats empty = history.snapshot();
    if (!requireTrue(!empty.valid && empty.sampleCount == 0u &&
                         empty.capacity == AccelerationStructureHistory::kCapacity,
                     "empty AS history must expose its fixed capacity")) {
        return false;
    }

    AccelerationStructureFrameStats frame;
    frame.supported = true;
    frame.valid = true;
    frame.staticBlas.supported = true;
    frame.staticBlas.assetCount = 2u;
    frame.staticBlas.residentAssetCount = 2u;
    frame.staticBlas.buildCount = 2u;
    frame.staticBlas.compactionCount = 2u;
    frame.staticBlas.buildCpuMs = 4.0;
    frame.staticBlas.buildGpuMs = 2.0;
    frame.staticBlas.compactionCpuMs = 3.0;
    frame.staticBlas.compactionGpuMs = 1.0;
    for (uint64_t sequence = 1u; sequence <= 1002u; ++sequence) {
        frame.sequence = sequence;
        auto& sceneTlas = frame.stages[static_cast<size_t>(AccelerationStructureStage::SceneTlas)];
        sceneTlas.cpuMs = static_cast<double>(sequence);
        sceneTlas.operationCount = 1u;
        sceneTlas.instanceCount = sequence;
        sceneTlas.scratchBytes = sequence * 10u;
        sceneTlas.structureBytes = sequence * 20u;
        auto& terrainBuild = frame.stages[static_cast<size_t>(AccelerationStructureStage::TerrainBlasBuild)];
        terrainBuild.cpuMs = static_cast<double>(sequence) * 2.0;
        terrainBuild.operationCount = 2u;
        terrainBuild.primitiveCount = sequence * 3u;
        frame.activeSceneTlasInstances = static_cast<uint32_t>(sequence);
        frame.activeSceneTlasBlas = 2u;
        frame.activeTerrainBlas = 4u;
        frame.activeSceneTlasBytes = sequence * 30u;
        frame.activeSceneReferencedBlasBytes = sequence * 40u;
        frame.activeTerrainBlasBytes = sequence * 50u;
        frame.activeTerrainPrimitiveCount = sequence * 60u;
        if (!requireTrue(history.record(frame), "unique AS frame sequences must be recorded")) {
            return false;
        }
    }
    if (!requireTrue(!history.record(frame), "duplicate AS frame sequences must be rejected")) {
        return false;
    }
    frame.sequence = 1003u;
    frame.stages[static_cast<size_t>(AccelerationStructureStage::SceneTlas)].cpuMs = -1.0;
    if (!requireTrue(!history.record(frame), "negative AS CPU durations must be rejected")) {
        return false;
    }

    const AccelerationStructureWindowStats stats = history.snapshot();
    const auto& sceneTlas = stats.stages[static_cast<size_t>(AccelerationStructureStage::SceneTlas)];
    const auto& terrainBuild = stats.stages[static_cast<size_t>(AccelerationStructureStage::TerrainBlasBuild)];
    return requireTrue(stats.valid && stats.sampleCount == 1000u && stats.observedSampleCount == 1002u,
                       "AS history must retain the latest fixed-size window") &&
           requireNear(sceneTlas.cpuMs.p50Ms, 502.0, "AS CPU p50 must use nearest-rank selection") &&
           requireNear(terrainBuild.cpuMs.p95Ms, 1904.0, "terrain BLAS CPU p95 must preserve stage values") &&
           requireTrue(sceneTlas.operationCount == 1000u && sceneTlas.peakOperationsPerFrame == 1u &&
                           sceneTlas.peakInstancesPerFrame == 1002u && sceneTlas.peakScratchBytesPerFrame == 10020u &&
                           sceneTlas.peakStructureBytesPerFrame == 20040u,
                       "AS history must retain work totals and per-frame peaks") &&
           requireTrue(stats.peakActiveSceneTlasInstances == 1002u && stats.peakActiveTerrainBlasBytes == 50100u &&
                           stats.latest.sequence == 1002u && stats.latest.staticBlas.assetCount == 2u,
                       "AS history must expose residency peaks and the latest static BLAS snapshot");
}

bool testRenderGraphTimingHistory() {
    RenderGraphTimingHistory history;
    RenderGraphFrameStats frame;
    frame.valid = true;
    frame.passCount = 75u;
    frame.batchCount = 14u;
    frame.submitCount = 11u;
    frame.workerRecordedBatches = 3u;
    frame.passes.push_back({"Frame", RhiQueueType::Graphics, 1.0, 0.0});
    frame.completeGpuFrame.supported = true;
    frame.completeGpuFrame.valid = true;

    for (uint64_t execution = 1u; execution <= 1002u; ++execution) {
        const double value = static_cast<double>(execution);
        frame.execution = execution;
        frame.cpuBuildMs = value;
        frame.cpuCompileMs = value * 2.0;
        frame.cpuExecuteMs = value * 3.0;
        frame.cpuRecordMs = value * 4.0;
        frame.cpuSubmitMs = value * 5.0;
        frame.cpuShadowPrepMs = value * 0.5;
        frame.cpuContextMs = value * 0.25;
        frame.cpuTerrainPrepMs = value * 0.125;
        frame.gpuTotalMs = value * 2.0;
        frame.gpuSpanMs = value * 1.5;
        frame.gpuIdleMs = 0.0;
        frame.completeGpuFrame.sequence = execution;
        frame.completeGpuFrame.spanMs = value * 0.75;
        if (!requireTrue(history.record(frame), "valid Render Graph frames must be accepted")) {
            return false;
        }
    }

    frame.cpuBuildMs = 2000.0;
    if (!requireTrue(history.record(frame), "a CPU sample must be accepted when the GPU execution is duplicated")) {
        return false;
    }

    RenderGraphFrameStats invalidCompleteGpuFrame = frame;
    invalidCompleteGpuFrame.completeGpuFrame.sequence = 1003u;
    invalidCompleteGpuFrame.completeGpuFrame.spanMs = -1.0;
    if (!requireTrue(!history.record(invalidCompleteGpuFrame),
                     "negative complete GPU frame spans must be rejected")) {
        return false;
    }

    RenderGraphFrameStats invalid = frame;
    invalid.execution = 1003u;
    invalid.cpuBuildMs = -1.0;
    if (!requireTrue(!history.record(invalid), "negative Render Graph durations must be rejected")) {
        return false;
    }

    const RenderGraphTimingWindowStats stats = history.snapshot();
    const auto& build = stats.cpu[static_cast<size_t>(RenderGraphCpuTimingStage::Build)];
    const auto& record = stats.cpu[static_cast<size_t>(RenderGraphCpuTimingStage::Record)];
    const auto& gpuTotal = stats.gpu[static_cast<size_t>(RenderGraphGpuTimingMetric::Total)];
    const auto& gpuSpan = stats.gpu[static_cast<size_t>(RenderGraphGpuTimingMetric::Span)];
    return requireTrue(stats.cpuValid && stats.gpuValid && stats.cpuSampleCount == 1000u &&
                           stats.gpuSampleCount == 1000u && stats.observedGpuSampleCount == 1002u &&
                           stats.completeGpuFrameValid && stats.completeGpuFrameSampleCount == 1000u &&
                           stats.observedCompleteGpuFrameSampleCount == 1002u,
                       "Render Graph history must retain separate CPU and GPU windows") &&
           requireNear(build.p50Ms, 503.0, "Render Graph CPU p50 must use nearest-rank selection") &&
           requireNear(record.p95Ms, 3812.0, "Render Graph CPU Record p95 must preserve stage values") &&
           requireNear(gpuTotal.p95Ms, 1904.0, "Render Graph GPU Total p95 must preserve stage values") &&
           requireNear(gpuSpan.p50Ms, 753.0, "Render Graph GPU Span p50 must preserve stage values") &&
           requireNear(stats.completeGpuFrameSpanMs.p50Ms, 376.5,
                       "complete GPU frame p50 must use its independent window") &&
           requireNear(stats.completeGpuFrameSpanMs.p95Ms, 714.0,
                       "complete GPU frame p95 must use its independent window") &&
           requireNear(stats.completeGpuFrameSpanMs.p99Ms, 744.0,
                       "complete GPU frame p99 must use its independent window") &&
           requireTrue(stats.latest.valid && stats.latest.gpuValid && stats.latest.execution == 1002u &&
                           stats.latest.passCount == 75u && stats.latest.workerRecordedBatches == 3u &&
                           stats.latest.completeGpuFrameValid && stats.latest.completeGpuFrameSequence == 1002u,
                       "Render Graph history must expose latest structural counters") &&
           requireNear(stats.latest.completeGpuFrameSpanMs, 751.5,
                       "latest complete GPU frame span must remain distinct");
}

} // namespace

int main() {
    if (!testStableStageNames() || !testFixedWindowPercentiles() || !testAccelerationStructureHistory() ||
        !testRenderGraphTimingHistory()) {
        return 1;
    }
    std::cout << "[gpu_timing_history_test] PASS\n";
    return 0;
}
