#include "DeferredPipeline.h"
#include "RenderScene.h"
#include "FrameOutput.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/GameplaySkyRenderer.h"

#include <glad/glad.h>

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
    }

    // Clear auxiliary targets
    clearDeferredAuxiliaryTargets();

    // Sky capture
    if (m_skyCapturePass && m_shared->sky) {
        m_skyCapturePass->execute(*ctx.world, targets, *m_shared->sky,
                                  m_resourceMgr, ctx.camera.position.y,
                                  ctx.shaderTime, ctx.camera.position,
                                  m_currentSettings.cloud.timeScale);
    }

    m_deferredFrameActive = true;

    // GBuffer terrain (delegated to TerrainRenderer)
    // Note: This is still handled by Renderer in the current phase.
    // In Phase 10f, this will be fully migrated.

    // Entity and drop GBuffer
    if (m_gbufferPass) {
        m_gbufferPass->executeEntities(*ctx.world, ctx, targets,
                                       m_shared->humanoidRenderer, nullptr, true);
        m_gbufferPass->executeDrops(*ctx.world, ctx, targets,
                                    m_shared->dropRenderer, m_shared->dropSystem);
    }

    // Velocity pass
    if (m_velocityPass) {
        m_velocityPass->execute(ctx, m_currentSettings, targets);
    }

    // Shadow pass
    if (m_shadowPass && m_currentSettings.shadow.enabled) {
        // Shadow rendering is handled by ShadowPass
        // m_shadowPass->execute(...);
    }

    // SSAO pass
    if (m_ssaoPass) {
        m_ssaoPass->execute(ctx, m_currentSettings, targets);
    }

    // Copy forward alpha to scene lighting
    const int capturedWidth = m_capturedViewport[2] > 0 ? m_capturedViewport[2] : windowWidth;
    const int capturedHeight = m_capturedViewport[3] > 0 ? m_capturedViewport[3] : windowHeight;
    targets.copyFramebufferColorToSceneLighting(m_capturedFramebuffer, capturedWidth, capturedHeight);

    // Deferred lighting
    if (m_lightingPass) {
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
        m_reflectionPass->execute(ctx, m_currentSettings, targets);
    }

    // Cloud pass
    if (m_cloudPass) {
        m_cloudPass->execute(ctx, m_currentSettings, targets);
    }

    // Scene composite
    if (m_sceneCompositePass) {
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
        renderWaterCompositePass(ctx, true);
    }

    // Particles
    renderParticlesToSceneResolved(ctx);
    targets.copySceneCompositeToSceneResolved();

    // Volumetric fog
    if (m_volumetricPass) {
        m_volumetricPass->execute(ctx, m_currentSettings, targets, m_hasPreviousFrameData);
    }

    // TAA resolve
    if (m_taaPass && m_currentSettings.taa.enabled && m_hasPreviousFrameData) {
        m_taaPass->execute(ctx, m_currentSettings, targets);
    }

    // Motion blur
    if (m_motionBlurPass && m_currentSettings.postProcess.motionBlurEnabled && m_hasPreviousFrameData) {
        m_motionBlurPass->execute(ctx, m_currentSettings, targets);
    }

    // Depth of field
    if (m_dofPass && m_currentSettings.postProcess.dofEnabled) {
        m_dofPass->execute(ctx, m_currentSettings, targets);
    }

    // Final history update and blit
    updateDeferredHistoryTargets();
    targets.copySceneResolvedToTransparentComposite();
    targets.blitSceneResolvedTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
    targets.blitDepthTo(m_capturedFramebuffer, capturedWidth, capturedHeight);
    restoreCapturedFramebufferViewport(windowWidth, windowHeight);

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

void DeferredPipeline::renderParticlesToSceneResolved(const FrameContext& /*ctx*/) {
    if (!m_currentSettings.weather.particlesEnabled || !m_shared || !m_shared->particleSystem) {
        return;
    }

    if (!m_shared->deferredTargets) return;
    auto& targets = *m_shared->deferredTargets;

    targets.bindSceneComposite();

    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Particle rendering will be implemented when ParticleSystem is integrated
    // m_shared->particleSystem->renderToSceneResolved(...);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void DeferredPipeline::renderWaterCompositePass(const FrameContext& /*ctx*/, bool /*preTemporalResolve*/) {
    // Water composite requires many parameters from Renderer state.
    // This will be fully implemented in Phase 10f when Renderer is slimmed down.
    // For now, water rendering is handled by the legacy Renderer path.
}

FrameOutput DeferredPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output;

    if (m_shared && m_shared->deferredTargets) {
        output.sceneDepthTex = m_shared->deferredTargets->depthTexture();
        output.gbufferDepthTex = m_shared->deferredTargets->depthTexture();
        output.weatherMaskTex = m_shared->deferredTargets->weatherMaskTexture();
    }

    output.hasDeferredInputs = m_deferredFrameActive;
    output.hasDebugView = (m_debugPass != nullptr);
    output.skipPostProcess = false;

    return output;
}
