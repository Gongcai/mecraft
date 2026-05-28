#include "ForwardPipeline.h"
#include "RenderScene.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../targets/CommonFrameTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../../resource/ResourceMgr.h"

#include <glad/glad.h>

ForwardPipeline::ForwardPipeline() = default;
ForwardPipeline::~ForwardPipeline() = default;

void ForwardPipeline::init(SharedRenderResources& shared) {
    m_terrainRenderer = shared.terrain;
    m_terrainCache = shared.terrainCache;
    m_commonTargets = shared.commonTargets;
    m_skyRenderer = shared.sky;
    m_resourceMgr = shared.resources;
    m_initialized = true;
}

void ForwardPipeline::shutdown() {
    m_terrainRenderer = nullptr;
    m_terrainCache = nullptr;
    m_commonTargets = nullptr;
    m_skyRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_initialized = false;
}

FrameOutput ForwardPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& /*settings*/) {
    if (!m_initialized || !ctx.shared) {
        return {};
    }

    // Step 1: Render sky
    if (m_skyRenderer && ctx.world) {
        // Sky rendering requires camera and window parameters
        // For now, sky is rendered through the shared resources
        // m_skyRenderer->render(...);
    }

    // Step 2: Render terrain (opaque + cutout)
    renderTerrain(ctx);

    // Step 3: Render transparent terrain
    renderTransparent(ctx);

    // Step 4: Build and return frame output
    return buildFrameOutput(ctx);
}

void ForwardPipeline::renderTerrain(const FrameContext& ctx) {
    if (!m_terrainRenderer || !m_resourceMgr) {
        return;
    }

    // Bind forward shader and render state
    // The terrain renderer handles:
    // - Frustum culling
    // - MDI draw commands or direct draw calls
    // - Opaque and cutout geometry

    // Note: In the full implementation, this would call:
    // m_terrainRenderer->bindForwardRenderState(ctx);
    // m_terrainRenderer->renderOpaqueChunksAndCollectPasses(...);
    // m_terrainRenderer->renderCutoutChunks(...);
}

void ForwardPipeline::renderTransparent(const FrameContext& ctx) {
    if (!m_terrainRenderer) {
        return;
    }

    // Render transparent geometry (water, glass, etc.)
    // In forward mode, transparent is rendered with simple alpha blending
    // Note: In the full implementation, this would call:
    // m_terrainRenderer->renderTransparentChunks(...);
    // m_terrainRenderer->renderWaterChunks(...);
}

FrameOutput ForwardPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output;

    // Forward pipeline outputs to the common scene color target
    if (m_commonTargets) {
        output.sceneColorTex = m_commonTargets->sceneColorTexture();
        output.sceneDepthTex = m_commonTargets->sceneDepthTexture();
    }

    // Forward pipeline does not produce deferred inputs
    output.hasDeferredInputs = false;
    output.hasDebugView = false;
    output.skipPostProcess = false;

    return output;
}
