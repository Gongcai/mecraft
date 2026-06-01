#ifndef MECRAFT_RENDER_DEBUG_SERVICE_H
#define MECRAFT_RENDER_DEBUG_SERVICE_H

#include <glad/glad.h>
#include <array>
#include <cstddef>
#include <cstdint>

/// GPU timer pass identifiers for profiling different rendering stages.
enum class GpuTimerPass : size_t {
    GBuffer = 0,
    Shadow = 1,
    Ssao = 2,
    Lighting = 3,
    Transparent = 4,
    Volumetric = 5,
    Reflection = 6,
    Cloud = 7,
    Water = 8,
    Post = 9,
    Count = 10
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
    double lightingMs = 0.0;
    double transparentMs = 0.0;
    double volumetricMs = 0.0;
    double reflectionMs = 0.0;
    double cloudMs = 0.0;
    double waterMs = 0.0;
    double postMs = 0.0;
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

    // GPU timer state
    std::array<std::array<GLuint, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerQueries{};
    std::array<std::array<bool, static_cast<size_t>(GpuTimerPass::Count)>, GPU_TIMER_RING_SIZE> m_gpuTimerIssued{};
    GpuFrameStats m_gpuFrameStats{};
    size_t m_gpuTimerWriteIndex = 0;
    bool m_gpuTimersInitialized = false;
    bool m_gpuTimerEnabled = true;
    bool m_gpuTimerActive = false;
    bool m_gpuTimerCanIssueThisFrame = true;
    GpuTimerPass m_activeGpuTimerPass = GpuTimerPass::GBuffer;

    // Culling statistics
    CullingFrameStats m_cullingStats{};

    // Render work statistics
    RenderWorkStats m_workStats{};
};

#endif // MECRAFT_RENDER_DEBUG_SERVICE_H
