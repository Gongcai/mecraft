#ifndef MECRAFT_RENDER_DEBUG_SERVICE_H
#define MECRAFT_RENDER_DEBUG_SERVICE_H

#include <array>
#include <cstddef>
#include <cstdint>

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
    void init();
    void shutdown();

    /// Begin a new frame for GPU timer queries.
    /// Must be called at the start of each frame before any beginGpuTimer calls.
    void beginFrame();

    /// Begin timing a specific GPU pass.
    [[nodiscard]] bool beginGpuTimer(GpuTimerPass pass);

    /// End timing a specific GPU pass.
    void endGpuTimer(GpuTimerPass pass);

    /// Enable or disable GPU timer queries.
    void setGpuTimerEnabled(bool enabled) { m_gpuTimerEnabled = enabled; }
    [[nodiscard]] bool isGpuTimerEnabled() const { return m_gpuTimerEnabled; }

    /// Get the latest GPU frame statistics.
    [[nodiscard]] const GpuFrameStats& getGpuFrameStats() const { return m_gpuFrameStats; }

    /// Shadow pass GPU timestamp and cascade statistics.
    [[nodiscard]] bool beginShadowFrame(int cascadeCount, int shadowResolution);
    void recordShadowCascadeStats(int cascadeIndex, const ShadowCascadeStats& stats);
    void recordShadowFrameTotals(int submitted, int culled, float maxCasterDistance);
    void markShadowTimestamp(int cascadeIndex, ShadowTimestampPoint point);
    void endShadowFrame();
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
    static constexpr size_t GPU_TIMER_RING_SIZE = 4;
    static constexpr size_t SHADOW_TIMER_CASCADE_COUNT = ShadowFrameStats::kMaxCascades;
    static constexpr size_t SHADOW_TIMER_POINT_COUNT = static_cast<size_t>(ShadowTimestampPoint::Count);

    // GPU timer state
    std::array<std::array<uint32_t, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerQueries{};
    std::array<std::array<bool, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerIssued{};
    GpuFrameStats m_gpuFrameStats{};
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerActive = false;
    bool m_gpuTimerCanIssueThisFrame = true;
    GpuTimerPass m_activeGpuTimerPass = GpuTimerPass::GBuffer;

    // Shadow timestamp state. Uses glQueryCounter(GL_TIMESTAMP) so it can run
    // inside the outer Shadow GL_TIME_ELAPSED pass timer.
    std::array<std::array<std::array<uint32_t, SHADOW_TIMER_POINT_COUNT>, SHADOW_TIMER_CASCADE_COUNT>, GPU_TIMER_RING_SIZE> m_shadowTimestampQueries{};
    std::array<std::array<std::array<bool, SHADOW_TIMER_POINT_COUNT>, SHADOW_TIMER_CASCADE_COUNT>, GPU_TIMER_RING_SIZE> m_shadowTimestampIssued{};
    std::array<ShadowFrameStats, GPU_TIMER_RING_SIZE> m_shadowFrameSlots{};
    std::array<bool, GPU_TIMER_RING_SIZE> m_shadowFrameIssued{};
    ShadowFrameStats m_shadowFrameStats{};
    uint64_t m_shadowStatsPublishCount = 0;
    bool m_shadowFrameActive = false;

    // Culling statistics
    CullingFrameStats m_cullingStats{};

    // Render work statistics
    RenderWorkStats m_workStats{};
};

#endif // MECRAFT_RENDER_DEBUG_SERVICE_H
