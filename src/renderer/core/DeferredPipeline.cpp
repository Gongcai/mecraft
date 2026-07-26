#include "DeferredPipeline.h"
#include "RenderScene.h"
#include "FrameOutput.h"
#include "../debug/RenderDebugService.h"
#include "../../resource/ResourceMgr.h"
#include "../shadow/ShadowRenderer.h"
#include "../targets/DeferredRenderTargets.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../renderers/GameplaySkyRenderer.h"
#include "../mesh/TerrainRhiPipelineSet.h"
#include "../mesh/TerrainRenderer.h"
#include "../mesh/WorldRenderBuffer.h"
#include "../mesh/TerrainRenderCache.h"
#include "../../world/World.h"
#include "../../particle/ParticleSystem.h"
#include "../../Diagnostics.h"

#include <algorithm>
#include <iostream>

namespace {
void setClearAttachment(RhiColorAttachment& attachment,
                        const RhiTextureViewHandle view,
                        const float red,
                        const float green,
                        const float blue,
                        const float alpha) {
    attachment.view = view;
    attachment.loadOp = RhiLoadOp::Clear;
    attachment.storeOp = RhiStoreOp::Store;
    attachment.clearColor[0] = red;
    attachment.clearColor[1] = green;
    attachment.clearColor[2] = blue;
    attachment.clearColor[3] = alpha;
}

void clearColorAttachments(RhiCommandList& commandList,
                           const char* debugName,
                           const int width,
                           const int height,
                           const RhiColorAttachment* attachments,
                           const uint32_t attachmentCount) {
    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.colorAttachments = attachments;
    renderingInfo.colorAttachmentCount = attachmentCount;

    commandList.beginRendering(renderingInfo);
    commandList.endRendering();
}

void clearDepthAttachment(RhiCommandList& commandList,
                          const char* debugName,
                          const int width,
                          const int height,
                          const RhiTextureViewHandle view,
                          const float depth) {
    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = view;
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = depth;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, width)),
        static_cast<uint32_t>(std::max(1, height))
    };
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList.beginRendering(renderingInfo);
    commandList.endRendering();
}

RhiTextureViewHandle createTexture2DView(RhiDevice& rhiDevice,
                                         const RhiTextureHandle texture,
                                         const RhiTextureFormat format) {
    if (!texture.isValid()) {
        return {};
    }

    RhiTextureViewDesc desc;
    desc.texture = texture;
    desc.viewType = RhiTextureViewType::Texture2D;
    desc.format = format;
    desc.baseMip = 0;
    desc.mipCount = 1;
    desc.baseLayer = 0;
    desc.layerCount = 1;
    return rhiDevice.createTextureView(desc);
}

void destroyTextureViews(RhiDevice& rhiDevice, RhiTextureViewHandle* views, const int viewCount) {
    for (int i = 0; i < viewCount; ++i) {
        if (views[i].isValid()) {
            rhiDevice.destroyTextureView(views[i]);
            views[i] = {};
        }
    }
}

bool clearRebuiltHistoryTargets(RhiDevice& rhiDevice,
                                RhiCommandListPool& commandListPool,
                                DeferredRenderTargets& targets) {
    RhiTextureViewHandle views[8];
    int viewCount = 0;

    const auto createView = [&](const RhiTextureHandle texture, const RhiTextureFormat format) {
        RhiTextureViewHandle view = createTexture2DView(rhiDevice, texture, format);
        if (view.isValid()) {
            views[viewCount] = view;
            ++viewCount;
        }
        return view;
    };

    const RhiTextureViewHandle ssaoHistoryView = createView(targets.ssaoHistoryTextureHandle(),
                                                            RhiTextureFormat::R8Unorm);
    const RhiTextureViewHandle ssaoHistoryPrevView = createView(targets.ssaoHistoryTexturePrevHandle(),
                                                                RhiTextureFormat::R8Unorm);
    const RhiTextureViewHandle ssgiHistoryView = createView(targets.ssgiHistoryTextureHandle(),
                                                            RhiTextureFormat::Rgba16Float);
    const RhiTextureViewHandle ssgiMomentsHistoryView = createView(targets.ssgiMomentsHistoryTextureHandle(),
                                                                   RhiTextureFormat::Rgba16Float);
    const RhiTextureViewHandle ssgiHistoryPrevView = createView(targets.ssgiHistoryTexturePrevHandle(),
                                                                RhiTextureFormat::Rgba16Float);
    const RhiTextureViewHandle ssgiMomentsHistoryPrevView = createView(targets.ssgiMomentsHistoryTexturePrevHandle(),
                                                                       RhiTextureFormat::Rgba16Float);
    const RhiTextureViewHandle historyDepthView = createView(targets.historyDepthTextureHandle(),
                                                             RhiTextureFormat::Depth32Float);
    const RhiTextureViewHandle historyDepthPrevView = createView(targets.historyDepthTexturePrevHandle(),
                                                                 RhiTextureFormat::Depth32Float);

    if (!ssaoHistoryView.isValid() ||
        !ssaoHistoryPrevView.isValid() ||
        !ssgiHistoryView.isValid() ||
        !ssgiMomentsHistoryView.isValid() ||
        !ssgiHistoryPrevView.isValid() ||
        !ssgiMomentsHistoryPrevView.isValid() ||
        !historyDepthView.isValid() ||
        !historyDepthPrevView.isValid()) {
        destroyTextureViews(rhiDevice, views, viewCount);
        return false;
    }

    RhiCommandList* commandListStorage =
        commandListPool.acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"DeferredPipeline.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    targets.transitionTexture(commandList, targets.ssaoHistoryTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.ssaoHistoryTexturePrevHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.ssgiHistoryTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.ssgiMomentsHistoryTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.ssgiHistoryTexturePrevHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.ssgiMomentsHistoryTexturePrevHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.historyDepthTextureHandle(),
                              RhiResourceState::DepthWrite);
    targets.transitionTexture(commandList, targets.historyDepthTexturePrevHandle(),
                              RhiResourceState::DepthWrite);

    RhiColorAttachment ssaoAttachments[2];
    setClearAttachment(ssaoAttachments[0], ssaoHistoryView, 1.0f, 1.0f, 1.0f, 1.0f);
    setClearAttachment(ssaoAttachments[1], ssaoHistoryPrevView, 1.0f, 1.0f, 1.0f, 1.0f);
    clearColorAttachments(commandList, "DeferredHistorySsaoInit",
                          targets.width(), targets.height(), ssaoAttachments, 2u);

    RhiColorAttachment ssgiAttachments[4];
    setClearAttachment(ssgiAttachments[0], ssgiHistoryView, 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[1], ssgiMomentsHistoryView, 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[2], ssgiHistoryPrevView, 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[3], ssgiMomentsHistoryPrevView, 0.0f, 0.0f, 0.0f, 0.0f);
    clearColorAttachments(commandList, "DeferredHistorySsgiInit",
                          targets.width(), targets.height(), ssgiAttachments, 4u);

    clearDepthAttachment(commandList, "DeferredHistoryDepthInit",
                         targets.width(), targets.height(), historyDepthView, 1.0f);
    clearDepthAttachment(commandList, "DeferredHistoryDepthPrevInit",
                         targets.width(), targets.height(), historyDepthPrevView, 1.0f);

    targets.transitionTexture(commandList, targets.ssaoHistoryTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.ssaoHistoryTexturePrevHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.ssgiHistoryTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.ssgiMomentsHistoryTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.ssgiHistoryTexturePrevHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.ssgiMomentsHistoryTexturePrevHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.historyDepthTextureHandle(),
                              RhiResourceState::DepthRead);
    targets.transitionTexture(commandList, targets.historyDepthTexturePrevHandle(),
                              RhiResourceState::ShaderRead);

    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"DeferredPipeline.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
    destroyTextureViews(rhiDevice, views, viewCount);
    return true;
}
} // namespace

void DeferredPipeline::initializePasses(ResourceMgr& resourceMgr,
                                        shadow::ShadowRenderer* shadowRenderer) {
    m_resourceMgr = &resourceMgr;
    m_shadowRenderer = shadowRenderer;

    m_skyCapturePass = std::make_unique<SkyCapturePass>();
    m_gbufferPass = std::make_unique<GBufferPass>();
    m_shadowPass = std::make_unique<ShadowPass>();
    m_waterCompositePass = std::make_unique<WaterCompositePass>();
    m_velocityPass = std::make_unique<VelocityPass>();
    m_ssaoPass = std::make_unique<SsaoPass>();
    m_ssgiPass = std::make_unique<SsgiPass>();
    m_lightingPass = std::make_unique<DeferredLightingPass>();
    m_reflectionPass = std::make_unique<ReflectionPass>();
    m_cloudPass = std::make_unique<CloudPass>();
    m_sceneCompositePass = std::make_unique<SceneCompositePass>();
    m_volumetricPass = std::make_unique<VolumetricPass>();
    m_taaPass = std::make_unique<TemporalResolvePass>();
    m_motionBlurPass = std::make_unique<MotionBlurPass>();
    m_dofPass = std::make_unique<DepthOfFieldPass>();
    m_debugPass = std::make_unique<DebugPass>();
    m_voxelGiClipmap = std::make_unique<VoxelGiClipmap>();

    m_skyCapturePass->init(resourceMgr);
    m_gbufferPass->init(resourceMgr);
    m_shadowPass->init(resourceMgr);
    m_waterCompositePass->init(resourceMgr);
    m_velocityPass->init(resourceMgr);
    m_ssaoPass->init(resourceMgr);
    m_ssgiPass->init(resourceMgr);
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
        initializePasses(*m_resourceMgr, m_shadowRenderer);
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
    if (m_shared != nullptr && m_shared->rhiDevice != nullptr) {
        m_renderGraph.releaseTransientResources(*m_shared->rhiDevice);
    }
    if (m_voxelGiClipmap) {
        m_voxelGiClipmap->shutdown();
    }
    if (m_debugPass) m_debugPass->shutdown();
    if (m_dofPass) m_dofPass->shutdown();
    if (m_motionBlurPass) m_motionBlurPass->shutdown();
    if (m_taaPass) m_taaPass->shutdown();
    if (m_volumetricPass) m_volumetricPass->shutdown();
    if (m_sceneCompositePass) m_sceneCompositePass->shutdown();
    if (m_cloudPass) m_cloudPass->shutdown();
    if (m_reflectionPass) m_reflectionPass->shutdown();
    if (m_lightingPass) m_lightingPass->shutdown();
    if (m_ssgiPass) m_ssgiPass->shutdown();
    if (m_ssaoPass) m_ssaoPass->shutdown();
    if (m_velocityPass) m_velocityPass->shutdown();
    if (m_waterCompositePass) m_waterCompositePass->shutdown();
    if (m_shadowPass) m_shadowPass->shutdown();
    if (m_gbufferPass) m_gbufferPass->shutdown();
    if (m_skyCapturePass) m_skyCapturePass->shutdown();

    m_debugPass.reset();
    m_dofPass.reset();
    m_motionBlurPass.reset();
    m_taaPass.reset();
    m_volumetricPass.reset();
    m_sceneCompositePass.reset();
    m_cloudPass.reset();
    m_reflectionPass.reset();
    m_lightingPass.reset();
    m_ssgiPass.reset();
    m_ssaoPass.reset();
    m_velocityPass.reset();
    m_shadowPass.reset();
    m_waterCompositePass.reset();
    m_gbufferPass.reset();
    m_skyCapturePass.reset();
    m_voxelGiClipmap.reset();
    m_resourceMgr = nullptr;
    m_shadowRenderer = nullptr;
    m_shared = nullptr;
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
    RhiDevice& rhiDevice = *m_shared->rhiDevice;
    const int windowWidth = static_cast<int>(ctx.renderExtent.width);
    const int windowHeight = static_cast<int>(ctx.renderExtent.height);

    // Use settings from RenderScene
    m_currentSettings = settings;

    // Per-frame state reset
    m_deferredHistoryUpdatedThisFrame = false;
    m_waterRenderedBeforeTemporal = false;

    // Ensure deferred targets are sized correctly
    if (!targets.ensureSize(windowWidth, windowHeight, m_currentSettings.shadow.resolution)) {
        return {};
    }

    // After resize/rebuild, invalidate temporal history
    if (targets.consumeRebuiltFlag()) {
        if (!clearRebuiltHistoryTargets(
                rhiDevice, *m_shared->commandListPool, targets)) {
            return {};
        }
        m_hasPreviousFrameData = false;
        if (m_cloudPass) {
            m_cloudPass->invalidateHistory();
        }
        if (m_volumetricPass) {
            m_volumetricPass->invalidateHistory();
        }
    }

    m_deferredFrameActive = true;

    // Deferred geometry, shadows, SSAO, and lighting execute through one graph.
    if (!executeFrameGraph(ctx, m_currentSettings)) {
        return {};
    }

    // Debug early-out for deferred light debug mode
    if (m_currentSettings.debug.deferredLightDebugMode > 0) {
        targets.copySceneLightingToSceneComposite(rhiDevice);
        targets.copySceneCompositeToTransparentComposite(rhiDevice);
        targets.copySceneCompositeToSceneResolved(rhiDevice);
        updateDeferredHistoryTargets();
        targets.copySceneResolvedToTexture(rhiDevice, ctx.sceneCaptureColorTexture);
        targets.copyDepthToTexture(rhiDevice, ctx.sceneCaptureDepthTexture);
        return buildFrameOutput(ctx);
    }

    // Scene composite
    if (m_sceneCompositePass) {
        m_sceneCompositePass->execute(ctx, m_currentSettings, targets, m_voxelGiClipmap.get());
    }
    targets.copySceneCompositeToTransparentComposite(rhiDevice);
    targets.copySceneCompositeToSceneResolved(rhiDevice);

    // Reflection debug early-out
    if (m_currentSettings.debug.reflectionDebugMode > 0) {
        updateDeferredHistoryTargets();
        targets.copySceneResolvedToTexture(rhiDevice, ctx.sceneCaptureColorTexture);
        targets.copyDepthToTexture(rhiDevice, ctx.sceneCaptureDepthTexture);
        return buildFrameOutput(ctx);
    }

    // Water composite before the selected temporal reconstruction stage.
    if (usesTemporalProjectionJitter(
            m_currentSettings.upscale.type, m_currentSettings.taa.enabled)) {
        renderWaterCompositePass(ctx, true);
    }

    // Generic transparent terrain (glass, stained glass) before temporal resolve.
    renderGenericTransparentPass(ctx);

    // Particles
    renderParticlesToSceneResolved(ctx);
    targets.copySceneCompositeToSceneResolved(rhiDevice);

    // Volumetric fog
    if (m_volumetricPass) {
        m_volumetricPass->execute(
            ctx, m_currentSettings, targets, m_hasPreviousFrameData && !ctx.temporalReset);
    }

    // TAA resolve
    if (m_taaPass && usesNativeTaaResolve(
            m_currentSettings.upscale.type, m_currentSettings.taa.enabled) &&
        m_hasPreviousFrameData && !ctx.temporalReset) {
        m_taaPass->execute(ctx, m_currentSettings, targets);
    }

    // Motion blur
    if (m_motionBlurPass && m_currentSettings.postProcess.motionBlurEnabled &&
        m_hasPreviousFrameData && !ctx.temporalReset) {
        m_motionBlurPass->execute(ctx, m_currentSettings, targets);
    }

    // Depth of field
    if (m_dofPass && m_currentSettings.postProcess.dofEnabled) {
        m_dofPass->execute(ctx, m_currentSettings, targets);
    }

    // Final history update and blit
    updateDeferredHistoryTargets();
    targets.copySceneResolvedToTransparentComposite(rhiDevice);
    if (m_currentSettings.debug.viewMode > 0 && m_debugPass) {
        m_debugPass->execute(ctx, m_currentSettings, targets, windowWidth, windowHeight);
    } else {
        targets.copySceneResolvedToTexture(rhiDevice, ctx.sceneCaptureColorTexture);
    }
    targets.copyDepthToTexture(rhiDevice, ctx.sceneCaptureDepthTexture);

    // Match the legacy split path: if water was not rendered before temporal
    // resolve, composite it over the already-blitted final scene.
    if (!m_waterRenderedBeforeTemporal) {
        renderWaterCompositePass(ctx, false);
    }

    // Mark that we now have valid previous frame data for temporal effects
    m_hasPreviousFrameData = true;

    return buildFrameOutput(ctx);
}

bool DeferredPipeline::recordDeferredAuxiliaryClear(
    RhiCommandList& commandList,
    DeferredRenderTargets& targets) {
    RhiColorAttachment sceneAttachments[3];
    setClearAttachment(sceneAttachments[0], targets.reflectionTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    setClearAttachment(sceneAttachments[1], targets.sceneCompositeTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    setClearAttachment(sceneAttachments[2], targets.sceneResolvedTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    clearColorAttachments(commandList, "DeferredAuxiliarySceneClear",
                          targets.width(), targets.height(), sceneAttachments, 3u);

    RhiColorAttachment halfResAttachments[2];
    setClearAttachment(halfResAttachments[0], targets.cloudTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    setClearAttachment(halfResAttachments[1], targets.ssgiHalfResTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    clearColorAttachments(commandList, "DeferredAuxiliaryHalfResClear",
                          targets.halfWidth(), targets.halfHeight(), halfResAttachments, 2u);

    RhiColorAttachment ssgiAttachments[8];
    setClearAttachment(ssgiAttachments[0], targets.ssgiTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[1], targets.ssgiDenoiseTextureViewHandle(0), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[2], targets.ssgiDenoiseTextureViewHandle(1), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[3], targets.ssgiTemporalTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[4], targets.ssgiTemporalMomentsTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[5], targets.weatherMaskTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[6], targets.reactiveMaskTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(ssgiAttachments[7], targets.transparencyMaskTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    clearColorAttachments(commandList, "DeferredAuxiliarySsgiClear",
                          targets.width(), targets.height(), ssgiAttachments, 8u);

    return true;
}

bool DeferredPipeline::executeFrameGraph(const FrameContext& ctx,
                                         const RenderSettings& settings) {
    if (m_shared == nullptr || m_shared->rhiDevice == nullptr ||
        m_shared->commandListPool == nullptr ||
        m_shared->deferredTargets == nullptr || ctx.worldView == nullptr ||
        m_resourceMgr == nullptr || m_skyCapturePass == nullptr ||
        m_lightingPass == nullptr || !ctx.sceneCaptureColorTexture.isValid() ||
        m_shared->sky == nullptr || ctx.dayNightSystem == nullptr ||
        ctx.weatherSystem == nullptr) {
        return false;
    }

    RhiDevice& rhiDevice = *m_shared->rhiDevice;
    DeferredRenderTargets& targets = *m_shared->deferredTargets;
    const bool ssaoEnabled = settings.ssao.enabled;
    const bool ssaoTemporalEnabled =
        ssaoEnabled && settings.ssao.temporalEnabled && !ctx.temporalReset;
    const bool ssgiEnabled =
        settings.ssgi.enabled && settings.debug.deferredLightDebugMode <= 0;
    const bool ssgiTemporalEnabled =
        ssgiEnabled && settings.ssgi.temporalEnabled && !ctx.temporalReset;
    const bool reflectionFilterEnabled =
        settings.debug.deferredLightDebugMode <= 0 &&
        settings.reflection.filterEnabled &&
        settings.debug.reflectionDebugMode == 0;
    const bool reflectionTemporalEnabled =
        settings.debug.deferredLightDebugMode <= 0 &&
        settings.reflection.temporalEnabled &&
        settings.debug.reflectionDebugMode == 0 && !ctx.temporalReset;
    const bool cloudEnabled = settings.debug.deferredLightDebugMode <= 0;
    if (!targets.ensureGBufferTextureViews(rhiDevice) ||
        !targets.ensurePerObjectVelocityTextureView(rhiDevice) ||
        !targets.ensureVelocityTextureView(rhiDevice) ||
        !targets.ensureSceneLightingTextureView(rhiDevice) ||
        !targets.ensureReflectionTextureView(rhiDevice) ||
        !targets.ensureCloudTextureView(rhiDevice) ||
        !targets.ensureSceneCompositeTextureView(rhiDevice) ||
        !targets.ensureSceneResolvedTextureView(rhiDevice) ||
        !targets.ensureSsgiTextureView(rhiDevice) ||
        !targets.ensureSsgiHalfResTextureView(rhiDevice) ||
        !targets.ensureSsgiDenoiseTextureView(rhiDevice, 0) ||
        !targets.ensureSsgiDenoiseTextureView(rhiDevice, 1) ||
        !targets.ensureSsgiTemporalTextureViews(rhiDevice) ||
        !targets.ensureWeatherMaskTextureView(rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(rhiDevice) ||
        !targets.ensureTransparencyMaskTextureView(rhiDevice) ||
        !targets.ensureSkyCaptureTextureView(rhiDevice) ||
        !targets.ensureVolumetricFogTextureViews(rhiDevice) ||
        !targets.ensureSsaoFilteredTextureView(rhiDevice) ||
        (ssaoEnabled && !targets.ensureSsaoHalfResTextureView(rhiDevice)) ||
        (ssaoEnabled && settings.ssao.filterEnabled &&
         !targets.ensureSsaoHalfResFilteredTextureView(rhiDevice)) ||
        (ssaoTemporalEnabled &&
         (!targets.ensureSsaoTemporalTextureView(rhiDevice) ||
          !targets.ensureSsaoHistoryTextureViews(rhiDevice))) ||
        (ssgiTemporalEnabled &&
         (!targets.ensureSsgiHistoryTextureViews(rhiDevice) ||
          !targets.ensureHistoryDepthTextureViews(rhiDevice))) ||
        ((reflectionFilterEnabled || reflectionTemporalEnabled) &&
         !targets.ensureReflectionTemporalScratchTextureView(rhiDevice)) ||
        (reflectionTemporalEnabled &&
         !targets.ensureHistoryReflectionTextureViews(rhiDevice)) ||
        (cloudEnabled && !targets.ensureHistoryCloudTextureViews(rhiDevice))) {
        return false;
    }
    if (ssaoEnabled && m_ssaoPass == nullptr) {
        return false;
    }
    if (ssgiEnabled && m_ssgiPass == nullptr) {
        return false;
    }
    if (settings.debug.deferredLightDebugMode <= 0 &&
        (m_reflectionPass == nullptr || m_cloudPass == nullptr)) {
        return false;
    }
    const RhiTextureHandle skyNoiseTexture =
        m_resourceMgr->getTexture2DHandle("shader_noise2d");
    if (!skyNoiseTexture.isValid()) {
        return false;
    }

    const bool shadowEnabled = settings.shadow.enabled;
    if (shadowEnabled &&
        (m_shadowPass == nullptr || m_shared->shadowRenderer == nullptr ||
         !m_shadowPass->prepareGraphFrame(
             ctx, settings, targets, *ctx.worldView))) {
        return false;
    }
    bool cloudGraphPrepared = false;
    bool voxelGiGraphPrepared = false;
    const auto failGraphSetup = [&]() {
        if (voxelGiGraphPrepared) {
            m_voxelGiClipmap->finishGraphExecution(false);
            voxelGiGraphPrepared = false;
        }
        if (cloudGraphPrepared) {
            m_cloudPass->finishGraphExecution(false);
            cloudGraphPrepared = false;
        }
        if (shadowEnabled) {
            m_shadowPass->finishGraphExecution(false);
        }
        return false;
    };

    m_renderGraph.reset();
    const auto importTexture = [&](const RhiTextureHandle texture,
                                   const RhiTextureViewHandle view,
                                   const RhiResourceState stableState,
                                   RgTextureHandle& graphTexture) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) return false;
        RgImportedTextureDesc imported;
        imported.name = desc.debugName;
        imported.texture = texture;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        imported.defaultView = view;
        graphTexture = m_renderGraph.importTexture(imported);
        return graphTexture.isValid();
    };

    RgTextureHandle albedo;
    RgTextureHandle normalAo;
    RgTextureHandle voxelLight;
    RgTextureHandle material;
    RgTextureHandle materialAux;
    RgTextureHandle depth;
    RgTextureHandle perObjectVelocity;
    RgTextureHandle velocity;
    if (!importTexture(targets.albedoTextureHandle(),
                       targets.albedoTextureViewHandle(),
                       RhiResourceState::ShaderRead, albedo) ||
        !importTexture(targets.normalAoTextureHandle(),
                       targets.normalAoTextureViewHandle(),
                       RhiResourceState::ShaderRead, normalAo) ||
        !importTexture(targets.voxelLightTextureHandle(),
                       targets.voxelLightTextureViewHandle(),
                       RhiResourceState::ShaderRead, voxelLight) ||
        !importTexture(targets.materialTextureHandle(),
                       targets.materialTextureViewHandle(),
                       RhiResourceState::ShaderRead, material) ||
        !importTexture(targets.materialAuxTextureHandle(),
                       targets.materialAuxTextureViewHandle(),
                       RhiResourceState::ShaderRead, materialAux) ||
        !importTexture(targets.depthTextureHandle(),
                       targets.depthTextureViewHandle(),
                       RhiResourceState::DepthRead, depth) ||
        !importTexture(targets.perObjectVelocityTextureHandle(),
                       targets.perObjectVelocityTextureViewHandle(),
                       RhiResourceState::ShaderRead, perObjectVelocity) ||
        !importTexture(targets.velocityTextureHandle(),
                       targets.velocityTextureViewHandle(),
                       RhiResourceState::ShaderRead, velocity)) {
        return failGraphSetup();
    }

    ShadowPass::GraphResources shadowResources;
    if (!importTexture(targets.csmShadowDepthTextureHandle(),
                       targets.csmShadowDepthArrayTextureViewHandle(),
                       RhiResourceState::DepthRead,
                       shadowResources.depthOpaque) ||
        !importTexture(targets.csmShadowDepthAllTextureHandle(),
                       targets.csmShadowDepthAllArrayTextureViewHandle(),
                       RhiResourceState::DepthRead,
                       shadowResources.depthAll) ||
        !importTexture(targets.csmShadowColor0TextureHandle(),
                       targets.csmShadowColor0ArrayTextureViewHandle(),
                       RhiResourceState::ShaderRead,
                       shadowResources.color0) ||
        !importTexture(targets.csmShadowColor1TextureHandle(),
                       targets.csmShadowColor1ArrayTextureViewHandle(),
                       RhiResourceState::ShaderRead,
                       shadowResources.color1)) {
        return failGraphSetup();
    }

    SsaoPass::GraphResources ssaoResources;
    ssaoResources.depth = depth;
    ssaoResources.normalAo = normalAo;
    ssaoResources.velocity = velocity;
    if (!importTexture(targets.ssaoFilteredTextureHandle(),
                       targets.ssaoFilteredTextureViewHandle(),
                       RhiResourceState::ShaderRead,
                       ssaoResources.filtered) ||
        (ssaoEnabled &&
         (!importTexture(targets.ssaoHalfResTextureHandle(),
                         targets.ssaoHalfResTextureViewHandle(),
                         RhiResourceState::ShaderRead,
                         ssaoResources.halfRes) ||
         (settings.ssao.filterEnabled &&
          !importTexture(targets.ssaoHalfResFilteredTextureHandle(),
                         targets.ssaoHalfResFilteredTextureViewHandle(),
                         RhiResourceState::ShaderRead,
                         ssaoResources.halfResFiltered)) ||
         (ssaoTemporalEnabled &&
          (!importTexture(targets.ssaoTemporalTextureHandle(),
                          targets.ssaoTemporalTextureViewHandle(),
                          RhiResourceState::ShaderRead,
                          ssaoResources.temporal) ||
           !importTexture(targets.ssaoHistoryTextureHandle(), {},
                          RhiResourceState::ShaderRead,
                          ssaoResources.historyCurrent) ||
           !importTexture(targets.ssaoHistoryTexturePrevHandle(),
                          targets.ssaoHistoryTexturePrevViewHandle(),
                          RhiResourceState::ShaderRead,
                          ssaoResources.historyPrevious)))))) {
        return failGraphSetup();
    }

    RgTextureHandle reflection;
    RgTextureHandle sceneComposite;
    RgTextureHandle sceneResolved;
    RgTextureHandle cloud;
    RgTextureHandle ssgiHalfRes;
    RgTextureHandle ssgi;
    RgTextureHandle ssgiDenoise0;
    RgTextureHandle ssgiDenoise1;
    RgTextureHandle ssgiTemporal;
    RgTextureHandle ssgiTemporalMoments;
    RgTextureHandle weatherMask;
    RgTextureHandle reactiveMask;
    RgTextureHandle transparencyMask;
    RgTextureHandle skyCapture;
    RgTextureHandle atmosphereLut;
    RgTextureHandle skyNoise;
    RgTextureHandle sceneCaptureColor;
    RgTextureHandle sceneLighting;
    RgTextureHandle lightmapDay;
    RgTextureHandle lightmapNight;
    RgTextureHandle rippleNormal;
    RgTextureHandle ssgiHistoryCurrent;
    RgTextureHandle ssgiHistoryPrevious;
    RgTextureHandle ssgiMomentsHistoryCurrent;
    RgTextureHandle ssgiMomentsHistoryPrevious;
    RgTextureHandle historyDepthPrevious;
    RgTextureHandle reflectionScratch;
    RgTextureHandle historyReflectionPrevious;
    RgTextureHandle historyCloudPrevious;
    const RhiTextureHandle lightmapDayTexture = m_resourceMgr->getLightmapDay();
    const RhiTextureHandle lightmapNightTexture = m_resourceMgr->getLightmapNight();
    const RhiTextureHandle rippleNormalTexture =
        m_resourceMgr->getTexture2DHandle("shader_ripple_normal");
    if (!importTexture(targets.reflectionTextureHandle(),
                       targets.reflectionTextureViewHandle(),
                       RhiResourceState::ShaderRead, reflection) ||
        !importTexture(targets.sceneCompositeTextureHandle(),
                       targets.sceneCompositeTextureViewHandle(),
                       RhiResourceState::ShaderRead, sceneComposite) ||
        !importTexture(targets.sceneResolvedTextureHandle(),
                       targets.sceneResolvedTextureViewHandle(),
                       RhiResourceState::ShaderRead, sceneResolved) ||
        !importTexture(targets.cloudTextureHandle(),
                       targets.cloudTextureViewHandle(),
                       RhiResourceState::ShaderRead, cloud) ||
        !importTexture(targets.ssgiHalfResTextureHandle(),
                       targets.ssgiHalfResTextureViewHandle(),
                       RhiResourceState::ShaderRead, ssgiHalfRes) ||
        !importTexture(targets.ssgiTextureHandle(),
                       targets.ssgiTextureViewHandle(),
                       RhiResourceState::ShaderRead, ssgi) ||
        !importTexture(targets.ssgiDenoiseTextureHandle(0),
                       targets.ssgiDenoiseTextureViewHandle(0),
                       RhiResourceState::ShaderRead, ssgiDenoise0) ||
        !importTexture(targets.ssgiDenoiseTextureHandle(1),
                       targets.ssgiDenoiseTextureViewHandle(1),
                       RhiResourceState::ShaderRead, ssgiDenoise1) ||
        !importTexture(targets.ssgiTemporalTextureHandle(),
                       targets.ssgiTemporalTextureViewHandle(),
                       RhiResourceState::ShaderRead, ssgiTemporal) ||
        !importTexture(targets.ssgiTemporalMomentsTextureHandle(),
                       targets.ssgiTemporalMomentsTextureViewHandle(),
                       RhiResourceState::ShaderRead, ssgiTemporalMoments) ||
        !importTexture(targets.weatherMaskTextureHandle(),
                       targets.weatherMaskTextureViewHandle(),
                       RhiResourceState::ShaderRead, weatherMask) ||
        !importTexture(targets.reactiveMaskTextureHandle(),
                       targets.reactiveMaskTextureViewHandle(),
                       RhiResourceState::ShaderRead, reactiveMask) ||
        !importTexture(targets.transparencyMaskTextureHandle(),
                       targets.transparencyMaskTextureViewHandle(),
                       RhiResourceState::ShaderRead, transparencyMask) ||
        !importTexture(targets.skyCaptureTextureHandle(),
                       targets.skyCaptureTextureViewHandle(),
                       RhiResourceState::ShaderRead, skyCapture) ||
        !importTexture(targets.atmosphereLutTextureHandle(),
                       targets.atmosphereLutTextureViewHandle(),
                       RhiResourceState::ShaderRead, atmosphereLut) ||
        !importTexture(skyNoiseTexture, {}, RhiResourceState::ShaderRead,
                       skyNoise) ||
        !importTexture(ctx.sceneCaptureColorTexture, ctx.sceneCaptureColorView,
                       RhiResourceState::ShaderRead, sceneCaptureColor) ||
        !importTexture(targets.sceneLightingTextureHandle(),
                       targets.sceneLightingTextureViewHandle(),
                       RhiResourceState::ShaderRead, sceneLighting) ||
        !importTexture(lightmapDayTexture, {}, RhiResourceState::ShaderRead,
                       lightmapDay) ||
        !importTexture(lightmapNightTexture, {}, RhiResourceState::ShaderRead,
                       lightmapNight) ||
        !importTexture(rippleNormalTexture, {}, RhiResourceState::ShaderRead,
                       rippleNormal)) {
        return failGraphSetup();
    }
    if (ssgiTemporalEnabled &&
        (!importTexture(targets.ssgiHistoryTextureHandle(), {},
                        RhiResourceState::ShaderRead,
                        ssgiHistoryCurrent) ||
         !importTexture(targets.ssgiHistoryTexturePrevHandle(),
                        targets.ssgiHistoryTexturePrevViewHandle(),
                        RhiResourceState::ShaderRead,
                        ssgiHistoryPrevious) ||
         !importTexture(targets.ssgiMomentsHistoryTextureHandle(), {},
                        RhiResourceState::ShaderRead,
                        ssgiMomentsHistoryCurrent) ||
         !importTexture(targets.ssgiMomentsHistoryTexturePrevHandle(),
                        targets.ssgiMomentsHistoryTexturePrevViewHandle(),
                        RhiResourceState::ShaderRead,
                        ssgiMomentsHistoryPrevious) ||
         !importTexture(targets.historyDepthTexturePrevHandle(),
                        targets.historyDepthTexturePrevViewHandle(),
                        RhiResourceState::DepthRead,
                        historyDepthPrevious))) {
        return failGraphSetup();
    }
    if ((reflectionFilterEnabled || reflectionTemporalEnabled) &&
        !importTexture(targets.reflectionTemporalScratchTextureHandle(),
                       targets.reflectionTemporalScratchTextureViewHandle(),
                       RhiResourceState::ShaderRead,
                       reflectionScratch)) {
        return failGraphSetup();
    }
    if (reflectionTemporalEnabled &&
        !importTexture(targets.historyReflectionTexturePrevHandle(),
                       targets.historyReflectionTexturePrevViewHandle(),
                       RhiResourceState::ShaderRead,
                       historyReflectionPrevious)) {
        return failGraphSetup();
    }
    if (cloudEnabled &&
        !importTexture(targets.historyCloudTexturePrevHandle(),
                       targets.historyCloudTexturePrevViewHandle(),
                       RhiResourceState::ShaderRead,
                       historyCloudPrevious)) {
        return failGraphSetup();
    }
    ssaoResources.noise = skyNoise;

    RenderGraphPassBuilder auxiliaryClear = m_renderGraph.addPass(
        {"Deferred.AuxiliaryClear", RgPassType::Graphics,
         RhiQueueType::Graphics});
    auxiliaryClear.writeTexture(reflection, RhiResourceState::RenderTarget)
        .writeTexture(sceneComposite, RhiResourceState::RenderTarget)
        .writeTexture(sceneResolved, RhiResourceState::RenderTarget)
        .writeTexture(cloud, RhiResourceState::RenderTarget)
        .writeTexture(ssgiHalfRes, RhiResourceState::RenderTarget)
        .writeTexture(ssgi, RhiResourceState::RenderTarget)
        .writeTexture(ssgiDenoise0, RhiResourceState::RenderTarget)
        .writeTexture(ssgiDenoise1, RhiResourceState::RenderTarget)
        .writeTexture(ssgiTemporal, RhiResourceState::RenderTarget)
        .writeTexture(ssgiTemporalMoments, RhiResourceState::RenderTarget)
        .writeTexture(weatherMask, RhiResourceState::RenderTarget)
        .writeTexture(reactiveMask, RhiResourceState::RenderTarget)
        .writeTexture(transparencyMask, RhiResourceState::RenderTarget)
        .setExecute([&](RgPassContext& pass) {
            return recordDeferredAuxiliaryClear(pass.commandList(), targets);
        });
    RgPassHandle graphTail = auxiliaryClear.handle();

    RenderGraphPassBuilder skyCapturePass = m_renderGraph.addPass(
        {"Deferred.SkyCapture", RgPassType::Graphics, RhiQueueType::Graphics});
    skyCapturePass.dependsOn(graphTail)
        .readTexture(atmosphereLut, RhiResourceState::ShaderRead)
        .readTexture(skyNoise, RhiResourceState::ShaderRead)
        .writeTexture(skyCapture, RhiResourceState::RenderTarget)
        .setExecute([&](RgPassContext& pass) {
            return m_skyCapturePass->execute(
                pass.commandList(), *ctx.dayNightSystem, *ctx.weatherSystem,
                rhiDevice, targets, *m_shared->sky, *m_resourceMgr,
                ctx.camera.position.y, ctx.shaderTime, ctx.camera.position,
                settings.cloud.timeScale);
        });
    graphTail = skyCapturePass.handle();

    RenderGraphPassBuilder gbuffer = m_renderGraph.addPass(
        {"Deferred.GBuffer", RgPassType::Graphics, RhiQueueType::Graphics});
    gbuffer.dependsOn(graphTail)
        .writeTexture(albedo, RhiResourceState::RenderTarget)
        .writeTexture(normalAo, RhiResourceState::RenderTarget)
        .writeTexture(voxelLight, RhiResourceState::RenderTarget)
        .writeTexture(material, RhiResourceState::RenderTarget)
        .writeTexture(materialAux, RhiResourceState::RenderTarget)
        .writeTexture(depth, RhiResourceState::DepthWrite)
        .writeTexture(perObjectVelocity, RhiResourceState::RenderTarget)
        .setExecute([&](RgPassContext& pass) {
            RhiColorAttachment velocityClear;
            setClearAttachment(velocityClear,
                               targets.perObjectVelocityTextureViewHandle(),
                               0.0f, 0.0f, 0.0f, 0.0f);
            clearColorAttachments(pass.commandList(), "GBuffer.VelocityClear",
                                  targets.width(), targets.height(),
                                  &velocityClear, 1u);
            if (!renderGBufferTerrain(pass.commandList(), ctx, settings)) {
                return false;
            }
            if (m_gbufferPass == nullptr) return true;
            return m_gbufferPass->executeEntities(
                       pass.commandList(), *ctx.worldView, ctx, settings, targets,
                       m_shared->humanoidRenderer, m_shared->gameplayRegistry,
                       ctx.renderLocalPlayerModel) &&
                   m_gbufferPass->executeBlockEntities(
                       pass.commandList(), *ctx.worldView, ctx, settings, targets,
                       m_shared->blockEntityRenderer) &&
                   m_gbufferPass->executeDrops(
                       pass.commandList(), *ctx.worldView, ctx, settings, targets,
                       m_shared->dropRenderer, m_shared->dropSystem) &&
                   m_gbufferPass->executeFallingBlocks(
                       pass.commandList(), *ctx.worldView, ctx, settings, targets,
                       m_shared->fallingBlockRenderer,
                       m_shared->gameplayRegistry);
        });
    graphTail = gbuffer.handle();

    if (m_velocityPass != nullptr) {
        RenderGraphPassBuilder velocityPass = m_renderGraph.addPass(
            {"Deferred.Velocity", RgPassType::Graphics, RhiQueueType::Graphics});
        velocityPass.dependsOn(graphTail)
            .readTexture(depth, RhiResourceState::DepthRead)
            .readTexture(perObjectVelocity, RhiResourceState::ShaderRead)
            .writeTexture(velocity, RhiResourceState::RenderTarget)
            .setExecute([&](RgPassContext& pass) {
                return m_velocityPass->execute(pass.commandList(), ctx, settings,
                                               targets);
            });
        graphTail = velocityPass.handle();
    }

    if (shadowEnabled) {
        graphTail = m_shadowPass->addGraphPasses(
            m_renderGraph, shadowResources, graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }
    }

    if (ssaoEnabled) {
        graphTail = m_ssaoPass->addGraphPasses(
            m_renderGraph, ctx, settings.ssao, targets, ssaoResources,
            graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }
    }

    RenderGraphPassBuilder sceneLightingCopy = m_renderGraph.addPass(
        {"Deferred.SceneLightingCopy", RgPassType::Copy,
         RhiQueueType::Graphics});
    sceneLightingCopy.dependsOn(graphTail)
        .readTexture(sceneCaptureColor, RhiResourceState::TransferSrc)
        .writeTexture(sceneLighting, RhiResourceState::TransferDst)
        .setExecute([&](RgPassContext& pass) {
            RhiTextureBlit blit;
            blit.src = ctx.sceneCaptureColorTexture;
            blit.dst = targets.sceneLightingTextureHandle();
            pass.commandList().blitTexture(blit);
            return true;
        });
    graphTail = sceneLightingCopy.handle();

    const RgTextureHandle lightingSsao = ssaoTemporalEnabled
        ? ssaoResources.temporal
        : ssaoResources.filtered;
    m_lightingPass->setHeldBlockLightValue(m_heldBlockLightValue);
    RenderGraphPassBuilder lighting = m_renderGraph.addPass(
        {"Deferred.Lighting", RgPassType::Graphics, RhiQueueType::Graphics});
    lighting.dependsOn(graphTail)
        .readTexture(albedo, RhiResourceState::ShaderRead)
        .readTexture(normalAo, RhiResourceState::ShaderRead)
        .readTexture(voxelLight, RhiResourceState::ShaderRead)
        .readTexture(material, RhiResourceState::ShaderRead)
        .readTexture(materialAux, RhiResourceState::ShaderRead)
        .readTexture(depth, RhiResourceState::DepthRead)
        .readTexture(lightmapDay, RhiResourceState::ShaderRead)
        .readTexture(lightmapNight, RhiResourceState::ShaderRead)
        .readTexture(lightingSsao, RhiResourceState::ShaderRead)
        .readTexture(skyCapture, RhiResourceState::ShaderRead)
        .readTexture(skyNoise, RhiResourceState::ShaderRead)
        .readTexture(atmosphereLut, RhiResourceState::ShaderRead)
        .readTexture(shadowResources.depthOpaque, RhiResourceState::DepthRead)
        .readTexture(shadowResources.depthAll, RhiResourceState::DepthRead)
        .readTexture(shadowResources.color0, RhiResourceState::ShaderRead)
        .readTexture(shadowResources.color1, RhiResourceState::ShaderRead)
        .readTexture(rippleNormal, RhiResourceState::ShaderRead)
        .readWriteTexture(sceneLighting, RhiResourceState::RenderTarget)
        .setExecute([&](RgPassContext& pass) {
            return m_lightingPass->execute(
                pass.commandList(), ctx, settings, targets);
        });
    graphTail = lighting.handle();

    if (ssgiEnabled) {
        SsgiPass::GraphResources ssgiResources;
        ssgiResources.sceneLighting = sceneLighting;
        ssgiResources.albedo = albedo;
        ssgiResources.normalAo = normalAo;
        ssgiResources.materialAux = materialAux;
        ssgiResources.depth = depth;
        ssgiResources.noise = skyNoise;
        ssgiResources.halfRes = ssgiHalfRes;
        ssgiResources.output = ssgi;
        ssgiResources.denoise = {ssgiDenoise0, ssgiDenoise1};
        ssgiResources.velocity = velocity;
        ssgiResources.historyDepthPrevious = historyDepthPrevious;
        ssgiResources.historyPrevious = ssgiHistoryPrevious;
        ssgiResources.momentsHistoryPrevious = ssgiMomentsHistoryPrevious;
        ssgiResources.temporal = ssgiTemporal;
        ssgiResources.temporalMoments = ssgiTemporalMoments;
        ssgiResources.historyCurrent = ssgiHistoryCurrent;
        ssgiResources.momentsHistoryCurrent = ssgiMomentsHistoryCurrent;
        graphTail = m_ssgiPass->addGraphPasses(
            m_renderGraph, ctx, settings, targets, ssgiResources, graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }
    }

    if (settings.debug.deferredLightDebugMode <= 0) {
        ReflectionPass::GraphResources reflectionResources;
        reflectionResources.sceneLighting = sceneLighting;
        reflectionResources.depth = depth;
        reflectionResources.normalAo = normalAo;
        reflectionResources.material = material;
        reflectionResources.materialAux = materialAux;
        reflectionResources.skyCapture = skyCapture;
        reflectionResources.voxelLight = voxelLight;
        reflectionResources.reflection = reflection;
        reflectionResources.scratch = reflectionScratch;
        reflectionResources.historyPrevious = historyReflectionPrevious;
        reflectionResources.velocity = velocity;
        graphTail = m_reflectionPass->addGraphPasses(
            m_renderGraph, ctx, settings, targets, reflectionResources,
            graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }

        CloudPass::GraphResources cloudResources;
        cloudResources.depth = depth;
        cloudResources.skyCapture = skyCapture;
        cloudResources.noise = skyNoise;
        cloudResources.historyPrevious = historyCloudPrevious;
        cloudResources.cloud = cloud;
        graphTail = m_cloudPass->addGraphPass(
            m_renderGraph, ctx, settings, targets, cloudResources, graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }
        cloudGraphPrepared = true;
    }

    if (settings.debug.deferredLightDebugMode <= 0 && m_voxelGiClipmap) {
        graphTail = m_voxelGiClipmap->addGraphPasses(
            m_renderGraph, ctx, settings.voxelGi, *m_resourceMgr, rhiDevice,
            graphTail);
        if (!graphTail.isValid()) {
            return failGraphSetup();
        }
        voxelGiGraphPrepared = m_voxelGiClipmap->graphFramePrepared();
    }

    const RgCompileResult compiled = m_renderGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[DeferredPipeline] Render Graph compile failed: "
                      << compiled.message << '\n');
        return failGraphSetup();
    }
    const GpuTimerCheckpoint timerCheckpoint = ctx.debugService != nullptr
        ? ctx.debugService->gpuTimerCheckpoint()
        : GpuTimerCheckpoint{};
    if (shadowEnabled) {
        m_shadowPass->beginGraphExecution();
    }
    const RgExecuteResult executed =
        m_renderGraph.execute(rhiDevice, *m_shared->commandListPool);
    if (!executed.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[DeferredPipeline] Render Graph execution failed: "
                      << executed.message << '\n');
    }
    if (!executed.succeeded() && ctx.debugService != nullptr) {
        ctx.debugService->cancelGpuTimersSince(timerCheckpoint);
    }
    if (shadowEnabled) {
        m_shadowPass->finishGraphExecution(executed.succeeded());
    }
    if (cloudGraphPrepared) {
        m_cloudPass->finishGraphExecution(executed.succeeded());
    }
    if (voxelGiGraphPrepared) {
        m_voxelGiClipmap->finishGraphExecution(executed.succeeded());
    }
    return executed.succeeded();
}

bool DeferredPipeline::renderGBufferTerrain(RhiCommandList& commandList,
                                            const FrameContext& ctx,
                                            const RenderSettings& settings) {
    if (!m_shared || !m_shared->deferredTargets || !m_shared->terrain ||
        !m_shared->worldRenderBuffer || !m_shared->resources || !m_shared->rhiDevice) {
        return false;
    }

    auto& targets = *m_shared->deferredTargets;
    auto& terrain = *m_shared->terrain;
    auto& worldBuffer = *m_shared->worldRenderBuffer;
    RhiDevice& rhiDevice = *m_shared->rhiDevice;

    if (!targets.ensureGBufferTextureViews(rhiDevice)) {
        return false;
    }

    RhiColorAttachment gbufferAttachments[5];
    setClearAttachment(gbufferAttachments[0], targets.albedoTextureViewHandle(), 0.0f, 0.0f, 0.0f, 0.0f);
    setClearAttachment(gbufferAttachments[1], targets.normalAoTextureViewHandle(), 0.5f, 0.5f, 1.0f, 1.0f);
    setClearAttachment(gbufferAttachments[2], targets.voxelLightTextureViewHandle(), 0.0f, 0.0f, 0.0f, 1.0f);
    setClearAttachment(gbufferAttachments[3], targets.materialTextureViewHandle(), 0.86f, 0.035f, 0.0f, 0.0f);
    setClearAttachment(gbufferAttachments[4], targets.materialAuxTextureViewHandle(), 0.0f, 0.0f, 0.65f, 0.0f);

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.depthTextureViewHandle();
    depthAttachment.depthLoadOp = RhiLoadOp::Clear;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "GBufferInitialClear";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = gbufferAttachments;
    renderingInfo.colorAttachmentCount = 5u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::GBuffer)
        : GpuTimerSegmentToken{};
    if (m_shared->terrainCache) {
        m_shared->terrainCache->releaseStaleMdiAllocations(*ctx.worldView);
        m_shared->terrainCache->drainMeshingResults(*ctx.worldView, commandList);
    }
    worldBuffer.beginFrame();
    terrain.clearTransparentBatches();

    // Build terrain frame data from FrameContext
    TerrainFrameData tfd;
    tfd.view = ctx.camera.view;
    tfd.viewProj = usesTemporalProjectionJitter(
        settings.upscale.type, settings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
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
    tfd.skyLighting.moonPhaseFlux =
        (std::abs(ctx.skyColors.moonPhaseAngle) / glm::pi<float>() + 0.2f) * 0.0005f;
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
    trs.blockMaterialMapsEnabled = settings.blockMaterialMaps.enabled;
    trs.blockNormalMapsEnabled = settings.blockMaterialMaps.normalMapsEnabled;
    trs.blockSpecularMapsEnabled = settings.blockMaterialMaps.specularMapsEnabled;
    trs.blockParallaxMapsEnabled = settings.blockMaterialMaps.parallaxMapsEnabled;
    trs.blockParallaxDepth = settings.blockMaterialMaps.parallaxDepth;

    if (m_shared->terrainRhiPipelines == nullptr ||
        !m_shared->terrainRhiPipelines->prepareGBuffer(
            commandList,
            *m_shared->resources,
            tfd,
            trs)) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        return false;
    }

    if (m_shared->terrainCache) {
        m_shared->terrainCache->submitMeshingJobs(*ctx.worldView, ctx.camera.position);
    }

    terrain.renderOpaqueChunksAndCollectPasses(*ctx.worldView, true);
    terrain.syncTransparentBatches();
    m_transparentBatch = terrain.transparentBatches();
    m_transparentPassPlan = terrain.transparentPassPlan();
    if (!worldBuffer.prepareRhiOpaqueAndCutout(
            commandList,
            m_shared->terrainRhiPipelines->metadataLayout())) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        return false;
    }

    commandList.beginRendering(renderingInfo);
    commandList.setViewport({
        0.0f,
        0.0f,
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())),
        0.0f,
        1.0f
    });
    commandList.setScissor(renderingInfo.renderArea);

    worldBuffer.recordRhiOpaque(
        commandList,
        m_shared->terrainRhiPipelines->gbufferOpaquePipeline(),
        m_shared->terrainRhiPipelines->gbufferOpaqueBindGroup());
    worldBuffer.recordRhiCutout(
        commandList,
        m_shared->terrainRhiPipelines->gbufferCutoutPipeline(),
        m_shared->terrainRhiPipelines->gbufferCutoutBindGroup());
    worldBuffer.captureSceneFrameStats();

    commandList.endRendering();
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    return true;
}

void DeferredPipeline::renderGenericTransparentPass(const FrameContext& ctx) {
    if (!m_shared || !m_shared->deferredTargets || !m_shared->worldRenderBuffer ||
        !m_shared->terrainRhiPipelines || !m_resourceMgr || !m_transparentPassPlan.hasGeneric()) {
        return;
    }

    auto& targets = *m_shared->deferredTargets;
    auto& worldBuffer = *m_shared->worldRenderBuffer;
    RhiDevice& rhiDevice = *m_shared->rhiDevice;

    if (!targets.ensureTransparentCompositeTextureViews(rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(rhiDevice) ||
        !targets.ensureTransparencyMaskTextureView(rhiDevice)) {
        return;
    }

    {
        RhiCommandList* copyCommandListStorage = m_shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (copyCommandListStorage == nullptr ||
        !copyCommandListStorage->begin({"DeferredPipeline.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& copyCommandList = *copyCommandListStorage;
        const GpuTimerSegmentToken copyTimer = ctx.debugService != nullptr
            ? ctx.debugService->beginGpuTimer(
                  copyCommandList, GpuTimerPass::Transparent)
            : GpuTimerSegmentToken{};
        targets.copySceneCompositeToTransparentComposite(copyCommandList);
        targets.copyDepthToTransparentComposite(copyCommandList);
        if (ctx.debugService != nullptr) {
            ctx.debugService->endGpuTimer(copyCommandList, copyTimer);
        }
        if (!copyCommandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&copyCommandList};
        if (!rhiDevice.submit({"DeferredPipeline.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
    }

    RhiColorAttachment colorAttachments[3];
    colorAttachments[0].view = targets.transparentCompositeTextureViewHandle();
    colorAttachments[0].loadOp = RhiLoadOp::Load;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
    colorAttachments[1].loadOp = RhiLoadOp::Load;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
    colorAttachments[2].loadOp = RhiLoadOp::Load;
    colorAttachments[2].storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = targets.transparentCompositeDepthTextureViewHandle();
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "GenericTransparent.TransparentComposite";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList* commandListStorage = m_shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"DeferredPipeline.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Transparent)
        : GpuTimerSegmentToken{};

    TerrainFrameData tfd;
    tfd.view = ctx.camera.view;
    tfd.viewProj = usesTemporalProjectionJitter(
        m_currentSettings.upscale.type, m_currentSettings.taa.enabled)
        ? ctx.camera.jitteredViewProj : ctx.camera.viewProj;
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
    tfd.skyLighting.moonPhaseFlux =
        (std::abs(ctx.skyColors.moonPhaseAngle) / glm::pi<float>() + 0.2f) * 0.0005f;
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
    trs.blockMaterialMapsEnabled = m_currentSettings.blockMaterialMaps.enabled;
    trs.blockNormalMapsEnabled = m_currentSettings.blockMaterialMaps.normalMapsEnabled;
    trs.blockSpecularMapsEnabled = m_currentSettings.blockMaterialMaps.specularMapsEnabled;
    trs.blockParallaxMapsEnabled = m_currentSettings.blockMaterialMaps.parallaxMapsEnabled;
    trs.blockParallaxDepth = m_currentSettings.blockMaterialMaps.parallaxDepth;

    const bool volFogShadersReady = m_volumetricPass && m_volumetricPass->hasShaders();

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

    worldBuffer.beginFrame();
    for (const DrawBatchEntry* entry : genericEntries) {
        worldBuffer.addTransparent(entry->range);
    }

    if (!m_shared->terrainRhiPipelines->prepareTransparent(
            commandList,
            *m_resourceMgr,
            targets,
            tfd,
            trs,
            m_heldBlockLightValue,
            volFogShadersReady) ||
        !worldBuffer.prepareRhiTransparent(
            commandList,
            m_shared->terrainRhiPipelines->transparentMetadataLayout())) {
        if (ctx.debugService != nullptr) {
            ctx.debugService->cancelGpuTimer(gpuTimer);
        }
        return;
    }

    targets.transitionTexture(commandList, targets.reactiveMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.transparencyMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(renderingInfo);
    worldBuffer.recordRhiTransparent(
        commandList,
        m_shared->terrainRhiPipelines->transparentPipeline(),
        m_shared->terrainRhiPipelines->transparentBindGroup());

    commandList.endRendering();
    targets.transitionTexture(commandList, targets.reactiveMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.transparencyMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"DeferredPipeline.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }

    RhiCommandList* copyCommandListStorage = m_shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (copyCommandListStorage == nullptr ||
        !copyCommandListStorage->begin({"DeferredPipeline.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& copyCommandList = *copyCommandListStorage;
    const GpuTimerSegmentToken copyTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(copyCommandList, GpuTimerPass::Transparent)
        : GpuTimerSegmentToken{};
    targets.copyTransparentCompositeToSceneComposite(copyCommandList);
    targets.copyTransparentCompositeToSceneResolved(copyCommandList);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(copyCommandList, copyTimer);
    }
    if (!copyCommandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&copyCommandList};
        if (!rhiDevice.submit({"DeferredPipeline.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }

}

void DeferredPipeline::updateDeferredHistoryTargets() {
    if (!m_shared || !m_shared->deferredTargets || !m_shared->rhiDevice) return;
    auto& targets = *m_shared->deferredTargets;
    RhiDevice& rhiDevice = *m_shared->rhiDevice;

    if (!targets.isReady() || m_deferredHistoryUpdatedThisFrame) return;

    targets.copySceneResolvedToHistory(rhiDevice);
    targets.copyDepthToHistory(rhiDevice);
    targets.copyReflectionToHistory(rhiDevice);
    targets.copyCloudToHistory(rhiDevice);
    if (!m_currentSettings.volumetric.temporalEnabled || !m_hasPreviousFrameData ||
        !(m_volumetricPass && m_volumetricPass->hasTemporalShader())) {
        targets.copyVolumetricToHistory(rhiDevice);
    }
    targets.swapHistory();
    targets.swapSsaoHistory();
    targets.swapSsgiHistory();
    m_deferredHistoryUpdatedThisFrame = true;
}

void DeferredPipeline::renderParticlesToSceneResolved(const FrameContext& ctx) {
    if (!m_currentSettings.weather.particlesEnabled || !m_shared || !m_shared->particleSystem || !m_resourceMgr) {
        return;
    }

    if (!m_shared->deferredTargets) return;
    auto& targets = *m_shared->deferredTargets;

    RhiDevice& rhiDevice = *m_shared->rhiDevice;
    if (!targets.ensureSceneCompositeTextureView(rhiDevice) ||
        !targets.ensureReactiveMaskTextureView(rhiDevice) ||
        !targets.ensureTransparencyMaskTextureView(rhiDevice)) {
        return;
    }

    RhiColorAttachment colorAttachments[3];
    colorAttachments[0].view = targets.sceneCompositeTextureViewHandle();
    colorAttachments[0].loadOp = RhiLoadOp::Load;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[1].view = targets.reactiveMaskTextureViewHandle();
    colorAttachments[1].loadOp = RhiLoadOp::Load;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[2].view = targets.transparencyMaskTextureViewHandle();
    colorAttachments[2].loadOp = RhiLoadOp::Load;
    colorAttachments[2].storeOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "Particles.SceneComposite";
    renderingInfo.renderArea = {
        0,
        0,
        static_cast<uint32_t>(std::max(1, targets.width())),
        static_cast<uint32_t>(std::max(1, targets.height()))
    };
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;

    RhiCommandList* commandListStorage = m_shared->commandListPool->acquire(RhiCommandListType::Graphics);
    if (commandListStorage == nullptr ||
        !commandListStorage->begin({"DeferredPipeline.Commands", RhiCommandListType::Graphics})) {
        std::abort();
    }
    RhiCommandList& commandList = *commandListStorage;
    const GpuTimerSegmentToken gpuTimer = ctx.debugService != nullptr
        ? ctx.debugService->beginGpuTimer(commandList, GpuTimerPass::Transparent)
        : GpuTimerSegmentToken{};
    m_shared->particleSystem->prepareFrame(ctx.camera.view, commandList);
    targets.transitionTexture(commandList, targets.sceneCompositeTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.reactiveMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    targets.transitionTexture(commandList, targets.transparencyMaskTextureHandle(),
                              RhiResourceState::RenderTarget);
    commandList.beginRendering(renderingInfo);

    const glm::mat4& viewProj = usesTemporalProjectionJitter(
        m_currentSettings.upscale.type, m_currentSettings.taa.enabled)
        ? ctx.camera.jitteredViewProj
        : ctx.camera.viewProj;
    const glm::vec2 screenSize(
        static_cast<float>(std::max(1, targets.width())),
        static_cast<float>(std::max(1, targets.height())));

    m_shared->particleSystem->renderToSceneResolved(
        commandList,
        targets.voxelLightTextureHandle(),
        targets.depthTextureHandle(),
        viewProj,
        screenSize);

    commandList.endRendering();
    targets.transitionTexture(commandList, targets.sceneCompositeTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.reactiveMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    targets.transitionTexture(commandList, targets.transparencyMaskTextureHandle(),
                              RhiResourceState::ShaderRead);
    if (ctx.debugService != nullptr) {
        ctx.debugService->endGpuTimer(commandList, gpuTimer);
    }
    if (!commandList.end()) {
        std::abort();
    }
    {
        RhiCommandList* submittedCommandLists[] = {&commandList};
        if (!rhiDevice.submit({"DeferredPipeline.Submit", submittedCommandLists, 1u})) {
            std::abort();
        }
    }
}

void DeferredPipeline::renderWaterCompositePass(const FrameContext& ctx, bool preTemporalResolve) {
    if (!m_waterCompositePass || !m_shared || !m_shared->deferredTargets ||
        !m_shared->worldRenderBuffer) {
        return;
    }

    auto& targets = *m_shared->deferredTargets;
    const bool volumetricFogActive = !preTemporalResolve &&
                                     (m_currentSettings.volumetric.lightEnabled ||
                                      (m_currentSettings.volumetric.fogEnabled &&
                                       m_currentSettings.volumetric.fogStrength > 0.001f)) &&
                                     m_volumetricPass && m_volumetricPass->hasShaders();

    const bool waterRenderedBeforeTemporal = m_waterCompositePass->execute(
        ctx, m_currentSettings, targets,
        m_deferredFrameActive, preTemporalResolve,
        m_currentSettings.transparent.compositeEnabled,
        m_currentSettings.transparent.waterEffectsEnabled,
        m_currentSettings.weather.surfaceRipplesEnabled,
        volumetricFogActive,
        *m_shared->worldRenderBuffer,
        m_transparentBatch,
        m_transparentPassPlan);
    if (waterRenderedBeforeTemporal) {
        m_waterRenderedBeforeTemporal = true;
    }
}

FrameOutput DeferredPipeline::buildFrameOutput(const FrameContext& ctx) {
    FrameOutput output;

    if (m_shared && m_shared->deferredTargets) {
        output.sceneColor = m_shared->deferredTargets->sceneResolvedTextureHandle();
        output.sceneDepth = m_shared->deferredTargets->depthTextureHandle();
        output.gbufferDepth = m_shared->deferredTargets->depthTextureHandle();
        output.weatherMask = m_shared->deferredTargets->weatherMaskTextureHandle();
        output.reactiveMask = m_shared->deferredTargets->reactiveMaskTextureHandle();
        output.transparencyMask = m_shared->deferredTargets->transparencyMaskTextureHandle();
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
        shadowData.shadowTextureHandle = m_shared->deferredTargets->csmShadowDepthComparisonTextureHandle();
        shadowData.shadowDepthRawHandle = m_shared->deferredTargets->csmShadowDepthTextureHandle();
        shadowData.shadowDepthAllHandle = m_shared->deferredTargets->csmShadowDepthAllComparisonTextureHandle();
        shadowData.shadowDepthAllRawHandle = m_shared->deferredTargets->csmShadowDepthAllTextureHandle();
        shadowData.shadowColor0Handle = m_shared->deferredTargets->csmShadowColor0TextureHandle();
        shadowData.shadowColor1Handle = m_shared->deferredTargets->csmShadowColor1TextureHandle();
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
