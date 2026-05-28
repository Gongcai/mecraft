#ifndef MECRAFT_FORWARD_PIPELINE_H
#define MECRAFT_FORWARD_PIPELINE_H

#include "RenderPipeline.h"
#include "FrameContext.h"
#include "FrameOutput.h"

#include <memory>

class ResourceMgr;
class TerrainRenderer;
class TerrainRenderCache;
class CommonFrameTargets;
class GameplaySkyRenderer;

/// Forward rendering pipeline implementation.
/// Uses chunk_lit_common.fs with simplified lighting, no GBuffer.
/// Features: terrain draw, sky, forward fog, simplified post-process.
/// Does NOT allocate deferred resources (GBuffer/SSAO/SSR/VFog/TAA) — saves VRAM.
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
    /// Render opaque and cutout terrain
    void renderTerrain(const FrameContext& ctx);

    /// Render transparent terrain (water, glass, etc.)
    void renderTransparent(const FrameContext& ctx);

    /// Build FrameOutput from current state
    FrameOutput buildFrameOutput(const FrameContext& ctx);

    // Shared resources (non-owning, set during init)
    TerrainRenderer* m_terrainRenderer = nullptr;
    TerrainRenderCache* m_terrainCache = nullptr;
    CommonFrameTargets* m_commonTargets = nullptr;
    GameplaySkyRenderer* m_skyRenderer = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    bool m_initialized = false;
};

#endif // MECRAFT_FORWARD_PIPELINE_H
