#ifndef MECRAFT_CLUSTERED_LIGHTING_PASS_H
#define MECRAFT_CLUSTERED_LIGHTING_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/ClusteredLightingContract.h"
#include "renderer/contracts/WorldLightGridContract.h"
#include "renderer/core/FrameContext.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <array>
#include <cstdint>
#include <vector>

class RhiCommandList;
class RhiDevice;

/// Latest completed clustered-light build statistics read through a delayed
/// GPU readback ring.
struct ClusteredLightingFrameStats final {
    bool valid = false;
    uint32_t clusterCount = 0u;
    uint32_t lightCount = 0u;
    uint32_t totalIndexCount = 0u;
    uint32_t maxLightsPerCluster = 0u;
    uint32_t nonEmptyClusterCount = 0u;
    uint32_t indexCapacity = 0u;
    uint32_t buildError = 0u;
    uint32_t worldCellCount = 0u;
    uint32_t worldIndexCount = 0u;
    uint32_t worldGlobalLightCount = 0u;
    uint32_t maxWorldLightsPerCell = 0u;
    float averageLightsPerCluster = 0.0f;
};

/// Builds the compact view-space light list shared by Deferred and Forward+.
/// Persistent buffers grow before graph compilation; every GPU capacity and
/// cursor invariant is validated before consumers read the list.
class ClusteredLightingPass final : public RenderPass {
public:
    struct LocalShadowResources final {
        RhiBufferHandle metadataBuffer;
        uint64_t metadataBufferBytes = 0u;
        RhiTextureViewHandle spotAtlasView;
        RhiTextureViewHandle pointCubeArrayView;
        RhiSamplerHandle sampler;
    };

    struct GraphResources final {
        RgBufferHandle lights;
        RgBufferHandle lightBounds;
        RgBufferHandle counts;
        RgBufferHandle offsets;
        RgBufferHandle records;
        RgBufferHandle cursors;
        RgBufferHandle compactIndices;
        RgBufferHandle scanScratch;
        RgBufferHandle stats;
        RgBufferHandle worldCells;
        RgBufferHandle worldIndices;
        RgBufferHandle worldHeader;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "ClusteredLighting"; }

    /// Replaces the complete normalized light snapshot used by the next frame.
    /// Stable IDs must be unique and every record must carry the current
    /// contract version.
    /// @param lights Complete immutable GPU light snapshot.
    /// @return True when every record satisfies the fixed GPU contract.
    [[nodiscard]] bool setLights(std::vector<renderer::contracts::GpuLight> lights);

    /// Publishes the persistent local-shadow resources consumed with the
    /// clustered light list. A resource generation change rebuilds only the
    /// consumer descriptor set while preserving compute-stage bind groups.
    /// @param resources Complete metadata, atlas, cube-array, and sampler set.
    /// @return True when every handle and range is valid.
    [[nodiscard]] bool setLocalShadowResources(const LocalShadowResources& resources);

    /// Allocates persistent buffers, computes per-light coverage bounds, and
    /// prepares all bind groups before the graph imports their handles.
    /// @param rhiDevice Vulkan device that owns the modern rendering profile.
    /// @param ctx Current camera matrices and clip distances.
    /// @param renderWidth Active render width in pixels.
    /// @param renderHeight Active render height in pixels.
    /// @return True when this frame has a complete, capacity-safe build plan.
    [[nodiscard]] bool prepareGraphFrame(RhiDevice& rhiDevice, const FrameContext& ctx, uint32_t renderWidth,
                                         uint32_t renderHeight);

    /// Imports every persistent build product into the Render Graph.
    /// @param graph Graph receiving stable buffer handles.
    /// @param resources Receives graph handles used by build and consumers.
    /// @return True when all live buffers were imported.
    [[nodiscard]] bool importGraphResources(RenderGraph& graph, GraphResources& resources) const;

    /// Adds upload, count, hierarchical prefix sum, compact fill, and
    /// validation passes after one dependency.
    /// @param graph Frame Render Graph.
    /// @param resources Imported persistent buffer handles.
    /// @param dependency Pass that must complete before light upload starts.
    /// @return Final validation pass, or an invalid handle on setup failure.
    [[nodiscard]] RgPassHandle addGraphPasses(RenderGraph& graph, const GraphResources& resources,
                                              RgPassHandle dependency);

    /// Commits delayed statistics only after the graph submission succeeds.
    /// @param succeeded True when every graph command list was submitted.
    /// @param completionToken Final graph submission that depends on validation.
    void finishGraphExecution(bool succeeded, RhiSubmissionToken completionToken);

    [[nodiscard]] const renderer::contracts::ClusterGrid& grid() const { return m_grid; }
    [[nodiscard]] const ClusteredLightingFrameStats& frameStats() const { return m_frameStats; }
    [[nodiscard]] uint32_t activeLightCount() const { return static_cast<uint32_t>(m_lightBounds.size()); }
    [[nodiscard]] RhiBindGroupLayoutHandle consumerBindGroupLayout() const { return m_consumerBindGroupLayout; }
    [[nodiscard]] RhiBindGroupHandle consumerBindGroup() const { return m_consumerBindGroup; }

private:
    struct BufferResource final {
        RhiBufferHandle handle;
        uint64_t capacityBytes = 0u;
    };

    struct ComputeStage final {
        RhiShaderHandle shader;
        RhiBindGroupLayoutHandle bindGroupLayout;
        RhiPipelineLayoutHandle pipelineLayout;
        RhiPipelineHandle pipeline;
    };

    struct ScanLevel final {
        uint32_t elementCount = 0u;
        uint32_t groupCount = 0u;
        uint32_t inputOffsetWords = 0u;
        uint32_t outputOffsetWords = 0u;
        uint32_t blockSumOffsetWords = 0u;
    };

    struct WorldLightGridStatsSnapshot final {
        uint32_t cellCount = 0u;
        uint32_t indexCount = 0u;
        uint32_t globalLightCount = 0u;
        uint32_t maxLightsPerCell = 0u;
    };

    [[nodiscard]] bool validateLights() const;
    [[nodiscard]] bool consumeReadback(RhiDevice& rhiDevice);
    [[nodiscard]] bool buildCoverage(const FrameContext& ctx, uint32_t renderWidth, uint32_t renderHeight);
    [[nodiscard]] bool buildScanPlan();
    [[nodiscard]] bool ensurePipelines(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBuffers(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBuffer(RhiDevice& rhiDevice, BufferResource& resource, uint64_t requiredBytes,
                                    RhiBufferUsageFlags usage, RhiMemoryCategory memoryCategory, const char* debugName);
    [[nodiscard]] bool ensureReadbackBuffers(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBuildBindGroups(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureConsumerBindGroup(RhiDevice& rhiDevice);
    [[nodiscard]] bool importBuffer(RenderGraph& graph, const BufferResource& resource,
                                    RgBufferHandle& graphBuffer) const;

    [[nodiscard]] bool recordUpload(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordCount(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordScan(RhiCommandList& commandList, uint32_t level) const;
    [[nodiscard]] bool recordScanAdd(RhiCommandList& commandList, uint32_t childLevel) const;
    [[nodiscard]] bool recordFinalize(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordFill(RhiCommandList& commandList) const;
    [[nodiscard]] bool recordValidateAndReadback(RhiCommandList& commandList);
    [[nodiscard]] WorldLightGridStatsSnapshot captureWorldLightGridStats() const;
    void applyWorldLightGridStats(const WorldLightGridStatsSnapshot& snapshot);
    void publishEmptyFrameStats();

    void destroyBuildBindGroups();
    void destroyComputeStage(ComputeStage& stage);
    void destroyPipelines();
    void destroyBuffers();

    RhiDevice* m_rhiDevice = nullptr;
    bool m_inputValid = true;
    bool m_prepared = false;
    bool m_gpuBuildFailed = false;
    bool m_emptyBuildReady = false;
    bool m_emptyBuildScheduled = false;
    std::vector<renderer::contracts::GpuLight> m_lights;
    std::vector<renderer::contracts::GpuClusterLightBounds> m_lightBounds;
    renderer::contracts::WorldLightGridBuildResult m_worldLightGrid;
    std::vector<uint32_t> m_zeroClusterWords;
    renderer::contracts::ClusterGrid m_grid;
    glm::mat4 m_inverseProjection{1.0f};
    uint32_t m_requiredIndexCount = 0u;
    uint32_t m_indexCapacity = 0u;
    uint32_t m_scanScratchWordCount = 0u;
    std::vector<ScanLevel> m_scanLevels;

    BufferResource m_lightBuffer;
    BufferResource m_lightBoundsBuffer;
    BufferResource m_countBuffer;
    BufferResource m_offsetBuffer;
    BufferResource m_recordBuffer;
    BufferResource m_cursorBuffer;
    BufferResource m_compactIndexBuffer;
    BufferResource m_scanScratchBuffer;
    BufferResource m_statsBuffer;
    BufferResource m_worldCellBuffer;
    BufferResource m_worldIndexBuffer;
    BufferResource m_worldHeaderBuffer;

    ComputeStage m_countStage;
    ComputeStage m_scanStage;
    ComputeStage m_scanScratchStage;
    ComputeStage m_scanAddStage;
    ComputeStage m_finalizeStage;
    ComputeStage m_fillStage;
    ComputeStage m_validateStage;
    RhiBindGroupLayoutHandle m_consumerBindGroupLayout;

    RhiBindGroupHandle m_countBindGroup;
    std::vector<RhiBindGroupHandle> m_scanBindGroups;
    std::vector<RhiBindGroupHandle> m_scanAddBindGroups;
    RhiBindGroupHandle m_finalizeBindGroup;
    RhiBindGroupHandle m_fillBindGroup;
    RhiBindGroupHandle m_validateBindGroup;
    RhiBindGroupHandle m_consumerBindGroup;
    LocalShadowResources m_localShadowResources;

    static constexpr uint32_t kStatsWordCount = 8u;
    static constexpr uint32_t kStatsReadbackRingSize = 3u;
    std::array<RhiBufferHandle, kStatsReadbackRingSize> m_statsReadbackBuffers{};
    std::array<bool, kStatsReadbackRingSize> m_statsReadbackWritten{};
    std::array<RhiSubmissionToken, kStatsReadbackRingSize> m_statsReadbackTokens{};
    std::array<WorldLightGridStatsSnapshot, kStatsReadbackRingSize> m_statsReadbackWorldSnapshots{};
    uint32_t m_statsReadbackWriteIndex = 0u;
    uint32_t m_pendingStatsReadbackIndex = 0u;
    bool m_statsReadbackSlotAvailable = true;
    bool m_statsReadbackPending = false;
    // Last graph submission that may still read or write the single-buffered
    // persistent build buffers. The next prepare call blocks on it because
    // the buffers are shared across in-flight frames.
    RhiSubmissionToken m_lastBuildToken;
    ClusteredLightingFrameStats m_frameStats;
};

#endif // MECRAFT_CLUSTERED_LIGHTING_PASS_H
