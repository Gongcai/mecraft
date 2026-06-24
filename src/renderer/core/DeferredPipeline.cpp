#include "DeferredPipeline.h"
#include "RenderScene.h"
#include "FrameOutput.h"
#include "../debug/RenderDebugLabels.h"
#include "../debug/RenderDebugService.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../../world/World.h"
#include "../../particle/ParticleSystem.h"

#include <glad/glad.h>

#include <algorithm>

namespace {
class ScopedGpuTimer {
public:
    ScopedGpuTimer(RenderDebugService* service, const GpuTimerPass pass)
        : m_service(service), m_pass(pass) {
        if (m_service != nullptr) {
            m_started = m_service->beginGpuTimer(m_pass);
        }
    }

    ~ScopedGpuTimer() {
        if (m_service != nullptr && m_started) {
            m_service->endGpuTimer(m_pass);
        }
    }

    ScopedGpuTimer(const ScopedGpuTimer&) = delete;
    ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;

private:
    RenderDebugService* m_service = nullptr;
    GpuTimerPass m_pass = GpuTimerPass::GBuffer;
    bool m_started = false;
};
} // namespace

void DeferredPipeline::init(ResourceMgr& resourceMgr, shadow::ShadowRenderer* shadowRenderer) {
    m_resourceMgr = &resourceMgr;
    m_shadowRenderer = shadowRenderer;

    m_skyCapturePass = std::make_unique<SkyCapturePass>();
    m_gbufferPass = std::make_unique<GBufferPass>();
    m_shadowPass = std::make_unique<ShadowPass>();
    m_waterCompositePass = std::make_unique<WaterCompositePass>();
    m_velocityPass = std::make_unique<VelocityPass>();
    m_ssaoPass = std::make_unique<SsaoPass>();
    m_lightingPass = std::make_unique<DeferredLightingPass>();
    m_reflectionPass = std::make_unique<ReflectionPass>();
    m_cloudPass = std::make_unique<CloudPass>();
    m_sceneCompositePass = std::make_unique<SceneCompositePass>();
    m_volumetricPass = std::make_unique<VolumetricPass>();
    m_taaPass = std::make_unique<TemporalResolvePass>();
    m_motionBlurPass = std::make_unique<MotionBlurPass>();
    m_dofPass = std::make_unique<DepthOfFieldPass>();
    m_debugPass = std::make_unique<DebugPass>();

    m_skyCapturePass->init(resourceMgr);
    m_gbufferPass->init(resourceMgr);
    m_shadowPass->init(resourceMgr);
    m_waterCompositePass->init(resourceMgr);
    m_velocityPass->init(resourceMgr);
    m_ssaoPass->init(resourceMgr);
    m_lightingPass->init(resourceMgr);
    m_reflectionPass->init(resourceMgr);
    m_cloudPass->init(resourceMgr);
    m_sceneCompositePass->init(resourceMgr);
    m_volumetricPass->init(resourceMgr);
    m_taaPass->init(resourceMgr);
    m_motionBlurPass->init(resourceMgr);
    m_dofPass->init(resourceMgr);
    m_debugPass->init(resourceMgr);

    if (shadowRenderer) {
        m_shadowPass->setShadowRenderer(shadowRenderer);
        m_lightingPass->setShadowRenderer(shadowRenderer);
        m_volumetricPass->setShadowRenderer(shadowRenderer);
        m_debugPass->setShadowRenderer(shadowRenderer);
    }

    // Passes that consume renderer-owned state receive it through execute().
}

void DeferredPipeline::init(SharedRenderResources& shared) {
    // Store shared resources pointer
    m_shared = &shared;

    // Extract ResourceMgr and ShadowRenderer from shared resources
    if (shared.resources) {
        m_resourceMgr = shared.resources;
        m_shadowRenderer = shared.shadowRenderer;
        init(*m_resourceMgr, m_shadowRenderer);
    }

    // Inject ShadowPass dependencies from shared resources
    if (m_shadowPass) {
        m_shadowPass->setTerrainRenderer(shared.terrain);
        m_shadowPass->setWorldRenderBuffer(shared.worldRenderBuffer);
        m_shadowPass->setBlockEntityRenderer(shared.blockEntityRenderer);
        m_shadowPass->setHumanoidRenderer(shared.humanoidRenderer);
        m_shadowPass->setDropRenderer(shared.dropRenderer);
        m_shadowPass->setFallingBlockRenderer(shared.fallingBlockRenderer);
        m_shadowPass->setDropSystem(shared.dropSystem);
        m_shadowPass->setGameplayRegistry(shared.gameplayRegistry);
    }
}

void DeferredPipeline::shutdown() {
    m_dofPass.reset();
    m_motionBlurPass.reset();
    m_taaPass.reset();
    m_volumetricPass.reset();
    m_sceneCompositePass.reset();
    m_cloudPass.reset();
    m_reflectionPass.reset();
    m_lightingPass.reset();
    m_ssaoPass.reset();
    m_velocityPass.reset();
    m_shadowPass.reset();
    m_waterCompositePass.reset();
    m_gbufferPass.reset();
    m_skyCapturePass.reset();
    m_debugPass.reset();
}

void DeferredPipeline::invalidateHistory() {
    m_hasPreviousFrameData = false;
    m_deferredHistoryUpdatedThisFrame = false;
    if (m_cloudPass) {
        m_cloudPass->invalidateHistory();
    }
    if (m_volumetricPass) {
        m_volumetricPass->invalidateHistory();
    }
}

FrameOutput DeferredPipeline::renderFrame(const FrameContext& ctx, const RenderSettings& settings) {
    // Pre-condition checks
    if (!m_shared || !m_resourceMgr || !m_shared->deferredTargets) {
        return {};
    }

    auto& targets = *m_shared->deferredTargets;
    const int windowWidth = ctx.frameWidth;
    const int windowHeight = ctx.frameHeight;

    // Use settings from RenderScene
    m_currentSettings = settings;

    // Per-frame state reset
    m_deferredHistoryUpdatedThisFrame = false;
    m_waterRenderedBeforeTemporal = false;

    // Capture current framebuffer for later restore
    captureCurrentFramebuffer();

    // Ensure deferred targets are sized correctly
    if (!targets.ensureSize(windowWidth, windowHeight, m_currentSettings.shadow.resolution)) {
        restoreCapturedFramebufferViewport(windowWidth, windowHeight);
        return {};
    }

    // After resize/rebuild, invalidate temporal history
    if (targets.consumeRebuiltFlag()) {
        m_hasPreviousFrameData = false;
        if (m_cloudPass) {
            m_cloudPass->invalidateHistory();
        }
        if (m_volumetricPass) {
            m_volumetricPass->invalidateHistory();
        }
    }

    // Clear auxiliary targets
    clearDeferredAuxiliaryTargets();

    // Sky capture
    if (m_skyCapturePass && m_shared->sky) {
        renderer::debug::ScopedDebugGroup passGroup("SkyCapture");
        m_skyCapturePass->execute(*ctx.dayNightSystem, *ctx.weatherSystem, targets, *m_shared->sky,
                                  m_resourceMgr, ctx.camera.position.y,
                                  ctx.shaderTime, ctx.camera.position,
                                  m_currentSettings.cloud.timeScale);
    }

    m_deferredFrameActive = true;

    // GBuffer terrain
    {
        renderer::debug::ScopedDebugGroup passGroup("GBuffer");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::GBuffer);
        renderGBufferTerrain(ctx, m_currentSettings);

        // Entity and drop GBuffer
        if (m_gbufferPass) {
            m_gbufferPass->executeEntities(*ctx.worldView, ctx, m_currentSettings,
                                           targets,
                                           m_shared->humanoidRenderer,
                                           m_shared->gameplayRegistry,
                                           ctx.renderLocalPlayerModel);
            m_gbufferPass->executeBlockEntities(*ctx.worldView, ctx, m_currentSettings,
                                                targets,
                                                m_shared->blockEntityRenderer);
            m_gbufferPass->executeDrops(*ctx.worldView, ctx, m_currentSettings,
                                        targets,
                                        m_shared->dropRenderer, m_shared->dropSystem);
            m_gbufferPass->executeFallingBlocks(*ctx.worldView, ctx, m_currentSettings,
                                                targets,
                                                m_shared->fallingBlockRenderer, m_shared->gameplayRegistry);
        }
    }

    // Velocity pass
    if (m_velocityPass) {
        renderer::debug::ScopedDebugGroup passGroup("Velocity");
        m_velocityPass->execute(ctx, m_currentSettings, targets);
    }

    // Shadow pass
    if (m_shadowPass && m_currentSettings.shadow.enabled && m_shadowPass->hasShaders() &&
        m_shared->shadowRenderer && ctx.worldView) {
        renderer::debug::ScopedDebugGroup passGroup("Shadow");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Shadow);
        const bool useMultiDrawIndirect = m_shared->terrain != nullptr
            ? m_shared->terrain->useMultiDrawIndirect()
            : true;
        auto shadowOutput = m_shadowPass->execute(
            ctx, m_currentSettings, targets, *ctx.worldView,
            m_transparentBatch, m_transparentPassPlan, useMultiDrawIndirect);
        m_transparentBatch = std::move(shadowOutput.transparentBatch);
        m_transparentPassPlan = shadowOutput.transparentPlan;
    }

    // SSAO pass
    if (m_ssaoPass) {
        renderer::debug::ScopedDebugGroup passGroup("SSAO");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Ssao);
        m_ssaoPass->execute(ctx, m_currentSettings, targets);
    }

    // Copy forward alpha to scene lighting
    const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : windowWidth;
    const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : windowHeight;
    targets.copyFramebufferColorToSceneLighting(m_capturedFramebuffer, capturedWidth, capturedHeight);

    // Deferred lighting
    if (m_lightingPass) {
        renderer::debug::ScopedDebugGroup passGroup("DeferredLighting");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Lighting);
        m_lightingPass->setHeldBlockLightValue(m_heldBlockLightValue);
        m_lightingPass->execute(ctx, m_currentSettings, targets);
    }

    // Debug early-out for deferred light debug mode
    if (m_currentSettings.debug.deferredLightDebugMode > 0) {
        targets.copySceneLightingToSceneComposite();
        targets.copySceneCompositeToTransparentComposite();
        targets.copySceneCompositeToSceneResolved();
        updateDeferredHistoryTargets();
        targets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        targets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        restoreCapturedFramebufferViewport(windowWidth, windowHeight);
        return buildFrameOutput(ctx);
    }

    // Reflection pass
    if (m_reflectionPass) {
        renderer::debug::ScopedDebugGroup passGroup("Reflection");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Reflection);
        m_reflectionPass->execute(ctx, m_currentSettings, targets);
    }

    // Cloud pass
    if (m_cloudPass) {
        renderer::debug::ScopedDebugGroup passGroup("Cloud");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Cloud);
        m_cloudPass->execute(ctx, m_currentSettings, targets);
    }

    // Scene composite
    if (m_sceneCompositePass) {
        renderer::debug::ScopedDebugGroup passGroup("SceneComposite");
        m_sceneCompositePass->execute(ctx, m_currentSettings, targets);
    }
    targets.copySceneCompositeToTransparentComposite();
    targets.copySceneCompositeToSceneResolved();

    // Reflection debug early-out
    if (m_currentSettings.debug.reflectionDebugMode > 0) {
        updateDeferredHistoryTargets();
        targets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        targets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
        restoreCapturedFramebufferViewport(windowWidth, windowHeight);
        return buildFrameOutput(ctx);
    }

    // Water composite (before TAA)
    if (m_currentSettings.taa.enabled) {
        renderer::debug::ScopedDebugGroup passGroup("WaterComposite.PreTAA");
        renderWaterCompositePass(ctx, true);
    }

    // Generic transparent terrain (glass, stained glass) before temporal resolve.
    {
        renderer::debug::ScopedDebugGroup passGroup("Transparent.Generic");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Transparent);
        renderGenericTransparentPass(ctx);
    }

    // Particles
    {
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Transparent);
        renderParticlesToSceneResolved(ctx);
    }
    targets.copySceneCompositeToSceneResolved();

    // Volumetric fog
    if (m_volumetricPass) {
        renderer::debug::ScopedDebugGroup passGroup("Volumetric");
        ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Volumetric);
        m_volumetricPass->execute(ctx, m_currentSettings, targets, m_hasPreviousFrameData);
    }

    // TAA resolve
    if (m_taaPass && m_currentSettings.taa.enabled && m_hasPreviousFrameData) {
        renderer::debug::ScopedDebugGroup passGroup("TemporalResolve");
        m_taaPass->execute(ctx, m_currentSettings, targets);
    }

    // Motion blur
    if (m_motionBlurPass && m_currentSettings.postProcess.motionBlurEnabled && m_hasPreviousFrameData) {
        renderer::debug::ScopedDebugGroup passGroup("MotionBlur");
        m_motionBlurPass->execute(ctx, m_currentSettings, targets);
    }

    // Depth of field
    if (m_dofPass && m_currentSettings.postProcess.dofEnabled) {
        renderer::debug::ScopedDebugGroup passGroup("DepthOfField");
        m_dofPass->execute(ctx, m_currentSettings, targets);
    }

    // Final history update and blit
    updateDeferredHistoryTargets();
    targets.copySceneResolvedToTransparentComposite();
    if (m_currentSettings.debug.viewMode > 0 && m_debugPass) {
        m_debugPass->execute(ctx, m_currentSettings, targets, m_capturedFramebuffer, capturedWidth, capturedHeight);
    } else {
        targets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
    }
    targets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);

    // Match the legacy split path: if water was not rendered before temporal
    // resolve, composite it over the already-blitted final scene.
    if (!m_waterRenderedBeforeTemporal) {
        renderer::debug::ScopedDebugGroup passGroup("WaterComposite.PostTAAFallback");
        renderWaterCompositePass(ctx, false);
    }

    restoreCapturedFramebufferViewport(windowWidth, windowHeight);

    // Mark that we now have valid previous frame data for temporal effects
    m_hasPreviousFrameData = true;

    return buildFrameOutput(ctx);
}

void DeferredPipeline::captureCurrentFramebuffer() {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_capturedFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_capturedViewport);
}

void DeferredPipeline::restoreCapturedFramebufferViewport(int windowWidth, int windowHeight) {
    if (!m_shared || !m_shared->deferredTargets) return;
    const int fallbackWidth = std::max(1, windowWidth);
    const int fallbackHeight = std::max(1, windowHeight);
    const int width = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : fallbackWidth;
    const int height = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : fallbackHeight;
    m_shared->deferredTargets->bindDefaultLike(m_capturedFramebuffer, width, height);
}

void DeferredPipeline::clearDeferredAuxiliaryTargets() {
    if (!m_shared || !m_shared->deferredTargets) return;
    auto& targets = *m_shared->deferredTargets;

    targets.bindReflection();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    targets.bindCloud();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    targets.bindSceneComposite();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    targets.bindSceneResolved();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    targets.clearWeatherMask();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void DeferredPipeline::renderGBufferTerrain(const FrameContext& ctx, const RenderSettings& settings) {
    if (!m_shared || !m_shared->deferredTargets || !m_shared->terrain ||
        !m_shared->worldRenderBuffer || !m_shared->resources) {
        return;
    }

    auto& targets = *m_shared->deferredTargets;
    auto& terrain = *m_shared->terrain;
    auto& worldBuffer = *m_shared->worldRenderBuffer;

    // Bind GBuffer and clear
    targets.bindGBuffer();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    constexpr GLfloat clearAlbedo[] = {0.0f, 0.0f, 0.0f, 0.0f};
    constexpr GLfloat clearNormal[] = {0.5f, 0.5f, 1.0f, 1.0f};
    constexpr GLfloat clearLight[] = {0.0f, 0.0f, 0.0f, 1.0f};
    constexpr GLfloat clearMaterial[] = {0.86f, 0.035f, 0.0f, 0.0f};
    constexpr GLfloat clearMaterialAux[] = {0.0f, 0.0f, 0.65f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, clearAlbedo);
    glClearBufferfv(GL_COLOR, 1, clearNormal);
    glClearBufferfv(GL_COLOR, 2, clearLight);
    glClearBufferfv(GL_COLOR, 3, clearMaterial);
    glClearBufferfv(GL_COLOR, 4, clearMaterialAux);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Terrain cache operations
    if (m_shared->terrainCache) {
        m_shared->terrainCache->releaseStaleMdiAllocations(*ctx.worldView);
        m_shared->terrainCache->drainMeshingResults(*ctx.worldView);
    }
    worldBuffer.beginFrame();
    terrain.clearTransparentBatches();

    // Get GBuffer shader from resource manager
    Shader* gbufferShader = m_shared->resources->getShader("chunk_gbuffer");
    if (!gbufferShader) return;

    // Build terrain frame data from FrameContext
    TerrainFrameData tfd;
    tfd.view = ctx.camera.view;
    tfd.viewProj = ctx.camera.viewProj;
    tfd.cameraPos = ctx.camera.position;
    tfd.animationTime = ctx.animationTime;
    tfd.shaderTime = ctx.shaderTime;
    tfd.surfaceWetness = ctx.weather.surfaceWetness;
    tfd.fog.enabled = ctx.fog.enabled;
    tfd.fog.color = ctx.fog.color;
    tfd.fog.start = ctx.fog.startDistance;
    tfd.fog.end = ctx.fog.endDistance;
    tfd.fog.density = ctx.fog.density;
    tfd.skyLighting.cameraPos = ctx.camera.position;
    tfd.skyLighting.sunDirection = ctx.skyColors.sunDirection;
    tfd.skyLighting.moonDirection = ctx.skyColors.moonDirection;
    tfd.skyLighting.sunLightColor = ctx.skyColors.sunLightColor;
    tfd.skyLighting.moonLightColor = ctx.skyColors.moonLightColor;
    tfd.skyLighting.skyAmbientColor = ctx.skyColors.skyAmbientColor;
    tfd.skyLighting.shadowTintColor = ctx.skyColors.shadowTintColor;
    tfd.skyLighting.horizonScatterColor = ctx.skyColors.horizonScatterColor;
    tfd.skyLighting.skyIntensity = ctx.skyIntensity;
    tfd.skyLighting.moonVisibility = ctx.skyColors.moonVisibility;
    tfd.skyLighting.directIlluminance = ctx.skyIlluminance.directIlluminance;
    tfd.skyLighting.skyIlluminance = ctx.skyIlluminance.skyIlluminance;
    tfd.skyLighting.sunIlluminance = ctx.skyIlluminance.sunIlluminance;
    tfd.skyLighting.moonIlluminance = ctx.skyIlluminance.moonIlluminance;
    tfd.skyLighting.cloudDynamicWeather = ctx.skyIlluminance.cloudDynamicWeather;
    tfd.atmosphere.aerialStrength = ctx.atmosphere.aerialStrength;
    tfd.atmosphere.horizonScatterStrength = ctx.atmosphere.horizonScatterStrength;
    tfd.atmosphere.sunWarmth = ctx.atmosphere.sunWarmth;
    tfd.atmosphere.skyCoolness = ctx.atmosphere.skyCoolness;
    tfd.atmosphere.weatherWetness = ctx.weather.wetness;
    tfd.atmosphere.weatherStorm = ctx.weather.storm;
    tfd.atmosphere.aerialReduction = ctx.weather.aerialReduction;
    tfd.atmosphere.lightningFlash = ctx.weather.lightningFlash;
    tfd.atmosphere.surfaceWetness = ctx.weather.surfaceWetness;
    tfd.atmosphere.skyWetness = ctx.weather.skyWetness;
    tfd.atmosphere.fogWetness = ctx.weather.fogWetness;
    tfd.atmosphere.cloudWetness = ctx.weather.cloudWetness;
    tfd.atmosphere.precipitation = ctx.weather.precipitation;
    tfd.atmosphere.directWeatherOcclusion = ctx.atmosphere.directWeatherOcclusion;
    tfd.atmosphere.directWeatherOcclusionOverride = ctx.atmosphere.directWeatherOcclusionOverride;

    terrain.setCameraPos(ctx.camera.position);
    terrain.updateFrustum(ctx.camera.viewProj);

    // Build terrain render settings from RenderSettings
    TerrainRenderSettings trs;
    trs.rainWetSurfacesEnabled = settings.weather.wetSurfacesEnabled;
    trs.rainSurfaceRipplesEnabled = settings.weather.surfaceRipplesEnabled;
    trs.aerialPerspectiveEnabled = settings.postProcess.aerialPerspectiveEnabled;
    trs.volumetricLightEnabled = settings.volumetric.lightEnabled;
    trs.volumetricFogEnabled = settings.volumetric.fogEnabled;
    trs.volumetricFogStrength = settings.volumetric.fogStrength;
    trs.directSunStrength = settings.postProcess.directSunStrength;
    trs.skyAmbientStrength = settings.postProcess.skyAmbientStrength;
    trs.weatherSkylightScale = settings.weather.skylightScale;
    trs.minimumAmbient = settings.postProcess.minimumAmbient;
    trs.blockLightStrength = settings.postProcess.blockLightStrength;
    trs.fakeBounceStrength = settings.postProcess.fakeBounceStrength;
    trs.albedoDesaturation = settings.postProcess.albedoDesaturation;
    trs.shadowDesaturation = settings.postProcess.shadowDesaturation;

    const TextureArray& texArray = m_shared->resources->getTextureArray();
    const bool volFogShadersReady = m_volumetricPass && m_volumetricPass->hasShaders();
    terrain.bindChunkRenderState(tfd, texArray, *gbufferShader,
                                  m_deferredFrameActive, settings.debug.viewMode,
                                  ctx.eyeInWater, m_heldBlockLightValue,
                                  targets, m_resourceMgr,
                                  volFogShadersReady, trs);

    if (settings.taa.enabled) {
        gbufferShader->setMat4("viewProj", ctx.camera.jitteredViewProj);
    }

    // Submit meshing jobs
    if (m_shared->terrainCache) {
        m_shared->terrainCache->submitMeshingJobs(*ctx.worldView, ctx.camera.position);
    }

    // Render opaque chunks and collect cutout/transparent entries
    std::vector<ChunkRenderEntry> cutoutEntries;
    std::vector<ChunkRenderEntry> transparentEntries;
    cutoutEntries.reserve(ctx.worldView->getActiveChunks().size() * 2);
    transparentEntries.reserve(ctx.worldView->getActiveChunks().size() * 2);
    terrain.renderOpaqueChunksAndCollectPasses(*ctx.worldView, cutoutEntries, transparentEntries, true);
    terrain.syncTransparentBatches();

    // Save transparent batch for water/transparent passes
    m_transparentBatch = terrain.transparentBatches();
    m_transparentPassPlan = terrain.transparentPassPlan();
    m_transparentEntries = transparentEntries;

    // Flush MDI buffer
    worldBuffer.flushOpaque();

    // Render cutout chunks
    terrain.renderCutoutChunks(cutoutEntries, *gbufferShader);
    worldBuffer.captureSceneFrameStats();

    // Unbind textures
    glBindVertexArray(0);
    for (int i = 10; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, 0);
    }
}

void DeferredPipeline::renderGenericTransparentPass(const FrameContext& ctx) {
    if (!m_shared || !m_shared->deferredTargets || !m_shared->worldRenderBuffer ||
        !m_shared->terrain || !m_resourceMgr || !m_transparentPassPlan.hasGeneric()) {
        return;
    }

    Shader* shader = m_resourceMgr->getShader("transparent_composite");
    if (shader == nullptr) {
        shader = m_resourceMgr->getShader("forward_basic_terrain");
    }
    if (shader == nullptr) {
        return;
    }

    auto& targets = *m_shared->deferredTargets;
    auto& terrain = *m_shared->terrain;
    auto& worldBuffer = *m_shared->worldRenderBuffer;

    targets.copySceneCompositeToTransparentComposite();
    targets.copyDepthToTransparentComposite();
    targets.bindTransparentComposite();

    TerrainFrameData tfd;
    tfd.view = ctx.camera.view;
    tfd.viewProj = m_currentSettings.taa.enabled ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
    tfd.cameraPos = ctx.camera.position;
    tfd.animationTime = ctx.animationTime;
    tfd.shaderTime = ctx.shaderTime;
    tfd.surfaceWetness = ctx.weather.surfaceWetness;
    tfd.fog.enabled = ctx.fog.enabled;
    tfd.fog.mode = ctx.fog.mode;
    tfd.fog.color = ctx.fog.color;
    tfd.fog.start = ctx.fog.startDistance;
    tfd.fog.end = ctx.fog.endDistance;
    tfd.fog.density = ctx.fog.density;
    tfd.skyLighting.cameraPos = ctx.camera.position;
    tfd.skyLighting.sunDirection = ctx.skyColors.sunDirection;
    tfd.skyLighting.moonDirection = ctx.skyColors.moonDirection;
    tfd.skyLighting.sunLightColor = ctx.skyColors.sunLightColor;
    tfd.skyLighting.moonLightColor = ctx.skyColors.moonLightColor;
    tfd.skyLighting.skyAmbientColor = ctx.skyColors.skyAmbientColor;
    tfd.skyLighting.shadowTintColor = ctx.skyColors.shadowTintColor;
    tfd.skyLighting.horizonScatterColor = ctx.skyColors.horizonScatterColor;
    tfd.skyLighting.skyIntensity = ctx.skyIntensity;
    tfd.skyLighting.moonVisibility = ctx.skyColors.moonVisibility;
    tfd.skyLighting.directIlluminance = ctx.skyIlluminance.directIlluminance;
    tfd.skyLighting.skyIlluminance = ctx.skyIlluminance.skyIlluminance;
    tfd.skyLighting.sunIlluminance = ctx.skyIlluminance.sunIlluminance;
    tfd.skyLighting.moonIlluminance = ctx.skyIlluminance.moonIlluminance;
    tfd.skyLighting.cloudDynamicWeather = ctx.skyIlluminance.cloudDynamicWeather;
    tfd.atmosphere.aerialStrength = ctx.atmosphere.aerialStrength;
    tfd.atmosphere.horizonScatterStrength = ctx.atmosphere.horizonScatterStrength;
    tfd.atmosphere.sunWarmth = ctx.atmosphere.sunWarmth;
    tfd.atmosphere.skyCoolness = ctx.atmosphere.skyCoolness;
    tfd.atmosphere.weatherWetness = ctx.weather.wetness;
    tfd.atmosphere.weatherStorm = ctx.weather.storm;
    tfd.atmosphere.aerialReduction = ctx.weather.aerialReduction;
    tfd.atmosphere.lightningFlash = ctx.weather.lightningFlash;
    tfd.atmosphere.surfaceWetness = ctx.weather.surfaceWetness;
    tfd.atmosphere.skyWetness = ctx.weather.skyWetness;
    tfd.atmosphere.fogWetness = ctx.weather.fogWetness;
    tfd.atmosphere.cloudWetness = ctx.weather.cloudWetness;
    tfd.atmosphere.precipitation = ctx.weather.precipitation;
    tfd.atmosphere.directWeatherOcclusion = ctx.atmosphere.directWeatherOcclusion;
    tfd.atmosphere.directWeatherOcclusionOverride = ctx.atmosphere.directWeatherOcclusionOverride;

    TerrainRenderSettings trs;
    trs.rainWetSurfacesEnabled = m_currentSettings.weather.wetSurfacesEnabled;
    trs.rainSurfaceRipplesEnabled = m_currentSettings.weather.surfaceRipplesEnabled;
    trs.aerialPerspectiveEnabled = m_currentSettings.postProcess.aerialPerspectiveEnabled;
    trs.volumetricLightEnabled = m_currentSettings.volumetric.lightEnabled;
    trs.volumetricFogEnabled = m_currentSettings.volumetric.fogEnabled;
    trs.volumetricFogStrength = m_currentSettings.volumetric.fogStrength;
    trs.directSunStrength = m_currentSettings.postProcess.directSunStrength;
    trs.skyAmbientStrength = m_currentSettings.postProcess.skyAmbientStrength;
    trs.weatherSkylightScale = m_currentSettings.weather.skylightScale;
    trs.minimumAmbient = m_currentSettings.postProcess.minimumAmbient;
    trs.blockLightStrength = m_currentSettings.postProcess.blockLightStrength;
    trs.fakeBounceStrength = m_currentSettings.postProcess.fakeBounceStrength;
    trs.albedoDesaturation = m_currentSettings.postProcess.albedoDesaturation;
    trs.shadowDesaturation = m_currentSettings.postProcess.shadowDesaturation;

    const TextureArray& texArray = m_resourceMgr->getTextureArray();
    const bool volFogShadersReady = m_volumetricPass && m_volumetricPass->hasShaders();
    terrain.bindChunkRenderState(tfd, texArray, *shader,
                                  true, 0, ctx.eyeInWater, m_heldBlockLightValue,
                                  targets, m_resourceMgr, volFogShadersReady, trs);
    shader->setInt("uCompositeInputsEnabled", 1);
    shader->setInt("uDepthSofteningEnabled", 1);
    shader->setInt("uForceBaseLod", 1);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, targets.depthTexture());
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, targets.sceneCompositeTexture());

    std::vector<const DrawBatchEntry*> genericEntries;
    genericEntries.reserve(m_transparentBatch.size());
    for (const DrawBatchEntry& entry : m_transparentBatch) {
        if (entry.kind == TransparentBatchKind::Generic) {
            genericEntries.push_back(&entry);
        }
    }
    std::sort(genericEntries.begin(), genericEntries.end(),
              [](const DrawBatchEntry* a, const DrawBatchEntry* b) {
                  return a->distanceSq > b->distanceSq;
              });

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    worldBuffer.beginFrame();
    for (const DrawBatchEntry* entry : genericEntries) {
        worldBuffer.addTransparent(entry->range);
    }
    worldBuffer.flushTransparent();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    shader->setInt("uForceBaseLod", 0);

    targets.copyTransparentCompositeToSceneComposite();
    targets.copyTransparentCompositeToSceneResolved();

    glBindVertexArray(0);
    for (int i = 14; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 0 ? GL_TEXTURE_2D_ARRAY : (i == 14 ? GL_TEXTURE_3D : GL_TEXTURE_2D), 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

void DeferredPipeline::updateDeferredHistoryTargets() {
    if (!m_shared || !m_shared->deferredTargets) return;
    auto& targets = *m_shared->deferredTargets;

    if (!targets.isReady() || m_deferredHistoryUpdatedThisFrame) return;

    targets.copySceneResolvedToHistory();
    targets.copyDepthToHistory();
    targets.copyReflectionToHistory();
    targets.copyCloudToHistory();
    if (!m_currentSettings.volumetric.temporalEnabled || !m_hasPreviousFrameData ||
        !(m_volumetricPass && m_volumetricPass->hasTemporalShader())) {
        targets.copyVolumetricToHistory();
    }
    targets.swapHistory();
    targets.swapSsaoHistory();
    m_deferredHistoryUpdatedThisFrame = true;
}

void DeferredPipeline::renderParticlesToSceneResolved(const FrameContext& ctx) {
    if (!m_currentSettings.weather.particlesEnabled || !m_shared || !m_shared->particleSystem || !m_resourceMgr) {
        return;
    }

    if (!m_shared->deferredTargets) return;
    auto& targets = *m_shared->deferredTargets;

    Shader* particleShader = m_resourceMgr->getShader("particle_gbuffer");
    if (!particleShader) return;

    targets.bindSceneComposite();

    const glm::mat4& viewProj = m_currentSettings.taa.enabled
        ? ctx.camera.jitteredViewProj
        : ctx.camera.viewProj;
    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shared->particleSystem->renderToSceneResolved(
        *particleShader,
        targets.voxelLightTexture(),
        targets.depthTexture(),
        ctx.camera.view, viewProj,
        screenSize);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void DeferredPipeline::renderWaterCompositePass(const FrameContext& ctx, bool preTemporalResolve) {
    if (!m_waterCompositePass || !m_shared || !m_shared->deferredTargets || !m_shared->worldRenderBuffer || !ctx.worldView) {
        return;
    }

    auto& targets = *m_shared->deferredTargets;
    const bool volumetricFogActive = !preTemporalResolve &&
                                     (m_currentSettings.volumetric.lightEnabled ||
                                      (m_currentSettings.volumetric.fogEnabled &&
                                       m_currentSettings.volumetric.fogStrength > 0.001f)) &&
                                     m_volumetricPass && m_volumetricPass->hasShaders();

    const bool useMultiDrawIndirect = m_shared->terrain != nullptr
        ? m_shared->terrain->useMultiDrawIndirect()
        : true;
    ScopedGpuTimer timer(ctx.debugService, GpuTimerPass::Water);
    const bool waterRenderedBeforeTemporal = m_waterCompositePass->execute(
        ctx, m_currentSettings, targets, *ctx.worldView,
        ctx.frameWidth, ctx.frameHeight,
        m_deferredFrameActive, preTemporalResolve,
        m_capturedFramebuffer, m_capturedViewport,
        m_currentSettings.transparent.compositeEnabled,
        m_currentSettings.transparent.waterEffectsEnabled,
        m_currentSettings.weather.surfaceRipplesEnabled,
        volumetricFogActive,
        useMultiDrawIndirect,
        *m_shared->worldRenderBuffer,
        m_transparentBatch,
        m_transparentPassPlan,
        m_transparentEntries);
    if (waterRenderedBeforeTemporal) {
        m_waterRenderedBeforeTemporal = true;
    }
}

FrameOutput DeferredPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output;

    if (m_shared && m_shared->deferredTargets) {
        output.sceneDepthTex = m_shared->deferredTargets->depthTexture();
        output.gbufferDepthTex = m_shared->deferredTargets->depthTexture();
        output.weatherMaskTex = m_shared->deferredTargets->weatherMaskTexture();
    }

    if (m_shared && m_shared->deferredTargets && m_shared->shadowRenderer) {
        auto& shadowData = output.heldItemShadow;
        const auto& cascades = m_shared->shadowRenderer->cascades();
        for (int i = 0; i < shadow::ShadowRenderer::CASCADE_COUNT; ++i) {
            shadowData.cascadeViewProj[i] = cascades[i].viewProj;
            shadowData.cascadeSplitFar[i] = cascades[i].splitFar;
            shadowData.cascadeTexelWorldSize[i] = cascades[i].texelWorldSize;
            shadowData.cascadeDepthExtent[i] = cascades[i].depthExtent;
        }
        shadowData.shadowTexture = m_shared->deferredTargets->csmShadowDepthComparisonTexture();
        shadowData.shadowDepthRaw = m_shared->deferredTargets->csmShadowDepthTexture();
        shadowData.shadowDepthAll = m_shared->deferredTargets->csmShadowDepthAllComparisonTexture();
        shadowData.shadowDepthAllRaw = m_shared->deferredTargets->csmShadowDepthAllTexture();
        shadowData.shadowColor0 = m_shared->deferredTargets->csmShadowColor0Texture();
        shadowData.shadowColor1 = m_shared->deferredTargets->csmShadowColor1Texture();
        shadowData.cameraPos = ctx.camera.position;
        shadowData.sunDirection = m_shared->shadowRenderer->lightDirection();
        shadowData.shadowDistance = m_currentSettings.shadow.distance;
        shadowData.constantBias = m_currentSettings.shadow.constantBias;
        shadowData.slopeBias = m_currentSettings.shadow.slopeBias;
        shadowData.normalOffset = m_currentSettings.shadow.normalOffset;
        shadowData.softness = m_currentSettings.shadow.softness;
        shadowData.pcssStrength = m_currentSettings.shadow.pcssStrength;
        shadowData.cascadeCount = shadow::ShadowRenderer::CASCADE_COUNT;
        shadowData.softShadowsEnabled = m_currentSettings.shadow.softShadowsEnabled ? 1 : 0;
        shadowData.pcssShadowsEnabled = m_currentSettings.shadow.pcssShadowsEnabled ? 1 : 0;
        shadowData.shadowsEnabled = m_currentSettings.shadow.enabled ? 1 : 0;
        shadowData.skyIntensity = ctx.skyIntensity;
    }

    output.hasDeferredInputs = m_deferredFrameActive;
    output.hasDebugView = (m_debugPass != nullptr);
    output.skipPostProcess = false;

    return output;
}
