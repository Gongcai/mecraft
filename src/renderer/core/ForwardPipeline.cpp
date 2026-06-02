#include "ForwardPipeline.h"
#include "RenderScene.h"
#include "../mesh/TerrainRenderCache.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../targets/CommonFrameTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../renderers/HumanoidRenderer.h"
#include "../renderers/DropRenderer.h"
#include "../../resource/ResourceMgr.h"
#include "../../engine/camera/Camera.h"
#include "../../engine/platform/Window.h"
#include "../../world/IWorldView.h"
#include "../../world/DayNightSystem.h"
#include "../../particle/ParticleSystem.h"

#include <glad/glad.h>
#include <algorithm>

ForwardPipeline::ForwardPipeline() = default;
ForwardPipeline::~ForwardPipeline() = default;

void ForwardPipeline::init(SharedRenderResources& shared) {
    m_shared = &shared;
    m_terrainRenderer = shared.terrain;
    m_terrainCache = shared.terrainCache;
    m_worldRenderBuffer = shared.worldRenderBuffer;
    m_commonTargets = shared.commonTargets;
    m_skyRenderer = shared.sky;
    m_resourceMgr = shared.resources;

    // Enable forward vanilla shaders on sub-renderers (no deferred/shaderpack contracts)
    if (shared.sky) shared.sky->setForwardMode(true);
    if (shared.dropRenderer) shared.dropRenderer->setForwardMode(true);

    m_initialized = true;
}

void ForwardPipeline::shutdown() {
    // Revert sub-renderers to deferred shaders
    if (m_shared) {
        if (m_shared->sky) m_shared->sky->setForwardMode(false);
        if (m_shared->dropRenderer) m_shared->dropRenderer->setForwardMode(false);
    }

    m_shared = nullptr;
    m_terrainRenderer = nullptr;
    m_terrainCache = nullptr;
    m_worldRenderBuffer = nullptr;
    m_commonTargets = nullptr;
    m_skyRenderer = nullptr;
    m_resourceMgr = nullptr;
    m_transparentBatch.clear();
    m_transparentPassPlan = {};
    m_transparentEntries.clear();
    m_initialized = false;
}

FrameOutput ForwardPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_initialized || !ctx.worldView) {
        return {};
    }

    beginBackbufferFrame(ctx);

    // 1. Sky background (sun, moon, clouds, gradient)
    renderSky(ctx);

    // 2. Opaque + cutout terrain
    renderTerrain(ctx, settings);

    // 3. Forward-only scene objects that should depth-test against terrain.
    renderEntitiesAndParticles(ctx, settings);

    // 4. Transparent terrain (water, glass, etc.)
    renderTransparent(ctx, settings);

    // 5. Build and return frame output
    return buildFrameOutput(ctx);
}

void ForwardPipeline::beginBackbufferFrame(const FrameContext& ctx) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glViewport(0, 0, std::max(1, ctx.frameWidth), std::max(1, ctx.frameHeight));

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ============================================================================
// Sky rendering
// ============================================================================

void ForwardPipeline::renderSky(const FrameContext& ctx) {
    if (!m_skyRenderer || !ctx.dayNightSystem || !ctx.cameraPtr) return;

    const auto& dayNight = *ctx.dayNightSystem;
    const float aspect = (ctx.frameHeight > 0)
        ? static_cast<float>(ctx.frameWidth) / static_cast<float>(ctx.frameHeight)
        : 1.0f;

    // Forward mode: pass 0 for skyCaptureTexture because sky gradient mode doesn't read it.
    m_skyRenderer->render(*ctx.cameraPtr, aspect, dayNight, 0);
}

// ============================================================================
// Terrain rendering (opaque + cutout)
// ============================================================================

void ForwardPipeline::renderTerrain(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_terrainRenderer || !m_resourceMgr || !m_worldRenderBuffer) return;

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    // Terrain cache maintenance
    if (m_terrainCache) {
        m_terrainCache->releaseStaleMdiAllocations(*ctx.worldView);
        m_terrainCache->drainMeshingResults(*ctx.worldView);
    }
    worldBuffer.beginFrame();
    terrain.clearTransparentBatches();

    // Get forward basic shader
    Shader* terrainShader = m_resourceMgr->getShader("forward_basic_terrain");
    if (!terrainShader) return;

    // Build terrain frame data from FrameContext
    TerrainFrameData tfd = buildTerrainFrameData(ctx);

    // Frustum + camera
    terrain.setCameraPos(ctx.camera.position);
    terrain.updateFrustum(ctx.camera.viewProj);

    // Terrain render settings
    TerrainRenderSettings trs = buildTerrainRenderSettings(settings);

    // Bind lightweight forward state
    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    terrain.bindBasicForwardState(tfd, texArray, *terrainShader,
                                   ctx.eyeInWater, 0/*heldBlockLight*/,
                                   m_resourceMgr, trs);

    // Submit meshing jobs
    if (m_terrainCache) {
        m_terrainCache->submitMeshingJobs(*ctx.worldView, ctx.camera.position);
    }

    // Render opaque + collect cutout/transparent entries
    std::vector<ChunkRenderEntry> cutoutEntries;
    std::vector<ChunkRenderEntry> transparentEntries;
    terrain.renderOpaqueChunksAndCollectPasses(*ctx.worldView, cutoutEntries, transparentEntries, true);
    terrain.syncTransparentBatches();

    // Save transparent batch for transparent pass
    m_transparentBatch = terrain.transparentBatches();
    m_transparentPassPlan = terrain.transparentPassPlan();
    m_transparentEntries = transparentEntries;

    // Flush MDI opaque + render cutout
    worldBuffer.flushOpaque();
    terrain.renderCutoutChunks(cutoutEntries, *terrainShader);
    worldBuffer.captureSceneFrameStats();

    // Unbind textures (units 0-4)
    glBindVertexArray(0);
    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 0);
    }
}

void ForwardPipeline::renderEntitiesAndParticles(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_shared || !ctx.cameraPtr || !ctx.windowPtr) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    if (m_shared->dropRenderer && m_shared->dropSystem) {
        m_shared->dropRenderer->render(*m_shared->dropSystem, *ctx.cameraPtr, *ctx.windowPtr);
    }

    if (m_shared->humanoidRenderer && m_shared->gameplayRegistry) {
        const auto mode = ctx.renderLocalPlayerModel
            ? HumanoidRenderer::kRenderAll
            : HumanoidRenderer::kRenderMobsOnly;
        m_shared->humanoidRenderer->render(*m_shared->gameplayRegistry, *ctx.cameraPtr, *ctx.windowPtr, mode);
    }

    if (settings.weather.particlesEnabled && m_shared->particleSystem) {
        m_shared->particleSystem->render(ctx.camera.projection, ctx.camera.view);
    }
}

// ============================================================================
// Transparent rendering (water, glass, etc.)
// ============================================================================

void ForwardPipeline::renderTransparent(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_terrainRenderer || !m_worldRenderBuffer) return;
    if (!m_transparentPassPlan.hasAny()) return;

    Shader* shader = m_resourceMgr->getShader("forward_basic_terrain");
    if (!shader) return;

    auto& terrain = *m_terrainRenderer;
    auto& worldBuffer = *m_worldRenderBuffer;

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    TerrainFrameData tfd = buildTerrainFrameData(ctx);
    TerrainRenderSettings trs = buildTerrainRenderSettings(settings);

    terrain.bindBasicForwardState(tfd, texArray, *shader,
                                   ctx.eyeInWater, 0, m_resourceMgr, trs);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // MDI path: sort back-to-front, flush transparent
    shader->setInt("uForceBaseLod", 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    std::sort(m_transparentBatch.begin(), m_transparentBatch.end(),
              [](const DrawBatchEntry& a, const DrawBatchEntry& b) {
                  return a.distanceSq > b.distanceSq;
              });
    for (const auto& entry : m_transparentBatch) {
        worldBuffer.addTransparent(entry.range);
    }
    worldBuffer.flushTransparent();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    shader->setInt("uForceBaseLod", 0);

    // Unbind
    glBindVertexArray(0);
    for (int i = 4; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// Frame output
// ============================================================================

FrameOutput ForwardPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output{};
    if (m_commonTargets) {
        output.sceneColorTex = m_commonTargets->sceneColorTexture();
        output.sceneDepthTex = m_commonTargets->sceneDepthTexture();
    }
    // Forward vanilla: no deferred inputs, skip post-process (bloom/exposure/grading)
    output.hasDeferredInputs = false;
    output.hasDebugView = false;
    output.skipPostProcess = true;
    return output;
}

// ============================================================================
// Data conversion helpers
// ============================================================================

TerrainFrameData ForwardPipeline::buildTerrainFrameData(const FrameContext& ctx) {
    TerrainFrameData tfd{};
    // Camera
    tfd.view = ctx.camera.view;
    tfd.viewProj = ctx.camera.viewProj;
    tfd.cameraPos = ctx.camera.position;
    tfd.animationTime = ctx.animationTime;
    tfd.shaderTime = ctx.shaderTime;

    // Fog (used by forward_basic_terrain.frag)
    tfd.fog.enabled = ctx.fog.enabled;
    tfd.fog.mode = ctx.fog.mode;
    tfd.fog.color = ctx.fog.color;
    tfd.fog.start = ctx.fog.startDistance;
    tfd.fog.end = ctx.fog.endDistance;
    tfd.fog.density = ctx.fog.density;

    // Only skyIntensity needed for day/night lightmap interpolation
    tfd.skyLighting.skyIntensity = ctx.skyIntensity;

    return tfd;
}

TerrainRenderSettings ForwardPipeline::buildTerrainRenderSettings(const RenderSettings& /*settings*/) {
    // Forward vanilla: TerrainRenderSettings is unused by bindBasicForwardState.
    // Return defaults. No deferred/shaderpack parameters are read.
    return TerrainRenderSettings{};
}
