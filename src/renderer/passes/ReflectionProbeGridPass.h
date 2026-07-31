#ifndef MECRAFT_REFLECTION_PROBE_GRID_PASS_H
#define MECRAFT_REFLECTION_PROBE_GRID_PASS_H

#include "RenderPass.h"
#include "renderer/contracts/ReflectionProbeContract.h"
#include "renderer/rhi/RhiHandles.h"
#include "renderer/rhi/RhiRenderGraph.h"

#include <cstdint>
#include <string>
#include <vector>

class RhiCommandList;
class RhiDevice;

/// Owns the GPU spatial grid consumed by deferred reflection shading.
class ReflectionProbeGridPass final : public RenderPass {
public:
    struct GraphResources final {
        RgBufferHandle probes;
        RgBufferHandle metadata;
        RgBufferHandle cells;
        RgBufferHandle indices;
        RgTextureHandle prefilteredCubeArray;
    };

    struct ConsumerResources final {
        RhiBufferHandle probeBuffer;
        uint64_t probeBufferBytes = 0u;
        RhiBufferHandle metadataBuffer;
        uint64_t metadataBufferBytes = 0u;
        RhiBufferHandle cellBuffer;
        uint64_t cellBufferBytes = 0u;
        RhiBufferHandle indexBuffer;
        uint64_t indexBufferBytes = 0u;
        RhiTextureViewHandle prefilteredCubeArrayView;
        uint32_t activeProbeCount = 0u;
    };

    void shutdown() override;
    [[nodiscard]] const char* name() const override {
        return "ReflectionProbeGrid";
    }

    /// Replaces the complete normalized scene-probe snapshot.
    /// @param probes Camera-relative probe records for the current scene state.
    void setSceneProbes(
        std::vector<renderer::contracts::GpuReflectionProbe> probes);

    /// Publishes a completed prefiltered cubemap array owned by the capture
    /// producer. The array must use the fixed extent, mip count, and RGBA16F
    /// format before an active probe can reference it.
    /// @param texture Completed sampled CubeArray texture.
    /// @param view CubeArray view spanning every completed capture slot.
    void setPrefilteredCubeArray(RhiTextureHandle texture,
                                 RhiTextureViewHandle view);

    /// Builds the spatial grid and ensures every upload resource is valid.
    /// @param rhiDevice Device that owns buffers and descriptor resources.
    /// @return True when the complete snapshot is ready for graph import.
    [[nodiscard]] bool prepareGraphFrame(RhiDevice& rhiDevice);

    /// Imports persistent grid buffers and the selected cubemap array.
    [[nodiscard]] bool importGraphResources(
        RenderGraph& graph,
        GraphResources& resources) const;

    /// Adds the transactional CPU-to-GPU grid upload when data changed.
    [[nodiscard]] RgPassHandle addGraphPasses(
        RenderGraph& graph,
        const GraphResources& resources,
        RgPassHandle dependency);

    /// Commits uploaded buffer contents after complete graph submission.
    /// @param succeeded True when recording and submission completed.
    void finishGraphExecution(bool succeeded);

    [[nodiscard]] ConsumerResources consumerResources() const;
    [[nodiscard]] uint32_t activeProbeCount() const {
        return static_cast<uint32_t>(m_grid.probes.size());
    }
    [[nodiscard]] const std::string& lastError() const {
        return m_lastError;
    }

private:
    struct BufferResource final {
        RhiBufferHandle handle;
        uint64_t capacityBytes = 0u;
    };

    [[nodiscard]] bool ensureResources(RhiDevice& rhiDevice);
    [[nodiscard]] bool ensureBuffer(RhiDevice& rhiDevice,
                                    BufferResource& resource,
                                    uint64_t requiredBytes,
                                    const char* debugName);
    [[nodiscard]] bool ensureEmptyCubeArray(RhiDevice& rhiDevice);
    [[nodiscard]] bool selectCubeArray(RhiDevice& rhiDevice);
    [[nodiscard]] bool importBuffer(RenderGraph& graph,
                                    const BufferResource& resource,
                                    RgBufferHandle& graphBuffer) const;
    [[nodiscard]] bool recordUpload(RhiCommandList& commandList) const;
    void destroyOwnedResources();

    RhiDevice* m_rhiDevice = nullptr;
    std::vector<renderer::contracts::GpuReflectionProbe> m_sceneProbes;
    renderer::contracts::ReflectionProbeGrid m_grid;
    uint64_t m_sceneRevision = 1u;
    uint64_t m_preparedRevision = 0u;

    BufferResource m_probeBuffer;
    BufferResource m_metadataBuffer;
    BufferResource m_cellBuffer;
    BufferResource m_indexBuffer;

    RhiTextureHandle m_emptyCubeArrayTexture;
    RhiTextureViewHandle m_emptyCubeArrayView;
    RhiTextureHandle m_externalCubeArrayTexture;
    RhiTextureViewHandle m_externalCubeArrayView;
    RhiTextureHandle m_consumerCubeArrayTexture;
    RhiTextureViewHandle m_consumerCubeArrayView;

    bool m_prepared = false;
    bool m_uploadRequired = true;
    bool m_uploadScheduled = false;
    std::string m_lastError;
};

#endif // MECRAFT_REFLECTION_PROBE_GRID_PASS_H
