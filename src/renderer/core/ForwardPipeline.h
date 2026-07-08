#ifndef MECRAFT_FORWARD_PIPELINE_H
#define MECRAFT_FORWARD_PIPELINE_H

#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"
#include "../mesh/TerrainRenderer.h"

#include <vector>

class ResourceMgr;
class RhiCommandList;
class TerrainRenderer;
class TerrainRenderCache;
class WorldRenderBuffer;
class CommonFrameTargets;
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
    bool beginBackbufferFrame(const FrameContext& ctx);
    void endBackbufferFrame();
    void renderSky(const FrameContext& ctx);
    void renderTerrain(const FrameContext& ctx, const RenderSettings& settings);
    void renderEntitiesAndParticles(const FrameContext& ctx, const RenderSettings& settings);
    void renderTransparent(const FrameContext& ctx, const RenderSettings& settings);
    FrameOutput buildFrameOutput(const FrameContext& ctx);

    static TerrainFrameData buildTerrainFrameData(const FrameContext& ctx);
    static TerrainRenderSettings buildTerrainRenderSettings(const RenderSettings& settings);

    // Shared resources (non-owning, set during init)
    TerrainRenderer* m_terrainRenderer = nullptr;
    TerrainRenderCache* m_terrainCache = nullptr;
    WorldRenderBuffer* m_worldRenderBuffer = nullptr;
    CommonFrameTargets* m_commonTargets = nullptr;
    GameplaySkyRenderer* m_skyRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;
    SharedRenderResources* m_shared = nullptr;
    RhiCommandList* m_backbufferCommandList = nullptr;

    // Transparent batch state (populated by renderTerrain, consumed by renderTransparent)
    std::vector<DrawBatchEntry> m_transparentBatch;
    TransparentPassPlan m_transparentPassPlan;
    std::vector<ChunkRenderEntry> m_transparentEntries;

    bool m_initialized = false;
};

#endif // MECRAFT_FORWARD_PIPELINE_H
