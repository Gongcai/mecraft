#ifndef MECRAFT_FORWARD_PIPELINE_H
#define MECRAFT_FORWARD_PIPELINE_H

#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"
#include "../mesh/TerrainRenderer.h"
#include "../rhi/RhiRenderGraph.h"

#include <vector>

class ResourceMgr;
class RhiCommandList;
class TerrainRenderer;
class TerrainRenderCache;
class WorldRenderBuffer;
class GameplaySkyRenderer;

/// Forward rendering pipeline - vanilla/basic fallback renderer.
/// No skyCapture, atmosphereLut, shadow maps, SSAO, SSR, volumetric, or deferred resources.
/// Renders: sky gradient, opaque/cutout terrain, transparent terrain, simple fog.
class ForwardPipeline : public RenderPipeline {
public:
    ForwardPipeline();
    ~ForwardPipeline() override;

    // RenderPipeline interface
    void init(SharedRenderResources& shared) override;
    void shutdown() override;
    FrameOutput renderFrame(const FrameContext& ctx, const RenderSettings& settings) override;
    const char* name() const override { return "Forward (Vanilla)"; }
    bool supportsDeferred() const override { return false; }
    bool supportsDebugView() const override { return false; }

private:
    [[nodiscard]] bool executeFrameGraph(const FrameContext& ctx, const RenderSettings& settings);
    [[nodiscard]] bool prepareSceneTlas();
    [[nodiscard]] bool prepareGraphFrame(const FrameContext& ctx, const RenderSettings& settings,
                                         RhiCommandList& commandList);
    [[nodiscard]] bool recordSkyPass(const FrameContext& ctx, RhiCommandList& commandList);
    [[nodiscard]] bool recordScenePass(const FrameContext& ctx, const RenderSettings& settings,
                                       RhiCommandList& commandList);
    bool prepareTerrain(const FrameContext& ctx, RhiCommandList& commandList);
    void renderSky(const FrameContext& ctx, RhiCommandList& commandList);
    void renderTerrain(RhiCommandList& commandList);
    void renderEntitiesAndParticles(const FrameContext& ctx, const RenderSettings& settings,
                                    RhiCommandList& commandList);
    void renderTransparent(RhiCommandList& commandList);
    FrameOutput buildFrameOutput(const FrameContext& ctx);

    static TerrainFrameData buildTerrainFrameData(const FrameContext& ctx);

    // Shared resources (non-owning, set during init)
    TerrainRenderer* m_terrainRenderer = nullptr;
    TerrainRenderCache* m_terrainCache = nullptr;
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    GameplaySkyRenderer* m_skyRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    SharedRenderResources* m_shared = nullptr;

    // Transparent batch state (populated by renderTerrain, consumed by renderTransparent)
    std::vector<DrawBatchEntry> m_transparentBatch;
    TransparentPassPlan m_transparentPassPlan;
    RenderGraph m_renderGraph;

    bool m_initialized = false;
};

#endif // MECRAFT_FORWARD_PIPELINE_H
