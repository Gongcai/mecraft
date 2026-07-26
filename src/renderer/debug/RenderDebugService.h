#ifndef MECRAFT_RENDER_DEBUG_SERVICE_H
#define MECRAFT_RENDER_DEBUG_SERVICE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    Lighting = 4,
    Transparent = 5,
    Volumetric = 6,
    Reflection = 7,
    Cloud = 8,
    Water = 9,
    Post = 10,
    Count = 11
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
enum class FrustumPlane : size_t {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Top = 3,
    Near = 4,
    Far = 5,
    Count = 6
};

/// GPU timing statistics for a single frame.
struct GpuFrameStats {
    bool supported = false;
    bool valid = false;
    double gbufferMs = 0.0;
    double shadowMs = 0.0;
    double ssaoMs = 0.0;
    double ssgiMs = 0.0;
    double lightingMs = 0.0;
    double transparentMs = 0.0;
    double volumetricMs = 0.0;
    double reflectionMs = 0.0;
    double cloudMs = 0.0;
    double waterMs = 0.0;
    double postMs = 0.0;
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
    /// Sum of measured pass durations.
    double gpuTotalMs = 0.0;
    /// Last pass end minus first pass begin on the GPU clock.
    double gpuSpanMs = 0.0;
    /// Span minus total: scheduling bubbles between passes.
    double gpuIdleMs = 0.0;
    std::vector<RenderGraphPassStats> passes;
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

enum class ShadowTimestampPoint : size_t {
    Start = 0,
    OpaqueEnd = 1,
    End = 2,
    Count = 3
};

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

    /// Begin one command-list segment for a GPU pass.
    [[nodiscard]] GpuTimerSegmentToken beginGpuTimer(RhiCommandList& commandList,
                                                     GpuTimerPass pass);

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

    /// Shadow pass GPU timestamp and cascade statistics.
    [[nodiscard]] bool beginShadowFrame(int cascadeCount, int shadowResolution);
    void recordShadowCascadeStats(int cascadeIndex, const ShadowCascadeStats& stats);
    void recordShadowFrameTotals(int submitted, int culled, float maxCasterDistance);
    void markShadowTimestamp(RhiCommandList& commandList,
                             int cascadeIndex,
                             ShadowTimestampPoint point);
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
    enum class GpuTimerSegmentState : uint8_t {
        Unused,
        Recording,
        Issued
    };

    static constexpr size_t GPU_TIMER_RING_SIZE = 4;
    static constexpr size_t GPU_TIMER_PASS_COUNT = static_cast<size_t>(GpuTimerPass::Count);
    static constexpr size_t GPU_TIMER_MAX_SEGMENTS_PER_PASS = 16;
    static constexpr size_t GPU_TIMER_POINTS_PER_SEGMENT = 2;
    static constexpr uint32_t GPU_TIMER_QUERY_COUNT =
        static_cast<uint32_t>(GPU_TIMER_RING_SIZE * GPU_TIMER_PASS_COUNT *
                              GPU_TIMER_MAX_SEGMENTS_PER_PASS *
                              GPU_TIMER_POINTS_PER_SEGMENT);
    static constexpr size_t SHADOW_TIMER_CASCADE_COUNT = ShadowFrameStats::kMaxCascades;
    static constexpr size_t SHADOW_TIMER_POINT_COUNT = static_cast<size_t>(ShadowTimestampPoint::Count);
    static constexpr uint32_t SHADOW_TIMER_QUERY_COUNT =
        static_cast<uint32_t>(GPU_TIMER_RING_SIZE * SHADOW_TIMER_CASCADE_COUNT *
                              SHADOW_TIMER_POINT_COUNT);

    [[nodiscard]] static uint32_t gpuTimerQueryIndex(size_t frameIndex,
                                                     size_t passIndex,
                                                     size_t segmentIndex,
                                                     size_t pointIndex);
    [[nodiscard]] static uint32_t shadowQueryIndex(size_t frameIndex,
                                                   size_t cascadeIndex,
                                                   size_t pointIndex);

    // GPU timer state
    RhiQueryPoolHandle m_gpuTimerQueryPool;
    std::array<std::array<uint8_t, GPU_TIMER_PASS_COUNT>, GPU_TIMER_RING_SIZE>
        m_gpuTimerAllocatedSegmentCounts{};
    std::array<std::array<std::array<GpuTimerSegmentState,
                                    GPU_TIMER_MAX_SEGMENTS_PER_PASS>,
                          GPU_TIMER_PASS_COUNT>,
               GPU_TIMER_RING_SIZE>
        m_gpuTimerSegmentStates{};
    GpuFrameStats m_gpuFrameStats{};
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerCanIssueThisFrame = true;

    // Shadow timestamps use one explicit RHI query pool. Each ring slot owns a
    // contiguous range so availability and result reads can be batched.
    RhiDevice* m_rhiDevice = nullptr;
    RhiQueryPoolHandle m_shadowTimestampQueryPool;
    std::array<std::array<std::array<bool, SHADOW_TIMER_POINT_COUNT>, SHADOW_TIMER_CASCADE_COUNT>, GPU_TIMER_RING_SIZE> m_shadowTimestampIssued{};
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
