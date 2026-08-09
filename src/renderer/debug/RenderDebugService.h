#ifndef MECRAFT_RENDER_DEBUG_SERVICE_H
#define MECRAFT_RENDER_DEBUG_SERVICE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "../contracts/DynamicBlasPolicyContract.h"
#include "../contracts/RtgiSamplingContract.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiTypes.h"

class RhiCommandList;
class RhiDevice;

/// GPU timer pass identifiers for profiling different rendering stages.
enum class GpuTimerPass : size_t {
    GBuffer = 0,
    Shadow = 1,
    Ssao = 2,
    Ssgi = 3,
    Rtgi = 4,
    Nrd = 5,
    Lighting = 6,
    Transparent = 7,
    Volumetric = 8,
    Reflection = 9,
    Cloud = 10,
    Water = 11,
    Post = 12,
    RtgiTrace = 13,
    RtgiSignalPack = 14,
    NrdGuidePrep = 15,
    NrdDispatch = 16,
    SceneTlas = 17,
    TerrainBlasBuild = 18,
    TerrainBlasCompaction = 19,
    AccelerationStructureDynamicPrepare = 20,
    RtgiSceneTlasBootstrap = 21,
    Count = 22
};

/// Identifies one explicitly recorded GPU timestamp segment.
struct GpuTimerSegmentToken {
    GpuTimerPass pass = GpuTimerPass::GBuffer;
    uint8_t frameIndex = 0;
    uint8_t segmentIndex = 0;
    bool valid = false;
};

/// Captures timer allocation state before recording an unsubmitted command batch.
struct GpuTimerCheckpoint {
    std::array<uint8_t, static_cast<size_t>(GpuTimerPass::Count)> segmentCounts{};
    uint8_t frameIndex = 0;
    bool valid = false;
};

/// Frustum plane identifiers for culling statistics.
enum class FrustumPlane : size_t { Left = 0, Right = 1, Bottom = 2, Top = 3, Near = 4, Far = 5, Count = 6 };

/// GPU timing statistics for a single frame.
struct GpuFrameStats {
    bool supported = false;
    bool valid = false;
    /// Monotonic identity assigned whenever a completed timer frame is published.
    uint64_t sequence = 0u;
    double gbufferMs = 0.0;
    double shadowMs = 0.0;
    double ssaoMs = 0.0;
    double ssgiMs = 0.0;
    double rtgiMs = 0.0;
    double nrdMs = 0.0;
    double rtgiTraceMs = 0.0;
    double rtgiSignalPackMs = 0.0;
    double nrdGuidePrepMs = 0.0;
    double nrdDispatchMs = 0.0;
    double sceneTlasMs = 0.0;
    double terrainBlasBuildMs = 0.0;
    double terrainBlasCompactionMs = 0.0;
    double accelerationStructureDynamicPrepareMs = 0.0;
    double rtgiSceneTlasBootstrapMs = 0.0;
    double lightingMs = 0.0;
    double transparentMs = 0.0;
    double volumetricMs = 0.0;
    double reflectionMs = 0.0;
    double cloudMs = 0.0;
    double waterMs = 0.0;
    double postMs = 0.0;
};

/// Complete GPU span for one scene-rendering frame across all submitted Render Graphs.
/// Presentation and UI work submitted after the terminal scene-render marker are excluded.
struct GpuFrameSpanStats {
    bool supported = false;
    bool valid = false;
    /// Monotonic identity assigned whenever a completed span is published.
    uint64_t sequence = 0u;
    double spanMs = 0.0;
};

/// Percentiles computed from one fixed GPU timing sample window.
struct GpuTimingPercentiles {
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
};

/// Percentiles for one stable Render Graph timing stage.
struct GpuTimerPassWindowStats {
    GpuTimerPass pass = GpuTimerPass::GBuffer;
    GpuTimingPercentiles gpuMs;
};

/// Snapshot of the most recent fixed-size GPU timing sample window.
struct GpuTimingWindowStats {
    bool valid = false;
    size_t sampleCount = 0u;
    size_t capacity = 0u;
    uint64_t observedSampleCount = 0u;
    GpuTimingPercentiles totalTrackedGpuMs;
    std::array<GpuTimerPassWindowStats, static_cast<size_t>(GpuTimerPass::Count)> passes{};
};

/// Returns the stable diagnostic name for a Render Graph GPU timing stage.
/// @param pass Stage identifier whose name is requested.
/// @return Process-lifetime English name suitable for UI and JSON keys.
[[nodiscard]] const char* gpuTimerPassName(GpuTimerPass pass);

/// Stores the most recent real-rendered Render Graph timings without allocation.
/// Duplicate sequence identities are rejected so delayed query results are
/// counted exactly once by both Dashboard and benchmark consumers.
class GpuTimingHistory {
public:
    static constexpr size_t kCapacity = 1000u;

    /// Clears every stored sample and sequence identity.
    void reset();

    /// Records one completed GPU timer frame.
    /// @param stats Completed frame with a non-zero unique sequence identity.
    /// @return True when the frame was accepted into the window.
    [[nodiscard]] bool record(const GpuFrameStats& stats);

    /// Computes p50, p95, and p99 for each stage and their tracked sum.
    /// @return Immutable statistics for the currently stored sample window.
    [[nodiscard]] GpuTimingWindowStats snapshot() const;

private:
    std::array<std::array<double, kCapacity>, static_cast<size_t>(GpuTimerPass::Count)> m_passSamples{};
    std::array<double, kCapacity> m_totalSamples{};
    size_t m_nextSample = 0u;
    size_t m_sampleCount = 0u;
    uint64_t m_observedSampleCount = 0u;
    uint64_t m_lastSequence = 0u;
};

/// Runtime acceleration-structure stages with independent CPU work attribution.
enum class AccelerationStructureStage : size_t {
    SceneTlas = 0,
    TerrainBlasBuild = 1,
    TerrainBlasCompaction = 2,
    DynamicResourcePreparation = 3,
    RtgiSceneTlasBootstrap = 4,
    Count = 5
};

/// Returns the stable report name for one acceleration-structure stage.
/// @param stage Stage whose diagnostic name is requested.
/// @return Process-lifetime English identifier suitable for JSON keys.
[[nodiscard]] const char* accelerationStructureStageName(AccelerationStructureStage stage);

/// CPU duration and work volume recorded for one acceleration-structure stage in one frame.
struct AccelerationStructureStageFrameStats {
    double cpuMs = 0.0;
    uint32_t operationCount = 0u;
    uint64_t instanceCount = 0u;
    uint64_t primitiveCount = 0u;
    uint64_t scratchBytes = 0u;
    uint64_t structureBytes = 0u;
};

/// Immutable asset-level static BLAS build diagnostics aggregated across the active scene.
struct StaticBlasFrameStats {
    bool supported = false;
    uint32_t assetCount = 0u;
    uint32_t residentAssetCount = 0u;
    uint64_t buildCount = 0u;
    uint64_t compactionCount = 0u;
    uint64_t geometryCount = 0u;
    uint64_t primitiveCount = 0u;
    uint64_t opaquePrimitiveCount = 0u;
    uint64_t cutoutPrimitiveCount = 0u;
    uint64_t scratchPeakBytes = 0u;
    uint64_t uncompactedBlasBytes = 0u;
    uint64_t compactedBlasBytes = 0u;
    double buildCpuMs = 0.0;
    double buildGpuMs = 0.0;
    double compactionCpuMs = 0.0;
    double compactionGpuMs = 0.0;
};

/// Per-frame dynamic BLAS actions and ordered policy rejection counters.
struct DynamicBlasFrameStats {
    bool producerConnected = false;
    std::array<uint32_t, static_cast<size_t>(renderer::contracts::DynamicBlasAction::Count)> actionCounts{};
    std::array<uint32_t, static_cast<size_t>(renderer::contracts::DynamicBlasRigidReuseRejectReason::Count)>
        rigidReuseRejectCounts{};
    std::array<uint32_t, static_cast<size_t>(renderer::contracts::DynamicBlasUpdateRejectReason::Count)>
        updateRejectCounts{};
};

/// Current terrain ray-tracing bucket distribution and this frame's build workload.
struct TerrainRayTracingBucketFrameStats {
    uint64_t activeOpaquePrimitives = 0u;
    uint64_t activeCutoutPrimitives = 0u;
    uint64_t builtOpaquePrimitives = 0u;
    uint64_t builtCutoutPrimitives = 0u;
};

/// Per-frame runtime AS workload plus the current resident resource snapshot.
struct AccelerationStructureFrameStats {
    bool supported = false;
    bool valid = false;
    uint64_t sequence = 0u;
    std::array<AccelerationStructureStageFrameStats, static_cast<size_t>(AccelerationStructureStage::Count)> stages{};
    uint32_t activeSceneTlasInstances = 0u;
    uint32_t activeSceneTlasBlas = 0u;
    uint32_t activeTerrainBlas = 0u;
    uint64_t activeSceneTlasBytes = 0u;
    uint64_t activeSceneReferencedBlasBytes = 0u;
    uint64_t activeTerrainBlasBytes = 0u;
    uint64_t activeTerrainPrimitiveCount = 0u;
    uint32_t sceneTlasGenerationAllocations = 0u;
    uint32_t sceneTlasGenerationReuses = 0u;
    uint32_t sceneTlasGenerationReuseWaits = 0u;
    uint32_t retiredSceneTlasGenerations = 0u;
    DynamicBlasFrameStats dynamicBlas;
    TerrainRayTracingBucketFrameStats terrainBuckets;
    StaticBlasFrameStats staticBlas;
};

/// Totals and per-frame peaks for one AS work stage over a fixed sample window.
struct AccelerationStructureStageWindowStats {
    GpuTimingPercentiles cpuMs;
    uint64_t operationCount = 0u;
    uint32_t peakOperationsPerFrame = 0u;
    uint64_t peakInstancesPerFrame = 0u;
    uint64_t peakPrimitivesPerFrame = 0u;
    uint64_t peakScratchBytesPerFrame = 0u;
    uint64_t peakStructureBytesPerFrame = 0u;
};

/// Fixed-window totals and per-frame peaks for dynamic BLAS policy decisions.
struct DynamicBlasWindowStats {
    bool producerConnected = false;
    std::array<uint64_t, static_cast<size_t>(renderer::contracts::DynamicBlasAction::Count)> actionCounts{};
    std::array<uint32_t, static_cast<size_t>(renderer::contracts::DynamicBlasAction::Count)> peakActionsPerFrame{};
    std::array<uint64_t, static_cast<size_t>(renderer::contracts::DynamicBlasRigidReuseRejectReason::Count)>
        rigidReuseRejectCounts{};
    std::array<uint64_t, static_cast<size_t>(renderer::contracts::DynamicBlasUpdateRejectReason::Count)>
        updateRejectCounts{};
};

/// Fixed-window terrain ray-tracing bucket workload and residency peaks.
struct TerrainRayTracingBucketWindowStats {
    uint64_t builtOpaquePrimitives = 0u;
    uint64_t builtCutoutPrimitives = 0u;
    uint64_t peakBuiltOpaquePrimitivesPerFrame = 0u;
    uint64_t peakBuiltCutoutPrimitivesPerFrame = 0u;
    uint64_t peakActiveOpaquePrimitives = 0u;
    uint64_t peakActiveCutoutPrimitives = 0u;
};

/// Fixed-window acceleration-structure workload and residency statistics.
struct AccelerationStructureWindowStats {
    bool valid = false;
    size_t sampleCount = 0u;
    size_t capacity = 0u;
    uint64_t observedSampleCount = 0u;
    std::array<AccelerationStructureStageWindowStats, static_cast<size_t>(AccelerationStructureStage::Count)> stages{};
    uint32_t peakActiveSceneTlasInstances = 0u;
    uint32_t peakActiveSceneTlasBlas = 0u;
    uint32_t peakActiveTerrainBlas = 0u;
    uint64_t peakActiveSceneTlasBytes = 0u;
    uint64_t peakActiveSceneReferencedBlasBytes = 0u;
    uint64_t peakActiveTerrainBlasBytes = 0u;
    uint64_t peakActiveTerrainPrimitiveCount = 0u;
    uint64_t sceneTlasGenerationAllocationCount = 0u;
    uint64_t sceneTlasGenerationReuseCount = 0u;
    uint64_t sceneTlasGenerationReuseWaitCount = 0u;
    uint32_t peakSceneTlasGenerationAllocationsPerFrame = 0u;
    uint32_t peakSceneTlasGenerationReusesPerFrame = 0u;
    uint32_t peakSceneTlasGenerationReuseWaitsPerFrame = 0u;
    uint32_t peakRetiredSceneTlasGenerations = 0u;
    DynamicBlasWindowStats dynamicBlas;
    TerrainRayTracingBucketWindowStats terrainBuckets;
    AccelerationStructureFrameStats latest;
};

/// Fixed-window totals and peaks from completed RTGI validation-image reductions.
struct RtgiTraceCounterWindowStats final {
    bool valid = false;
    size_t sampleCount = 0u;
    size_t capacity = 0u;
    uint64_t observedSampleCount = 0u;
    uint64_t pixelCount = 0u;
    uint64_t candidateCount = 0u;
    uint64_t confirmedCount = 0u;
    double confirmationRate = 0.0;
    uint32_t peakCandidateCountPerPixel = 0u;
    uint32_t peakConfirmedCountPerPixel = 0u;
    renderer::contracts::RtgiTraceCounterFrameStats latest;
};

/// Stores unique delayed RTGI counter readbacks in a fixed allocation-free window.
class RtgiTraceCounterHistory final {
public:
    static constexpr size_t kCapacity = 1000u;

    /// Clears every counter sample and sequence identity.
    void reset();

    /// Records one validated asynchronous counter readback.
    /// @param stats Completed reduction with a non-zero monotonic sequence identity.
    /// @return True when the sample was accepted into the window.
    [[nodiscard]] bool record(const renderer::contracts::RtgiTraceCounterFrameStats& stats);

    /// Returns aggregate totals, confirmation rate, and per-pixel peaks.
    [[nodiscard]] RtgiTraceCounterWindowStats snapshot() const;

private:
    std::array<uint64_t, kCapacity> m_pixelSamples{};
    std::array<uint64_t, kCapacity> m_candidateSamples{};
    std::array<uint64_t, kCapacity> m_confirmedSamples{};
    std::array<uint32_t, kCapacity> m_peakCandidateSamples{};
    std::array<uint32_t, kCapacity> m_peakConfirmedSamples{};
    size_t m_nextSample = 0u;
    size_t m_sampleCount = 0u;
    uint64_t m_observedSampleCount = 0u;
    uint64_t m_lastSequence = 0u;
    renderer::contracts::RtgiTraceCounterFrameStats m_latest;
};

/// Stores runtime AS CPU work, counts, byte volumes, and residency peaks without allocation.
class AccelerationStructureHistory {
public:
    static constexpr size_t kCapacity = 1000u;

    /// Clears every sample, accumulated counter, peak, and sequence identity.
    void reset();

    /// Records one rendered frame with a non-zero monotonic sequence identity.
    /// @param stats Completed acceleration-structure frame snapshot.
    /// @return True when the frame was accepted into the history window.
    [[nodiscard]] bool record(const AccelerationStructureFrameStats& stats);

    /// Computes CPU percentiles and returns accumulated work and residency peaks.
    [[nodiscard]] AccelerationStructureWindowStats snapshot() const;

private:
    std::array<std::array<double, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)> m_cpuSamples{};
    std::array<std::array<uint32_t, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)>
        m_operationSamples{};
    std::array<std::array<uint64_t, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)>
        m_instanceSamples{};
    std::array<std::array<uint64_t, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)>
        m_primitiveSamples{};
    std::array<std::array<uint64_t, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)>
        m_scratchByteSamples{};
    std::array<std::array<uint64_t, kCapacity>, static_cast<size_t>(AccelerationStructureStage::Count)>
        m_structureByteSamples{};
    std::array<uint32_t, kCapacity> m_activeSceneTlasInstanceSamples{};
    std::array<uint32_t, kCapacity> m_activeSceneTlasBlasSamples{};
    std::array<uint32_t, kCapacity> m_activeTerrainBlasSamples{};
    std::array<uint64_t, kCapacity> m_activeSceneTlasByteSamples{};
    std::array<uint64_t, kCapacity> m_activeSceneReferencedBlasByteSamples{};
    std::array<uint64_t, kCapacity> m_activeTerrainBlasByteSamples{};
    std::array<uint64_t, kCapacity> m_activeTerrainPrimitiveSamples{};
    std::array<uint32_t, kCapacity> m_sceneTlasGenerationAllocationSamples{};
    std::array<uint32_t, kCapacity> m_sceneTlasGenerationReuseSamples{};
    std::array<uint32_t, kCapacity> m_sceneTlasGenerationReuseWaitSamples{};
    std::array<uint32_t, kCapacity> m_retiredSceneTlasGenerationSamples{};
    std::array<uint64_t, kCapacity> m_activeTerrainOpaquePrimitiveSamples{};
    std::array<uint64_t, kCapacity> m_activeTerrainCutoutPrimitiveSamples{};
    std::array<uint64_t, kCapacity> m_builtTerrainOpaquePrimitiveSamples{};
    std::array<uint64_t, kCapacity> m_builtTerrainCutoutPrimitiveSamples{};
    std::array<std::array<uint32_t, kCapacity>, static_cast<size_t>(renderer::contracts::DynamicBlasAction::Count)>
        m_dynamicBlasActionSamples{};
    std::array<std::array<uint32_t, kCapacity>,
               static_cast<size_t>(renderer::contracts::DynamicBlasRigidReuseRejectReason::Count)>
        m_dynamicBlasRigidReuseRejectSamples{};
    std::array<std::array<uint32_t, kCapacity>,
               static_cast<size_t>(renderer::contracts::DynamicBlasUpdateRejectReason::Count)>
        m_dynamicBlasUpdateRejectSamples{};
    size_t m_nextSample = 0u;
    size_t m_sampleCount = 0u;
    uint64_t m_observedSampleCount = 0u;
    uint64_t m_lastSequence = 0u;
    AccelerationStructureFrameStats m_latest;
};

/// One Render Graph pass GPU timing entry in compiled execution order.
struct RenderGraphPassStats {
    std::string name;
    RhiQueueType queue = RhiQueueType::Graphics;
    double gpuMs = 0.0;
    /// GPU idle time between the previous pass's end and this pass's start.
    double gapMs = 0.0;
};

/// Render Graph frame statistics: CPU stage costs sampled at the most recent
/// graph submission plus GPU pass timings from the latest completed snapshot.
struct RenderGraphFrameStats {
    bool valid = false;
    /// Execution identity of the GPU timing snapshot (0 while none completed).
    uint64_t execution = 0u;
    uint32_t passCount = 0u;
    uint32_t batchCount = 0u;
    /// Queue submissions issued for the last frame graph.
    uint32_t submitCount = 0u;
    /// Submission batches recorded on worker threads last frame.
    uint32_t workerRecordedBatches = 0u;
    double cpuBuildMs = 0.0;
    double cpuCompileMs = 0.0;
    double cpuExecuteMs = 0.0;
    /// Portions of cpuExecuteMs: command recording versus queue submission.
    double cpuRecordMs = 0.0;
    double cpuSubmitMs = 0.0;
    /// CPU-side shadow preparation (caster collection and culling).
    double cpuShadowPrepMs = 0.0;
    /// CPU-side frame context construction in RenderScene.
    double cpuContextMs = 0.0;
    /// Terrain collection and indirect upload inside the Record window.
    double cpuTerrainPrepMs = 0.0;
    /// Sum of measured pass durations.
    double gpuTotalMs = 0.0;
    /// Last pass end minus first pass begin on the GPU clock.
    double gpuSpanMs = 0.0;
    /// Span minus total: scheduling bubbles between passes.
    double gpuIdleMs = 0.0;
    /// Transient textures placed on shared alias pages this frame.
    uint32_t aliasedTextureCount = 0u;
    /// Live shared alias pages backing placed transients.
    uint32_t aliasPageCount = 0u;
    /// Bytes the aliased transients would need as dedicated images.
    uint64_t aliasedRequestBytes = 0u;
    /// Total bytes allocated across all live shared alias pages.
    uint64_t aliasTotalPageBytes = 0u;
    /// Complete scene-rendering GPU span collected from frame-start and terminal markers.
    GpuFrameSpanStats completeGpuFrame;
    /// Runtime acceleration-structure work and residency for this rendered frame.
    AccelerationStructureFrameStats accelerationStructures;
    /// Latest completed asynchronous RTGI validation-image reduction.
    renderer::contracts::RtgiTraceCounterFrameStats rtgiTraceCounters;
    std::vector<RenderGraphPassStats> passes;
};

/// Stable CPU timing stages exported by the Render Graph benchmark history.
enum class RenderGraphCpuTimingStage : size_t {
    Build = 0,
    Compile = 1,
    Execute = 2,
    Record = 3,
    Submit = 4,
    ShadowPrep = 5,
    Context = 6,
    TerrainPrep = 7,
    Count = 8
};

/// Returns the stable diagnostic name for one Render Graph CPU timing stage.
/// @param stage CPU timing stage whose name is requested.
/// @return Process-lifetime English name suitable for UI and JSON keys.
[[nodiscard]] const char* renderGraphCpuTimingStageName(RenderGraphCpuTimingStage stage);

/// Stable GPU frame metrics exported by the Render Graph benchmark history.
enum class RenderGraphGpuTimingMetric : size_t { Total = 0, Span = 1, Idle = 2, Overlap = 3, Count = 4 };

/// Returns the stable diagnostic name for one Render Graph GPU frame metric.
/// @param metric GPU frame metric whose name is requested.
/// @return Process-lifetime English name suitable for UI and JSON keys.
[[nodiscard]] const char* renderGraphGpuTimingMetricName(RenderGraphGpuTimingMetric metric);

/// Structural values from the latest Render Graph submission in a timing window.
struct RenderGraphLatestStats {
    bool valid = false;
    bool gpuValid = false;
    uint64_t execution = 0u;
    uint32_t passCount = 0u;
    uint32_t batchCount = 0u;
    uint32_t submitCount = 0u;
    uint32_t workerRecordedBatches = 0u;
    bool completeGpuFrameValid = false;
    uint64_t completeGpuFrameSequence = 0u;
    double completeGpuFrameSpanMs = 0.0;
};

/// Fixed-window CPU and primary Render Graph GPU timing statistics.
struct RenderGraphTimingWindowStats {
    bool cpuValid = false;
    bool gpuValid = false;
    size_t capacity = 0u;
    size_t cpuSampleCount = 0u;
    size_t gpuSampleCount = 0u;
    uint64_t observedGpuSampleCount = 0u;
    bool completeGpuFrameValid = false;
    size_t completeGpuFrameSampleCount = 0u;
    uint64_t observedCompleteGpuFrameSampleCount = 0u;
    GpuTimingPercentiles completeGpuFrameSpanMs;
    std::array<GpuTimingPercentiles, static_cast<size_t>(RenderGraphCpuTimingStage::Count)> cpu{};
    std::array<GpuTimingPercentiles, static_cast<size_t>(RenderGraphGpuTimingMetric::Count)> gpu{};
    RenderGraphLatestStats latest;
};

/// Stores CPU submission timings and delayed primary Render Graph GPU spans.
/// CPU samples are accepted on every valid frame; GPU samples are accepted once
/// per completed Render Graph execution identity.
class RenderGraphTimingHistory {
public:
    static constexpr size_t kCapacity = GpuTimingHistory::kCapacity;

    /// Clears every stored CPU/GPU sample and structural value.
    void reset();

    /// Records one Render Graph frame snapshot.
    /// @param stats CPU submission statistics and the latest completed GPU timing snapshot.
    /// @return True when the CPU snapshot or a new GPU execution was accepted.
    [[nodiscard]] bool record(const RenderGraphFrameStats& stats);

    /// Computes p50, p95, and p99 for all CPU/GPU frame metrics.
    /// @return Immutable statistics for the currently stored timing windows.
    [[nodiscard]] RenderGraphTimingWindowStats snapshot() const;

private:
    std::array<std::array<double, kCapacity>, static_cast<size_t>(RenderGraphCpuTimingStage::Count)> m_cpuSamples{};
    std::array<std::array<double, kCapacity>, static_cast<size_t>(RenderGraphGpuTimingMetric::Count)> m_gpuSamples{};
    size_t m_cpuNextSample = 0u;
    size_t m_cpuSampleCount = 0u;
    size_t m_gpuNextSample = 0u;
    size_t m_gpuSampleCount = 0u;
    uint64_t m_observedGpuSampleCount = 0u;
    uint64_t m_lastGpuExecution = 0u;
    std::array<double, kCapacity> m_completeGpuFrameSamples{};
    size_t m_completeGpuFrameNextSample = 0u;
    size_t m_completeGpuFrameSampleCount = 0u;
    uint64_t m_observedCompleteGpuFrameSampleCount = 0u;
    uint64_t m_lastCompleteGpuFrameSequence = 0u;
    RenderGraphLatestStats m_latest;
};

/// Hi-Z occlusion culling counters read back from the GPU (ring-delayed by
/// a few frames to avoid stalls).
struct HiZCullFrameStats {
    bool valid = false;
    uint32_t opaqueCulled = 0u;
    uint32_t opaqueTotal = 0u;
    uint32_t cutoutCulled = 0u;
    uint32_t cutoutTotal = 0u;
};

/// Per-cascade GPU shadow command culling counters read back from the GPU
/// (ring-delayed by a few frames to avoid stalls). Frozen interleaved
/// cascades retain the counts from their last rendered frame.
struct ShadowCullFrameStats {
    bool valid = false;
    std::array<uint32_t, 4> culled{};
    std::array<uint32_t, 4> total{};
};

/// Per-cascade CSM shadow statistics for profiling and baseline capture.
struct ShadowCascadeStats {
    double gpuTotalMs = 0.0;
    double gpuOpaqueMs = 0.0;
    double gpuTransparentMs = 0.0;
    int boxVisible = 0;
    int boxCulled = 0;
    int distanceVisible = 0;
    int distanceCulled = 0;
    int cutoutEntries = 0;
    int transparentEntries = 0;
    size_t opaqueCommands = 0;
    size_t cutoutCommands = 0;
    size_t transparentCommands = 0;
    uint64_t opaqueVertices = 0;
    uint64_t cutoutVertices = 0;
    uint64_t transparentVertices = 0;
    float splitNear = 0.0f;
    float splitFar = 0.0f;
    float radius = 0.0f;
    float texelWorldSize = 0.0f;
    bool transparentRendered = false;
};

/// Shadow pass breakdown for the latest available GPU timestamp frame.
struct ShadowFrameStats {
    static constexpr size_t kMaxCascades = 4;

    bool supported = false;
    bool valid = false;
    int cascadeCount = 0;
    int shadowResolution = 0;
    int submitted = 0;
    int culled = 0;
    float maxCasterDistance = 0.0f;
    double gpuTotalMs = 0.0;
    std::array<ShadowCascadeStats, kMaxCascades> cascades{};
};

enum class ShadowTimestampPoint : size_t { Start = 0, OpaqueEnd = 1, End = 2, Count = 3 };

/// Frustum culling statistics for a single frame.
struct CullingFrameStats {
    int regionTests = 0;
    int regionPassed = 0;
    int columnTests = 0;
    int columnPassed = 0;
    int chunkTests = 0;
    int chunkPassed = 0;
    int chunkCulled = 0;
    std::array<int, static_cast<size_t>(FrustumPlane::Count)> chunkCulledByPlane{};
};

/// Render work statistics for a single frame.
struct RenderWorkStats {
    size_t blockVertexBytes = 0;
    size_t opaqueCommands = 0;
    size_t cutoutCommands = 0;
    size_t transparentCommands = 0;
    size_t transparentGenericCommands = 0;
    size_t transparentWaterCommands = 0;
    size_t opaqueLogicalCommands = 0;
    size_t cutoutLogicalCommands = 0;
    size_t transparentLogicalCommands = 0;
    size_t opaquePoolCapacityVertices = 0;
    size_t cutoutPoolCapacityVertices = 0;
    size_t transparentPoolCapacityVertices = 0;
    size_t opaquePoolUsedVertices = 0;
    size_t cutoutPoolUsedVertices = 0;
    size_t transparentPoolUsedVertices = 0;
    size_t opaquePoolUsedBytes = 0;
    size_t cutoutPoolUsedBytes = 0;
    size_t transparentPoolUsedBytes = 0;
    size_t opaquePoolCapacityBytes = 0;
    size_t cutoutPoolCapacityBytes = 0;
    size_t transparentPoolCapacityBytes = 0;
    size_t terrainPoolUsedBytes = 0;
    size_t terrainPoolCapacityBytes = 0;
    size_t terrainMetadataBytes = 0;
    size_t terrainMetadataSlots = 0;
    size_t terrainMetadataFreeSlots = 0;
    float opaquePoolFragmentation = 0.0f;
    float cutoutPoolFragmentation = 0.0f;
    float transparentPoolFragmentation = 0.0f;
    uint64_t opaqueVertices = 0;
    uint64_t cutoutVertices = 0;
    uint64_t transparentVertices = 0;
    uint64_t transparentGenericVertices = 0;
    uint64_t transparentWaterVertices = 0;
    uint64_t opaqueVertexReadBytes = 0;
    uint64_t cutoutVertexReadBytes = 0;
    uint64_t transparentVertexReadBytes = 0;
    uint64_t terrainVertexReadBytes = 0;
    int cutoutCandidates = 0;
    int cutoutSkippedByDistance = 0;
    int mdiSubChunkTests = 0;
    int mdiSubChunksCulled = 0;
    // Upload budget stats
    size_t meshUploadBytesThisFrame = 0;
    size_t meshUploadVerticesThisFrame = 0;
    size_t meshUploadDeferredCount = 0;
    size_t worldBufferExpandCount = 0;
    double worldBufferUploadMs = 0.0;
    bool terrainBlasSupported = false;
    bool terrainBlasHealthy = true;
    uint32_t terrainActiveBlas = 0u;
    uint32_t terrainPendingBlasBuilds = 0u;
    uint32_t terrainPendingBlasCompactions = 0u;
    uint32_t terrainRetiredBlasTasks = 0u;
    uint32_t terrainBlasBuildsThisFrame = 0u;
    uint32_t terrainBlasCompactionsThisFrame = 0u;
    uint64_t terrainBlasPrimitives = 0u;
    uint64_t terrainBlasGeometryBytes = 0u;
    uint64_t terrainBlasPrimitiveMetadataBytes = 0u;
    uint64_t terrainBlasBytes = 0u;
    uint64_t terrainBlasScratchPeakBytes = 0u;
};

/// Manages GPU timer queries and collects debug statistics for the rendering pipeline.
/// Extracted from Renderer to centralize debug/profiling functionality.
class RenderDebugService {
public:
    void init(RhiDevice& rhiDevice);
    void shutdown();

    /// Begin a new frame for GPU timer queries.
    /// Must be called at the start of each frame before any beginGpuTimer calls.
    /// Resolves completed timer slots and resets the next writable query ranges.
    /// @param commandList Recording command list outside any rendering scope.
    void beginFrame(RhiCommandList& commandList);

    /// Marks the terminal GPU point of the current scene-rendering frame.
    /// @param commandList Final submitted scene-render command list outside rendering scope.
    void endGpuFrameSpan(RhiCommandList& commandList);

    /// Discards the current frame span when its terminal graph cannot submit.
    void cancelGpuFrameSpan();

    /// Begin one command-list segment for a GPU pass.
    [[nodiscard]] GpuTimerSegmentToken beginGpuTimer(RhiCommandList& commandList, GpuTimerPass pass);

    /// End the active command-list segment for a GPU pass.
    void endGpuTimer(RhiCommandList& commandList, GpuTimerSegmentToken token);

    /// Discard a segment whose command list will not be submitted.
    void cancelGpuTimer(GpuTimerSegmentToken token);

    /// Captures all timer allocation counts before an atomic command batch is recorded.
    [[nodiscard]] GpuTimerCheckpoint gpuTimerCheckpoint() const;

    /// Discards every timer allocated after a checkpoint when its batch was not submitted.
    void cancelGpuTimersSince(const GpuTimerCheckpoint& checkpoint);

    /// Enable or disable GPU timer queries.
    void setGpuTimerEnabled(bool enabled) { m_gpuTimerEnabled = enabled; }
    [[nodiscard]] bool isGpuTimerEnabled() const { return m_gpuTimerEnabled; }

    /// Get the latest GPU frame statistics.
    [[nodiscard]] const GpuFrameStats& getGpuFrameStats() const { return m_gpuFrameStats; }

    /// Get the latest completed complete scene-rendering GPU span.
    [[nodiscard]] const GpuFrameSpanStats& getGpuFrameSpanStats() const { return m_gpuFrameSpanStats; }

    /// Computes fixed-window percentiles for completed GPU timer frames.
    /// @return Statistics covering at most the latest 1000 real rendered frames.
    [[nodiscard]] GpuTimingWindowStats getGpuTimingWindowStats() const { return m_gpuTimingHistory.snapshot(); }

    /// Shadow pass GPU timestamp and cascade statistics.
    [[nodiscard]] bool beginShadowFrame(int cascadeCount, int shadowResolution);
    void recordShadowCascadeStats(int cascadeIndex, const ShadowCascadeStats& stats);
    void recordShadowFrameTotals(int submitted, int culled, float maxCasterDistance);
    void markShadowTimestamp(RhiCommandList& commandList, int cascadeIndex, ShadowTimestampPoint point);
    void endShadowFrame();
    /// Discards an active shadow frame whose timestamp command list was not submitted.
    void cancelShadowFrame();
    [[nodiscard]] const ShadowFrameStats& getShadowFrameStats() const { return m_shadowFrameStats; }

    // Culling statistics (updated by the rendering pipeline)
    void resetCullingStats();
    void recordRegionCull(bool passed);
    void recordColumnCull(bool passed);
    void recordChunkCull(bool passed, FrustumPlane culledPlane = FrustumPlane::Count);
    [[nodiscard]] const CullingFrameStats& getCullingFrameStats() const { return m_cullingStats; }

    // Render work statistics (updated by the rendering pipeline)
    void setRenderWorkStats(const RenderWorkStats& stats) { m_workStats = stats; }
    [[nodiscard]] const RenderWorkStats& getRenderWorkStats() const { return m_workStats; }

private:
    enum class GpuTimerSegmentState : uint8_t { Unused, Recording, Issued };
    enum class GpuFrameSpanState : uint8_t { Unused, Started, Issued };
    enum class GpuFrameSpanPoint : size_t { Start = 0, End = 1, Count = 2 };

    static constexpr size_t GPU_TIMER_RING_SIZE = 4;
    static constexpr size_t GPU_TIMER_PASS_COUNT = static_cast<size_t>(GpuTimerPass::Count);
    // RELAX emits 21 dispatches on its first frame, in addition to guide
    // preparation, so one stage needs more than the legacy 16 slots.
    static constexpr size_t GPU_TIMER_MAX_SEGMENTS_PER_PASS = 32;
    static constexpr size_t GPU_TIMER_POINTS_PER_SEGMENT = 2;
    static constexpr uint32_t GPU_TIMER_QUERY_COUNT = static_cast<uint32_t>(
        GPU_TIMER_RING_SIZE * GPU_TIMER_PASS_COUNT * GPU_TIMER_MAX_SEGMENTS_PER_PASS * GPU_TIMER_POINTS_PER_SEGMENT);
    static constexpr size_t GPU_FRAME_SPAN_POINT_COUNT = static_cast<size_t>(GpuFrameSpanPoint::Count);
    static constexpr uint32_t GPU_FRAME_SPAN_QUERY_COUNT =
        static_cast<uint32_t>(GPU_TIMER_RING_SIZE * GPU_FRAME_SPAN_POINT_COUNT);
    static constexpr size_t SHADOW_TIMER_CASCADE_COUNT = ShadowFrameStats::kMaxCascades;
    static constexpr size_t SHADOW_TIMER_POINT_COUNT = static_cast<size_t>(ShadowTimestampPoint::Count);
    static constexpr uint32_t SHADOW_TIMER_QUERY_COUNT =
        static_cast<uint32_t>(GPU_TIMER_RING_SIZE * SHADOW_TIMER_CASCADE_COUNT * SHADOW_TIMER_POINT_COUNT);

    [[nodiscard]] static uint32_t gpuTimerQueryIndex(size_t frameIndex, size_t passIndex, size_t segmentIndex,
                                                     size_t pointIndex);
    [[nodiscard]] static uint32_t gpuFrameSpanQueryIndex(size_t frameIndex, GpuFrameSpanPoint point);
    [[nodiscard]] static uint32_t shadowQueryIndex(size_t frameIndex, size_t cascadeIndex, size_t pointIndex);

    // GPU timer state
    /// Serializes the segment allocator read-modify-write in beginGpuTimer,
    /// endGpuTimer, and their cancel paths. Render-graph passes may record
    /// on worker threads, so segment slots must be claimed atomically.
    mutable std::mutex m_gpuTimerMutex;
    RhiQueryPoolHandle m_gpuTimerQueryPool;
    RhiQueryPoolHandle m_gpuFrameSpanQueryPool;
    std::array<std::array<uint8_t, GPU_TIMER_PASS_COUNT>, GPU_TIMER_RING_SIZE> m_gpuTimerAllocatedSegmentCounts{};
    std::array<std::array<std::array<GpuTimerSegmentState, GPU_TIMER_MAX_SEGMENTS_PER_PASS>, GPU_TIMER_PASS_COUNT>,
               GPU_TIMER_RING_SIZE>
        m_gpuTimerSegmentStates{};
    GpuFrameStats m_gpuFrameStats{};
    GpuFrameSpanStats m_gpuFrameSpanStats{};
    GpuTimingHistory m_gpuTimingHistory;
    uint64_t m_gpuFrameSequence = 0u;
    uint64_t m_gpuFrameSpanSequence = 0u;
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerCanIssueThisFrame = true;
    std::array<GpuFrameSpanState, GPU_TIMER_RING_SIZE> m_gpuFrameSpanStates{};

    // Shadow timestamps use one explicit RHI query pool. Each ring slot owns a
    // contiguous range so availability and result reads can be batched.
    RhiDevice* m_rhiDevice = nullptr;
    RhiQueryPoolHandle m_shadowTimestampQueryPool;
    std::array<std::array<std::array<bool, SHADOW_TIMER_POINT_COUNT>, SHADOW_TIMER_CASCADE_COUNT>, GPU_TIMER_RING_SIZE>
        m_shadowTimestampIssued{};
    std::array<ShadowFrameStats, GPU_TIMER_RING_SIZE> m_shadowFrameSlots{};
    std::array<bool, GPU_TIMER_RING_SIZE> m_shadowFrameIssued{};
    ShadowFrameStats m_shadowFrameStats{};
    bool m_shadowFrameActive = false;

    // Culling statistics
    CullingFrameStats m_cullingStats{};

    // Render work statistics
    RenderWorkStats m_workStats{};
};

#endif // MECRAFT_RENDER_DEBUG_SERVICE_H
