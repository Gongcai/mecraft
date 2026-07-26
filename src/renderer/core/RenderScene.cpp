#include "RenderScene.h"
#include "../../Diagnostics.h"
#include "RenderResourceHub.h"
#include "SettingsMapper.h"
#include "ForwardPipeline.h"
#include "DeferredPipeline.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/FirstPersonHeldItemRenderer.h"
#include "engine/camera/Camera.h"
#include "../mesh/TerrainStreamingService.h"

#if defined(MECRAFT_ENABLE_FSR31)
#include "renderer/upscaling/Fsr31TemporalConfig.h"
#endif
#if defined(MECRAFT_ENABLE_STREAMLINE)
#include "renderer/upscaling/DlssVulkanContext.h"
#include "renderer/upscaling/StreamlineRuntime.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>
#include "engine/platform/Window.h"
#include "../../particle/RainRenderer.h"
#include "../../world/World.h"
#include "../../world/IWorldView.h"
#include "../../world/DayNightSystem.h"
#include "../../world/WeatherSystem.h"
#include "../../world/block/Block.h"
#include "../../world/WeatherSystem.h"
#include "engine/platform/Time.h"
#include "../renderers/GameplaySkyRenderer.h"

namespace {
/// Convert GameplaySkyRenderer::SkyColors to SkyColorsData
SkyColorsData toSkyColorsData(const GameplaySkyRenderer::SkyColors& src) {
    SkyColorsData dst;
    dst.top = src.top;
    dst.horizon = src.horizon;
    dst.fog = src.fog;
    dst.halo = src.halo;
    dst.sunDirection = src.sunDirection;
    dst.sunScatter = src.sunScatter;
    dst.sunLightColor = src.sunLightColor;
    dst.skyAmbientColor = src.skyAmbientColor;
    dst.shadowTintColor = src.shadowTintColor;
    dst.horizonScatterColor = src.horizonScatterColor;
    dst.cloudColor = src.cloudColor;
    dst.moonDirection = src.moonDirection;
    dst.moonLightColor = src.moonLightColor;
    dst.haloStrength = src.haloStrength;
    dst.horizonHaze = src.horizonHaze;
    dst.sunGlare = src.sunGlare;
    dst.sunVisibility = src.sunVisibility;
    dst.moonVisibility = src.moonVisibility;
    dst.moonPhaseAngle = src.moonPhaseAngle;
    dst.dayFactor = src.dayFactor;
    dst.nightFactor = src.nightFactor;
    dst.horizonFactor = src.horizonFactor;
    dst.rainFactor = src.rainFactor;
    dst.wetnessFactor = src.wetnessFactor;
    dst.cloudinessFactor = src.cloudinessFactor;
    return dst;
}

/// Convert GameplaySkyRenderer::SkyIlluminanceData to SkyIlluminanceData
SkyIlluminanceData toSkyIlluminanceData(const GameplaySkyRenderer::SkyIlluminanceData& src) {
    SkyIlluminanceData dst;
    dst.directIlluminance = src.directIlluminance;
    dst.skyIlluminance = src.skyIlluminance;
    dst.sunIlluminance = src.sunIlluminance;
    dst.moonIlluminance = src.moonIlluminance;
    dst.cloudDynamicWeather = src.cloudDynamicWeather;
    return dst;
}

glm::vec2 decodePackedLight(const uint8_t packed) {
    return glm::vec2(
        static_cast<float>((packed >> 4) & 0x0F) / 15.0f,
        static_cast<float>(packed & 0x0F) / 15.0f);
}

glm::vec2 mixLight(const glm::vec2& a, const glm::vec2& b, const float t) {
    return a + (b - a) * t;
}

float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float computeHeldItemSceneHdrScale(const FrameContext& ctx,
                                   const RenderSettings& settings,
                                   const PipelineMode pipelineMode) {
    if (pipelineMode == PipelineMode::Forward) {
        return 1.0f;
    }

    const float directEnergy = luminance(ctx.skyIlluminance.directIlluminance) *
                               settings.postProcess.directSunStrength;
    const float skyEnergy = luminance(ctx.skyIlluminance.skyIlluminance) *
                            settings.postProcess.skyAmbientStrength *
                            settings.weather.skylightScale;
    const float weatherAttenuation = 1.0f - std::clamp(ctx.weather.wetness * 0.35f + ctx.weather.storm * 0.45f,
                                                       0.0f,
                                                       0.70f);
    const float scale = 1.0f + directEnergy * 2.25f * weatherAttenuation + skyEnergy * 1.20f;
    return std::clamp(scale, 1.0f, 6.5f);
}

bool beginSceneCaptureRendering(RhiCommandList& commandList,
                                const FrameContext& ctx,
                                const char* debugName) {
    if (!ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachment;
    colorAttachment.view = ctx.sceneCaptureColorView;
    colorAttachment.loadOp = RhiLoadOp::Load;
    colorAttachment.storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.sceneCaptureDepthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = debugName;
    renderingInfo.renderArea = {
        0,
        0,
        ctx.renderExtent.width,
        ctx.renderExtent.height
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList.beginRendering(renderingInfo);
    return true;
}

bool beginWeatherRendering(RhiCommandList& commandList,
                           const FrameContext& ctx,
                           DeferredRenderTargets* targets,
                           const bool writeTemporalMasks) {
    if (!writeTemporalMasks) {
        return beginSceneCaptureRendering(commandList, ctx, "SceneCapture.Weather");
    }
    if (targets == nullptr || !ctx.sceneCaptureColorView.isValid() ||
        !ctx.sceneCaptureDepthView.isValid() ||
        !targets->reactiveMaskTextureViewHandle().isValid() ||
        !targets->transparencyMaskTextureViewHandle().isValid()) {
        return false;
    }

    RhiColorAttachment colorAttachments[3];
    colorAttachments[0].view = ctx.sceneCaptureColorView;
    colorAttachments[0].loadOp = RhiLoadOp::Load;
    colorAttachments[0].storeOp = RhiStoreOp::Store;
    colorAttachments[1].view = targets->reactiveMaskTextureViewHandle();
    colorAttachments[1].loadOp = RhiLoadOp::Load;
    colorAttachments[1].storeOp = RhiStoreOp::Store;
    colorAttachments[2].view = targets->transparencyMaskTextureViewHandle();
    colorAttachments[2].loadOp = RhiLoadOp::Load;
    colorAttachments[2].storeOp = RhiStoreOp::Store;

    RhiDepthStencilAttachment depthAttachment;
    depthAttachment.view = ctx.sceneCaptureDepthView;
    depthAttachment.depthLoadOp = RhiLoadOp::Load;
    depthAttachment.depthStoreOp = RhiStoreOp::Store;

    RhiRenderingInfo renderingInfo;
    renderingInfo.debugName = "SceneCapture.Weather";
    renderingInfo.renderArea = {0, 0, ctx.renderExtent.width, ctx.renderExtent.height};
    renderingInfo.colorAttachments = colorAttachments;
    renderingInfo.colorAttachmentCount = 3u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    commandList.beginRendering(renderingInfo);
    return true;
}

glm::vec2 sampleHeldItemLight(const IWorldView& worldView, const glm::vec3& cameraPosition) {
    const int x0 = static_cast<int>(std::floor(cameraPosition.x));
    const int y0 = static_cast<int>(std::floor(cameraPosition.y));
    const int z0 = static_cast<int>(std::floor(cameraPosition.z));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const float tx = glm::fract(cameraPosition.x);
    const float ty = glm::fract(cameraPosition.y);
    const float tz = glm::fract(cameraPosition.z);

    const glm::vec2 c000 = decodePackedLight(worldView.getPackedLight(x0, y0, z0));
    const glm::vec2 c100 = decodePackedLight(worldView.getPackedLight(x1, y0, z0));
    const glm::vec2 c010 = decodePackedLight(worldView.getPackedLight(x0, y1, z0));
    const glm::vec2 c110 = decodePackedLight(worldView.getPackedLight(x1, y1, z0));
    const glm::vec2 c001 = decodePackedLight(worldView.getPackedLight(x0, y0, z1));
    const glm::vec2 c101 = decodePackedLight(worldView.getPackedLight(x1, y0, z1));
    const glm::vec2 c011 = decodePackedLight(worldView.getPackedLight(x0, y1, z1));
    const glm::vec2 c111 = decodePackedLight(worldView.getPackedLight(x1, y1, z1));

    const glm::vec2 x00 = mixLight(c000, c100, tx);
    const glm::vec2 x10 = mixLight(c010, c110, tx);
    const glm::vec2 x01 = mixLight(c001, c101, tx);
    const glm::vec2 x11 = mixLight(c011, c111, tx);
    const glm::vec2 y0Mix = mixLight(x00, x10, ty);
    const glm::vec2 y1Mix = mixLight(x01, x11, ty);
    return mixLight(y0Mix, y1Mix, tz);
}
} // anonymous namespace

RenderScene::RenderScene() = default;
RenderScene::~RenderScene() = default;

void RenderScene::init(ResourceMgr& resourceMgr) {
    // Phase 9: Populate shared resources
    m_shared.resources = &resourceMgr;

    // Phase R4: Initialize terrain streaming service
    // Note: Thread pool initialization is deferred until setupResources() is called

    // Phase R6: Initialize debug service
    m_debugService.init(resourceMgr.rhiDevice());

    // Phase 9: Initialize pipelines
    m_forwardPipeline = std::make_unique<ForwardPipeline>();
    m_deferredPipeline = std::make_unique<DeferredPipeline>();

    // Set initial active pipeline based on settings
    if (m_settings.pipelineMode == PipelineMode::Deferred) {
        m_activePipeline = m_deferredPipeline.get();
    } else {
        m_activePipeline = m_forwardPipeline.get();
    }

    // Note: Pipeline init is deferred until shared resources are fully populated
    // (terrain, targets, sky). This happens when setupResources() is called
    // and the Renderer exposes its resources.
}

void RenderScene::shutdown() {
    if (m_shared.rhiDevice != nullptr) {
        m_sceneOverlayGraph.releaseTransientResources(*m_shared.rhiDevice);
        m_frameBeginGraph.releaseTransientResources(*m_shared.rhiDevice);
    }

    // Phase 9: Shutdown pipelines
    if (m_activePipeline) {
        m_activePipeline->shutdown();
        m_activePipelineInitialized = false;
        m_activePipeline = nullptr;
    }
    m_forwardPipeline.reset();
    m_deferredPipeline.reset();

    // Phase R4: Shutdown terrain streaming service
    m_terrainStreamingService.shutdown();

    // Phase R5: Shutdown overlay renderer
    m_overlayRenderer.shutdown();

    // Phase R6: Shutdown debug service
    m_debugService.shutdown();

    // Phase 5: Shutdown shared post-process pass
    m_fsr1Pass.shutdown();
    m_fsr1Supported = false;
    m_temporalUpscalePass.shutdown();
    m_postProcessPass.shutdown();
}

bool RenderScene::renderFrame(const IWorldView& worldView,
                              const Camera& camera,
                              const Window& window,
                              const glm::ivec2& frameRenderSize,
                              const glm::ivec2& frameOutputSize,
                              const float frameAspectRatio,
                              const DayNightSystem& dayNightSystem,
                              const WeatherSystem& weatherSystem) {
    if (!prepareFrameResources(frameRenderSize)) {
        return false;
    }

    if (m_shared.commandListPool == nullptr || m_shared.rhiDevice == nullptr) {
        std::abort();
    }
    if (!executeFrameBeginGraph()) {
        return false;
    }
    m_terrainStreamingService.beginFrame();
    if (m_blockEntityRenderer != nullptr) {
        m_blockEntityRenderer->beginFrame();
    }

    // Build frame context
    const std::optional<FrameContext> frameContext = buildFrameContext(
        worldView, camera, window, frameRenderSize, frameOutputSize, frameAspectRatio,
        dayNightSystem, weatherSystem);
    if (!frameContext.has_value()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Failed to resolve temporal frame parameters\n");
        m_terrainStreamingService.endFrame();
        return false;
    }
    m_currentContext = *frameContext;

    // Phase 9: Use active pipeline only if fully initialized and ready.
    // All shared resources must be populated AND pipeline must have been init'd.
    const bool newPipelineReady = m_activePipelineInitialized && isNewPipelineReady();

    // R7: New pipeline is the only path (legacy fallback removed)
    if (!newPipelineReady || !m_newPipelineActive) {
        m_terrainStreamingService.endFrame();
        return false;
    }

    m_lastFrameOutput = m_activePipeline->renderFrame(m_currentContext, m_settings);
    if (!m_lastFrameOutput.sceneColor.isValid() ||
        !m_lastFrameOutput.sceneDepth.isValid()) {
        m_terrainStreamingService.endFrame();
        return false;
    }
    return true;
}

bool RenderScene::executeFrameBeginGraph() {
    if (m_shared.rhiDevice == nullptr || m_shared.commandListPool == nullptr) {
        return false;
    }

    m_frameBeginGraph.reset();
    RenderGraphPassBuilder timerReset = m_frameBeginGraph.addPass(
        {"RenderDebug.TimerReset", RgPassType::Graphics, RhiQueueType::Graphics});
    timerReset.setExecute([this](RgPassContext& pass) {
        m_debugService.beginFrame(pass.commandList());
        return true;
    });

    const RgCompileResult compiled = m_frameBeginGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Frame-begin Render Graph compile failed: "
                      << compiled.message << '\n');
        return false;
    }
    const RgExecuteResult executed = m_frameBeginGraph.execute(
        *m_shared.rhiDevice, *m_shared.commandListPool);
    if (!executed.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Frame-begin Render Graph execution failed: "
                      << executed.message << '\n');
        return false;
    }
    return true;
}

bool RenderScene::executeSceneOverlayGraph(
    const RenderGameplayFrameRequest& request,
    const glm::ivec2& frameRenderSize,
    const bool lightDebugActive,
    float& cameraRainVisibility) {
    if (m_shared.rhiDevice == nullptr || m_shared.commandListPool == nullptr) {
        return false;
    }

    const WeatherDerived weather = request.weatherSystem.getDerived();
    const bool precipitationConfigured =
        !lightDebugActive && m_settings.weather.rainLinesEnabled;
    const bool precipitationVisible =
        precipitationConfigured &&
        (weather.rainStrength > 0.01f || weather.snowStrength > 0.01f);
    if (!lightDebugActive) {
        cameraRainVisibility = m_currentContext.cameraRainVisibility;
    }

    const glm::mat4 weatherView = request.camera.getViewMatrix();
    if (precipitationConfigured) {
        request.rainRenderer.prepareFrame(
            request.camera.getPosition(), weatherView,
            weather.rainStrength, weather.snowStrength, request.frameTime);
    }

    const bool heldItemVisible = request.renderFirstPersonHeldItem &&
                                 request.firstPersonHeldItemRenderer != nullptr &&
                                 request.firstPersonInventory != nullptr &&
                                 request.firstPersonHeldItemMotion != nullptr;
    if (heldItemVisible) {
        request.firstPersonHeldItemRenderer->setShadowData(
            FirstPersonHeldItemRenderer::fromFirstPersonShadowData(
                getHeldItemShadowData()));
        const glm::vec2 heldLight = sampleHeldItemLight(
            request.worldView, request.camera.getPosition());
        request.firstPersonHeldItemRenderer->setEnvironmentLight(
            heldLight.x, heldLight.y);
        request.firstPersonHeldItemRenderer->setSceneHdrScale(
            computeHeldItemSceneHdrScale(
                m_currentContext, m_settings, getPipelineMode()));
        request.firstPersonHeldItemRenderer->prepareFrameResources(
            *request.firstPersonInventory);
        request.firstPersonHeldItemRenderer->prepareFrame(
            frameRenderSize.x, frameRenderSize.y,
            *request.firstPersonInventory,
            *request.firstPersonHeldItemMotion,
            static_cast<float>(Time::getGameTime()));
    }

    RhiDevice& rhiDevice = *m_shared.rhiDevice;
    m_sceneOverlayGraph.reset();
    const auto importTexture = [&](const RhiTextureHandle texture,
                                   const RhiTextureViewHandle view,
                                   const RhiResourceState stableState,
                                   RgTextureHandle& graphTexture) {
        RhiTextureDesc desc;
        if (!rhiDevice.getTextureDesc(texture, desc)) {
            return false;
        }
        RgImportedTextureDesc imported;
        imported.name = desc.debugName;
        imported.texture = texture;
        imported.desc = desc;
        imported.initialState = stableState;
        imported.finalState = stableState;
        imported.defaultView = view;
        graphTexture = m_sceneOverlayGraph.importTexture(imported);
        return graphTexture.isValid();
    };

    RgTextureHandle sceneColor;
    RgTextureHandle sceneDepth;
    if (!importTexture(m_currentContext.sceneCaptureColorTexture,
                       m_currentContext.sceneCaptureColorView,
                       RhiResourceState::ShaderRead,
                       sceneColor) ||
        !importTexture(m_currentContext.sceneCaptureDepthTexture,
                       m_currentContext.sceneCaptureDepthView,
                       RhiResourceState::DepthRead,
                       sceneDepth)) {
        return false;
    }

    const glm::mat4 viewProj =
        m_currentContext.camera.projection * m_currentContext.camera.view;
    const IWorldView* overlayWorldView = &request.worldView;
    const BlockTargetRenderData* overlayTarget = &request.target;
    const BlockBreakRenderData* overlayBlockBreak = &request.blockBreak;
    RenderGraphPassBuilder overlay = m_sceneOverlayGraph.addPass(
        {"SceneOverlay.BlockInteraction", RgPassType::Graphics,
         RhiQueueType::Graphics});
    overlay.readWriteTexture(sceneColor, RhiResourceState::RenderTarget)
        .readWriteTexture(sceneDepth, RhiResourceState::DepthWrite)
        .setExecute([this, overlayWorldView, overlayTarget, overlayBlockBreak,
                     viewProj](RgPassContext& pass) {
            RhiCommandList& commandList = pass.commandList();
            if (!beginSceneCaptureRendering(
                    commandList, m_currentContext,
                    "SceneCapture.BlockOverlay")) {
                return false;
            }
            m_overlayRenderer.render(
                *overlayWorldView, viewProj,
                *overlayTarget, *overlayBlockBreak, commandList);
            commandList.endRendering();
            return true;
        });
    RgPassHandle graphTail = overlay.handle();

    if (precipitationVisible) {
        const bool hardwareDepthTest = getPipelineMode() == PipelineMode::Forward;
        const bool writeTemporalMasks = !hardwareDepthTest;
        DeferredRenderTargets* targets = m_shared.deferredTargets;
        if (writeTemporalMasks &&
            (targets == nullptr ||
             !targets->ensureReactiveMaskTextureView(rhiDevice) ||
             !targets->ensureTransparencyMaskTextureView(rhiDevice))) {
            return false;
        }

        RgTextureHandle precipitationDepth;
        RgTextureHandle reactiveMask;
        RgTextureHandle transparencyMask;
        if (!hardwareDepthTest &&
            !importTexture(m_lastFrameOutput.gbufferDepth, {},
                           RhiResourceState::DepthRead,
                           precipitationDepth)) {
            return false;
        }
        if (writeTemporalMasks &&
            (!importTexture(targets->reactiveMaskTextureHandle(),
                            targets->reactiveMaskTextureViewHandle(),
                            RhiResourceState::ShaderRead,
                            reactiveMask) ||
             !importTexture(targets->transparencyMaskTextureHandle(),
                            targets->transparencyMaskTextureViewHandle(),
                            RhiResourceState::ShaderRead,
                            transparencyMask))) {
            return false;
        }

        const float frameAspect = static_cast<float>(frameRenderSize.x) /
                                  static_cast<float>(std::max(1, frameRenderSize.y));
        const glm::mat4 weatherProjection =
            request.camera.getProjectionMatrix(frameAspect);
        const float alphaScale = m_settings.weather.rainAlphaScale;
        const glm::vec2 precipitationScreenSize(
            static_cast<float>(frameRenderSize.x),
            static_cast<float>(frameRenderSize.y));
        const RhiTextureHandle depthTexture = hardwareDepthTest
            ? RhiTextureHandle{}
            : m_lastFrameOutput.gbufferDepth;
        RainRenderer* precipitationRenderer = &request.rainRenderer;

        RenderGraphPassBuilder precipitation = m_sceneOverlayGraph.addPass(
            {"SceneOverlay.Precipitation", RgPassType::Graphics,
             RhiQueueType::Graphics});
        precipitation.dependsOn(graphTail)
            .readWriteTexture(sceneColor, RhiResourceState::RenderTarget)
            .readWriteTexture(sceneDepth, RhiResourceState::DepthWrite);
        if (!hardwareDepthTest) {
            precipitation.readTexture(
                precipitationDepth, RhiResourceState::DepthRead);
        }
        if (writeTemporalMasks) {
            precipitation
                .readWriteTexture(reactiveMask, RhiResourceState::RenderTarget)
                .readWriteTexture(transparencyMask,
                                  RhiResourceState::RenderTarget);
        }
        precipitation.setExecute(
            [this, precipitationRenderer, weather, weatherProjection, weatherView,
             targets, writeTemporalMasks, hardwareDepthTest, cameraRainVisibility,
             alphaScale, depthTexture, precipitationScreenSize](RgPassContext& pass) {
            RhiCommandList& commandList = pass.commandList();
            precipitationRenderer->uploadFrame(commandList);
            if (!beginWeatherRendering(
                    commandList, m_currentContext, targets,
                    writeTemporalMasks)) {
                return false;
            }
            if (weather.rainStrength > 0.01f) {
                precipitationRenderer->render(
                    commandList, weatherProjection, weatherView,
                    weather.rainStrength, cameraRainVisibility,
                    alphaScale, depthTexture,
                    precipitationScreenSize, hardwareDepthTest);
            }
            if (weather.snowStrength > 0.01f) {
                precipitationRenderer->renderSnow(
                    commandList, weatherProjection, weatherView,
                    weather.snowStrength, cameraRainVisibility,
                    alphaScale * 0.6f, depthTexture,
                    precipitationScreenSize, hardwareDepthTest);
            }
            commandList.endRendering();
            return true;
        });
        graphTail = precipitation.handle();
    }

    if (heldItemVisible) {
        const FirstPersonShadowData& shadowData = getHeldItemShadowData();
        RgTextureHandle shadowDepth;
        RgTextureHandle shadowDepthAll;
        RgTextureHandle shadowColor0;
        RgTextureHandle shadowColor1;
        if (shadowData.shadowsEnabled != 0) {
            const bool matchingDepthHandles =
                shadowData.shadowTextureHandle.index ==
                    shadowData.shadowDepthRawHandle.index &&
                shadowData.shadowTextureHandle.generation ==
                    shadowData.shadowDepthRawHandle.generation &&
                shadowData.shadowDepthAllHandle.index ==
                    shadowData.shadowDepthAllRawHandle.index &&
                shadowData.shadowDepthAllHandle.generation ==
                    shadowData.shadowDepthAllRawHandle.generation;
            if (!matchingDepthHandles ||
                !importTexture(shadowData.shadowDepthRawHandle, {},
                               RhiResourceState::DepthRead, shadowDepth) ||
                !importTexture(shadowData.shadowDepthAllRawHandle, {},
                               RhiResourceState::DepthRead, shadowDepthAll) ||
                !importTexture(shadowData.shadowColor0Handle, {},
                               RhiResourceState::ShaderRead, shadowColor0) ||
                !importTexture(shadowData.shadowColor1Handle, {},
                               RhiResourceState::ShaderRead, shadowColor1)) {
                return false;
            }
        }

        RenderGraphPassBuilder heldItem = m_sceneOverlayGraph.addPass(
            {"SceneOverlay.FirstPersonHeldItem", RgPassType::Graphics,
             RhiQueueType::Graphics});
        heldItem.dependsOn(graphTail)
            .readWriteTexture(sceneColor, RhiResourceState::RenderTarget)
            .readWriteTexture(sceneDepth, RhiResourceState::DepthWrite);
        if (shadowData.shadowsEnabled != 0) {
            heldItem.readTexture(shadowDepth, RhiResourceState::DepthRead)
                .readTexture(shadowDepthAll, RhiResourceState::DepthRead)
                .readTexture(shadowColor0, RhiResourceState::ShaderRead)
                .readTexture(shadowColor1, RhiResourceState::ShaderRead);
        }
        FirstPersonHeldItemRenderer* heldItemRenderer =
            request.firstPersonHeldItemRenderer;
        heldItem.setExecute([this, heldItemRenderer](RgPassContext& pass) {
            RhiCommandList& commandList = pass.commandList();
            heldItemRenderer->prepareRhiFrame(commandList);
            if (!beginSceneCaptureRendering(
                    commandList, m_currentContext,
                    "SceneCapture.FirstPersonHeldItem")) {
                return false;
            }
            heldItemRenderer->renderPrepared(commandList);
            commandList.endRendering();
            return true;
        });
    }

    const RgCompileResult compiled = m_sceneOverlayGraph.compile();
    if (!compiled.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Scene-overlay Render Graph compile failed: "
                      << compiled.message << '\n');
        return false;
    }
    const RgExecuteResult executed = m_sceneOverlayGraph.execute(
        rhiDevice, *m_shared.commandListPool);
    if (!executed.succeeded()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Scene-overlay Render Graph execution failed: "
                      << executed.message << '\n');
        return false;
    }
    return true;
}

void RenderScene::renderGameplayFrame(const RenderGameplayFrameRequest& request) {
    // Activate the pipeline when shared resources become available after target initialization.
    if (!isNewPipelineActive() && isNewPipelineReady()) {
        setNewPipelineActive(true);
    }

    const bool skipPostProcess = getPipelineMode() == PipelineMode::Forward;
    const glm::ivec2 displaySize(std::max(1, request.framebufferWidth),
                                std::max(1, request.framebufferHeight));
    glm::ivec2 frameRenderSize = displaySize;
    if (!skipPostProcess) {
        const std::optional<glm::ivec2> resolvedRenderSize =
            internalRenderSize(displaySize);
        if (!resolvedRenderSize.has_value()) {
            MECRAFT_LOG_STREAM(
                std::cerr << "[RenderScene] Invalid temporal upscaler resolution settings\n");
            return;
        }
        frameRenderSize = *resolvedRenderSize;
    }
    const float frameAspectRatio = static_cast<float>(displaySize.x) /
                                   static_cast<float>(displaySize.y);
    if (!m_postProcessPass.beginSceneCapture(*m_shared.rhiDevice,
                                             frameRenderSize.x,
                                             frameRenderSize.y)) {
        MECRAFT_LOG_STREAM(std::cerr << "[RenderScene] Failed to begin post-process scene capture\n");
        return;
    }

    const bool lightDebugActive = isLightDebugActive();
    float cameraRainVisibility = 1.0f;

    if (!renderFrame(request.worldView, request.camera, request.window,
                     frameRenderSize, displaySize, frameAspectRatio,
                     request.dayNightSystem, request.weatherSystem)) {
        return;
    }
    if (!executeSceneOverlayGraph(request, frameRenderSize,
                                  lightDebugActive, cameraRainVisibility)) {
        m_terrainStreamingService.endFrame();
        return;
    }

    if (!m_temporalUpscalePass.prepareOutputTarget(
            m_settings.upscale,
            m_currentContext.renderExtent,
            m_currentContext.outputExtent)) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Failed to prepare temporal HDR output target\n");
        m_terrainStreamingService.endFrame();
        return;
    }
    refreshTemporalFrameInput();
    if (m_temporalFrameInput.has_value()) {
        m_temporalFrameInput->renderingGameFrames = request.renderingGameFrames;
    }
    m_temporalUpscaleResult.reset();
    if (m_temporalFrameInput.has_value()) {
        m_temporalUpscaleResult = m_temporalUpscalePass.execute(
            m_settings.upscale,
            *m_temporalFrameInput);
        if (!m_temporalUpscaleResult->succeeded()) {
            MECRAFT_LOG_STREAM(
                std::cerr << "[RenderScene] "
                          << TemporalUpscalePass::statusText(m_temporalUpscaleResult->status)
                          << '\n');
            m_terrainStreamingService.endFrame();
            return;
        }
        if (!m_postProcessPass.setHdrInput(
                m_temporalUpscaleResult->outputHdrColor,
                m_temporalUpscaleResult->outputHdrColorView,
                static_cast<int>(m_temporalUpscaleResult->outputExtent.width),
                static_cast<int>(m_temporalUpscaleResult->outputExtent.height))) {
            MECRAFT_LOG_STREAM(
                std::cerr << "[RenderScene] Failed to configure the post-process HDR input\n");
            m_terrainStreamingService.endFrame();
            return;
        }
    } else if (!skipPostProcess && !isFsr1RuntimeEnabled()) {
        MECRAFT_LOG_STREAM(
            std::cerr << "[RenderScene] Temporal frame input is unavailable\n");
        m_terrainStreamingService.endFrame();
        return;
    }

    if (skipPostProcess) {
        if (!m_postProcessPass.blitSceneCaptureToBackbuffer(
                *m_shared.rhiDevice,
                m_currentContext.swapchainColorView,
                m_debugService)) {
            m_terrainStreamingService.endFrame();
            return;
        }
    } else {
        PostProcessEffects effects = buildPostProcessEffects(
            request.worldView, request.camera, frameAspectRatio,
            cameraRainVisibility, request.screenRollRadians,
            request.dayNightSystem, request.weatherSystem);
        m_postProcessPass.setFrameEffects(effects);
        if (lightDebugActive) {
            if (!m_postProcessPass.blitSceneCaptureToBackbuffer(
                    *m_shared.rhiDevice,
                    m_currentContext.swapchainColorView,
                    m_debugService)) {
                m_terrainStreamingService.endFrame();
                return;
            }
        } else {
            const bool fsrEnabled = isFsr1RuntimeEnabled();
            if (fsrEnabled) {
                const RhiTextureHandle postTexture = m_postProcessPass.compositeToTexture(
                    *m_shared.rhiDevice,
                    request.frameTime,
                    m_lastFrameOutput.gbufferDepth,
                    m_debugService);
                if (!postTexture.isValid()) {
                    std::abort();
                }
                const int inputWidth = m_postProcessPass.targetWidth();
                const int inputHeight = m_postProcessPass.targetHeight();
                if (!m_fsr1Pass.execute(
                        *m_shared.rhiDevice,
                        m_currentContext.swapchainColorView,
                        postTexture,
                        m_postProcessPass.compositeTextureViewHandle(),
                        inputWidth,
                        inputHeight,
                        displaySize.x,
                        displaySize.y,
                        m_settings.upscale.fsr1Sharpness,
                        m_debugService)) {
                    std::abort();
                }
            } else {
                if (!m_postProcessPass.compositeToBackbuffer(
                        *m_shared.rhiDevice,
                        m_currentContext.swapchainColorView,
                        m_currentContext.swapchainColorFormat,
                        displaySize.x,
                        displaySize.y,
                        request.frameTime,
                        m_lastFrameOutput.gbufferDepth,
                        m_debugService)) {
                    m_terrainStreamingService.endFrame();
                    return;
                }
            }
        }
    }

    m_terrainStreamingService.endFrame();
}

void RenderScene::setPipelineMode(PipelineMode mode) {
    if (m_settings.pipelineMode == mode) return;

    m_settings.pipelineMode = mode;
    invalidateFrameHistory();

    // Phase 9: Switch active pipeline
    if (m_forwardPipeline && m_deferredPipeline) {
        // Shutdown current pipeline
        if (m_activePipeline && m_activePipelineInitialized) {
            m_activePipeline->shutdown();
            m_activePipelineInitialized = false;
        }

        // Switch to new pipeline
        m_activePipeline = (mode == PipelineMode::Deferred)
            ? static_cast<RenderPipeline*>(m_deferredPipeline.get())
            : static_cast<RenderPipeline*>(m_forwardPipeline.get());

        // Initialize when all shared resources required by the active path are present.
        if (isNewPipelineReady()) {
            m_activePipeline->init(m_shared);
            m_activePipelineInitialized = true;
            // Re-evaluate: if new pipeline checkbox was checked, keep it active for the new pipeline
            m_newPipelineActive = m_newPipelineActive && isNewPipelineReady() && m_activePipelineInitialized;
        } else {
            m_newPipelineActive = false;
        }
    }
}

PipelineMode RenderScene::getPipelineMode() const {
    return m_settings.pipelineMode;
}

const char* RenderScene::activePipelineName() const {
    // Phase 9: Use active pipeline name if available
    if (m_activePipeline) {
        return m_activePipeline->name();
    }

    // Fallback to settings-based name
    switch (m_settings.pipelineMode) {
        case PipelineMode::Forward: return "Forward (Vanilla)";
        case PipelineMode::Deferred: return "Deferred (Shader Effects)";
        default: return "Unknown";
    }
}

void RenderScene::setSettings(const RenderSettings& settings) {
    if (m_settingsChangedCallback && !m_settingsChangedCallback(settings)) {
        return;
    }
    // Detect pipeline mode change and trigger switch
    if (settings.pipelineMode != m_settings.pipelineMode) {
        setPipelineMode(settings.pipelineMode);
    }

    const bool upscaleChanged =
        settings.upscale.type != m_settings.upscale.type ||
        settings.upscale.quality != m_settings.upscale.quality ||
        settings.upscale.outputWidth != m_settings.upscale.outputWidth ||
        settings.upscale.outputHeight != m_settings.upscale.outputHeight ||
        settings.upscale.dynamicResolutionEnabled !=
            m_settings.upscale.dynamicResolutionEnabled ||
        settings.upscale.debugVisualizationEnabled !=
            m_settings.upscale.debugVisualizationEnabled ||
        settings.upscale.fsr1Enabled != m_settings.upscale.fsr1Enabled ||
        std::abs(settings.upscale.fsr1RenderScale - m_settings.upscale.fsr1RenderScale) > 0.0001f;

    m_settings = settings;

    if (upscaleChanged) {
        invalidateFrameHistory();
    }

}

const RenderSettings& RenderScene::getSettings() const {
    return m_settings;
}

bool RenderScene::isFsr31Supported() const {
#if defined(MECRAFT_ENABLE_FSR31)
    return m_shared.rhiDevice != nullptr &&
           m_shared.rhiDevice->backend() == RhiBackend::Vulkan &&
           m_settings.pipelineMode == PipelineMode::Deferred;
#else
    return false;
#endif
}

bool RenderScene::isDlssSupported() const {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    const StreamlineRuntime& streamline = StreamlineRuntime::instance();
    return m_shared.rhiDevice != nullptr &&
           m_shared.rhiDevice->backend() == RhiBackend::Vulkan &&
           m_settings.pipelineMode == PipelineMode::Deferred &&
           streamline.initialized() && streamline.vulkanDeviceSet();
#else
    return false;
#endif
}

bool RenderScene::isReflexSupported() const {
#if defined(MECRAFT_ENABLE_STREAMLINE)
    const StreamlineRuntime& streamline = StreamlineRuntime::instance();
    return m_shared.rhiDevice != nullptr &&
           m_shared.rhiDevice->backend() == RhiBackend::Vulkan &&
           streamline.reflexLowLatencyAvailable();
#else
    return false;
#endif
}

bool RenderScene::supportsFrameGenerationInputs() const {
    return supportsDlssFrameGenerationInputs(
        m_settings, m_fsr1Supported);
}

void RenderScene::setSettingsChangedCallback(
    std::function<bool(const RenderSettings&)> callback) {
    m_settingsChangedCallback = std::move(callback);
}

const VoxelGiClipmapStats& RenderScene::getVoxelGiClipmapStats() const {
    static const VoxelGiClipmapStats kEmptyStats{};
    if (m_deferredPipeline && m_deferredPipeline->voxelGiClipmap()) {
        return m_deferredPipeline->voxelGiClipmap()->stats();
    }
    return kEmptyStats;
}

void RenderScene::setBlockEntityRenderer(BlockEntityRenderer* ber) {
    m_blockEntityRenderer = ber;
    m_shared.blockEntityRenderer = ber;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setBlockEntityRenderer(ber);
    }
}

void RenderScene::setHumanoidRenderer(HumanoidRenderer* hr) {
    m_humanoidRenderer = hr;
    m_shared.humanoidRenderer = hr;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setHumanoidRenderer(hr);
    }
}

void RenderScene::setDropRenderer(DropRenderer* dr) {
    m_dropRenderer = dr;
    m_shared.dropRenderer = dr;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setDropRenderer(dr);
    }
}

void RenderScene::setFallingBlockRenderer(FallingBlockRenderer* fbr) {
    m_fallingBlockRenderer = fbr;
    m_shared.fallingBlockRenderer = fbr;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setFallingBlockRenderer(fbr);
    }
}

void RenderScene::setParticleSystem(ParticleSystem* ps) {
    m_particleSystem = ps;
    m_shared.particleSystem = ps;
}

void RenderScene::setDropSystem(DropSystem* ds) {
    m_dropSystem = ds;
    m_shared.dropSystem = ds;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setDropSystem(ds);
    }
}

void RenderScene::setGameplayRegistry(ecs::GameplayRegistry* reg) {
    m_gameplayRegistry = reg;
    m_shared.gameplayRegistry = reg;
    if (m_deferredPipeline && m_deferredPipeline->shadowPass()) {
        m_deferredPipeline->shadowPass()->setGameplayRegistry(reg);
    }
}

const FrameOutput& RenderScene::getLastFrameOutput() const {
    return m_lastFrameOutput;
}

void RenderScene::setupResources(
    ThreadPool* threadPool,
    RhiDevice* rhiDevice,
    RhiCommandListPool* commandListPool,
    TerrainRenderer* terrain,
    TerrainRhiPipelineSet* terrainRhiPipelines,
    WorldRenderBuffer* worldRenderBuffer,
    DeferredRenderTargets* deferredTargets,
    GameplaySkyRenderer* sky,
    shadow::ShadowRenderer* shadowRenderer,
    const RenderSettings& initialSettings) {

    m_settings = initialSettings;
    m_activePipeline = (m_settings.pipelineMode == PipelineMode::Deferred)
        ? static_cast<RenderPipeline*>(m_deferredPipeline.get())
        : static_cast<RenderPipeline*>(m_forwardPipeline.get());

    m_terrainStreamingService.init(threadPool, worldRenderBuffer);
    m_shared.overlayRenderer = &m_overlayRenderer;

    m_shared.rhiDevice = rhiDevice;
    m_shared.commandListPool = commandListPool;
    if (rhiDevice == nullptr || commandListPool == nullptr) std::abort();
    m_temporalUpscalePass.init(*rhiDevice, *commandListPool);
    m_postProcessPass.init(*m_shared.resources, *commandListPool);
    m_fsr1Supported = Fsr1Pass::isSupported(*rhiDevice);
    if (m_fsr1Supported) {
        m_fsr1Pass.init(*m_shared.resources, *commandListPool);
    }
    m_overlayRenderer.init(*m_shared.resources, *rhiDevice);
    m_shared.terrainCache = &m_terrainStreamingService.terrainCache();
    m_shared.terrainStreaming = &m_terrainStreamingService;
    m_shared.terrain = terrain;
    m_shared.terrainRhiPipelines = terrainRhiPipelines;
    m_shared.worldRenderBuffer = worldRenderBuffer;
    m_shared.meshingService = &m_terrainStreamingService.meshingService();
    m_shared.deferredTargets = deferredTargets;
    m_shared.sky = sky;
    m_shared.shadowRenderer = shadowRenderer;
    m_shared.threadPool = threadPool;
}

void RenderScene::setEyeInWater(bool inWater) {
    m_eyeInWater = inWater;
}

void RenderScene::setRenderLocalPlayerModel(bool visible) {
    m_renderLocalPlayerModel = visible;
}

void RenderScene::setHeldBlockLightValue(int value) {
    if (m_deferredPipeline) {
        m_deferredPipeline->setHeldBlockLightValue(value);
    }
}

// R7: Legacy bridge methods removed — use renderFrame() instead

bool RenderScene::isLightDebugActive() const {
    return m_settings.debug.deferredLightDebugMode > 0 || m_settings.debug.reflectionDebugMode > 0;
}

RenderGraphFrameStats RenderScene::renderGraphFrameStats() const {
    if (m_deferredPipeline == nullptr) {
        return {};
    }
    return m_deferredPipeline->renderGraphFrameStats();
}

RenderScene::PresentationDebugInfo RenderScene::presentationDebugInfo() const {
    PresentationDebugInfo info;
    info.renderWidth = m_currentContext.renderExtent.width;
    info.renderHeight = m_currentContext.renderExtent.height;
    info.outputWidth = m_currentContext.outputExtent.width;
    info.outputHeight = m_currentContext.outputExtent.height;
    if (m_shared.rhiDevice != nullptr) {
        info.presentMode = m_shared.rhiDevice->capabilities().swapchainPresentMode;
        info.valid = true;
    }
    return info;
}

bool RenderScene::isNewPipelineReady() const {
    if (!m_activePipeline || !m_shared.rhiDevice ||
        !m_shared.rhiDevice->currentSwapchainColorView().isValid() ||
        !m_shared.rhiDevice->currentSwapchainDepthStencilView().isValid() ||
        !m_shared.terrain || !m_shared.sky || !m_shared.resources) {
        return false;
    }
    // Deferred pipeline requires deferredTargets; forward pipeline does not.
    if (m_activePipeline->supportsDeferred()) {
        return m_shared.deferredTargets != nullptr;
    }
    return true;
}

void RenderScene::setNewPipelineActive(bool active) {
    const bool wasActive = m_newPipelineActive;
    if (active && !m_activePipelineInitialized && isNewPipelineReady()) {
        m_activePipeline->init(m_shared);
        m_activePipelineInitialized = true;
    }
    m_newPipelineActive = active && isNewPipelineReady() && m_activePipelineInitialized;
    if (m_newPipelineActive && !wasActive) {
        invalidateFrameHistory();
    }
}

const char* RenderScene::getPipelineStatus() const {
    if (!m_activePipeline) return "No active pipeline";
    if (!m_shared.rhiDevice) return "Missing: rhiDevice";
    if (!m_shared.rhiDevice->currentSwapchainColorView().isValid()) return "Missing: swapchainColorView";
    if (!m_shared.rhiDevice->currentSwapchainDepthStencilView().isValid()) return "Missing: swapchainDepthStencilView";
    if (!m_shared.terrain) return "Missing: terrain";
    if (!m_shared.sky) return "Missing: sky";
    if (!m_shared.resources) return "Missing: resources";
    if (m_activePipeline->supportsDeferred() && !m_shared.deferredTargets) return "Missing: deferredTargets";
    if (!m_activePipelineInitialized) return "Ready (not initialized)";
    if (!m_newPipelineActive) return "Ready (inactive)";
    return "Active";
}

bool RenderScene::prepareFrameResources(const glm::ivec2& frameRenderSize) {
    if (!m_activePipeline || !m_activePipeline->supportsDeferred() || m_shared.deferredTargets == nullptr) {
        return true;
    }

    DeferredRenderTargets& targets = *m_shared.deferredTargets;
    return targets.ensureSize(frameRenderSize.x, frameRenderSize.y, m_settings.shadow.resolution);
}

PostProcessEffects RenderScene::buildPostProcessEffects(const IWorldView& worldView, const Camera& camera,
                                                         const float frameAspectRatio, float cameraRainVisibility,
                                                         float screenRollRadians,
                                                         const DayNightSystem& dayNightSystem,
                                                         const WeatherSystem& weatherSystem) const {
    PostProcessEffects effects;

    // Basic state
    effects.underwaterEnabled = m_eyeInWater;
    effects.screenRollRadians = screenRollRadians;

    // Post-process settings from RenderSettings
    effects.bloomEnabled = m_settings.postProcess.bloomEnabled;
    effects.bloomThreshold = m_settings.postProcess.bloomThreshold;
    effects.bloomStrength = m_settings.postProcess.bloomStrength;
    effects.bloomMipCount = m_settings.postProcess.bloomMipCount;
    effects.autoExposureEnabled = m_settings.postProcess.autoExposureEnabled;
    effects.autoExposureMin = m_settings.postProcess.autoExposureMin;
    effects.autoExposureMax = m_settings.postProcess.autoExposureMax;
    effects.autoExposureSpeed = m_settings.postProcess.autoExposureSpeed;
    effects.autoExposureBias = m_settings.postProcess.autoExposureBias;
    effects.autoExposureDayFactor = dayNightSystem.getSkyIntensity();
    effects.sunRaysEnabled = m_settings.postProcess.sunRaysEnabled;
    effects.sunRayStrength = m_settings.postProcess.sunRayStrength;
    effects.shaderpackGradingEnabled = m_settings.postProcess.shaderpackGradingEnabled;
    effects.tonemapMode = m_settings.postProcess.tonemapMode;
    effects.colorTemperature = m_settings.postProcess.colorTemperature;
    effects.vibrance = m_settings.postProcess.vibrance;
    effects.highlightCompression = m_settings.postProcess.highlightCompression;
    effects.filmEmulationStrength = m_settings.postProcess.filmEmulationStrength;
    effects.redModifierStrength = m_settings.postProcess.redModifierStrength;
    effects.colorLuma = glm::vec3(m_settings.postProcess.colorLumaR,
                                   m_settings.postProcess.colorLumaG,
                                   m_settings.postProcess.colorLumaB);
    effects.splitToneStrength = m_settings.postProcess.splitToneStrength;
    effects.vignetteStrength = m_settings.postProcess.vignetteStrength;
    effects.noiseDitherStrength = m_settings.postProcess.noiseDitherStrength;
    effects.sharpenStrength = m_settings.postProcess.sharpenStrength;
    effects.exposure = m_settings.postProcess.exposure;
    effects.gamma = m_settings.postProcess.gamma;
    effects.saturation = m_settings.postProcess.saturation;
    effects.contrast = m_settings.postProcess.contrast;
    effects.purkinjeShiftEnabled = m_settings.postProcess.purkinjeShiftEnabled;
    effects.bloomyFogEnabled = m_settings.postProcess.bloomyFogEnabled;

    // Weather state
    const WeatherState& weather = weatherSystem.getRenderState();
    const WeatherDerived& derived = weatherSystem.getDerived();
    effects.weatherWetness = weather.wetness;
    effects.weatherStorm = weather.storm;
    effects.snowStrength = derived.snowStrength;
    effects.skyWetness = derived.skyWetness;
    effects.fogWetness = derived.fogWetness;
    effects.cloudWetness = derived.cloudWetness;
    effects.weatherExposureBias = m_settings.weather.exposureBias;
    effects.weatherPostRainFog = m_settings.weather.postRainFog;
    effects.cameraRainVisibility = cameraRainVisibility;
    effects.gameTime = static_cast<float>(Time::getRawTime());
    effects.postprocessDebugMode = m_settings.debug.postprocessDebugMode;

    // Calculate the sun position in the top-left screen UV domain used by post-processing.
    {
        const float sunAngle = dayNightSystem.getCelestialAngleRadians();
        glm::vec3 sunDirection(0.25f, std::sin(sunAngle), -std::cos(sunAngle));
        if (glm::length(sunDirection) > 0.0001f) {
            sunDirection = glm::normalize(sunDirection);
        } else {
            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        const glm::mat4 viewProj = camera.getProjectionMatrix(frameAspectRatio) * camera.getViewMatrix();
        const glm::vec4 clip = viewProj * glm::vec4(camera.getPosition() + sunDirection * 256.0f, 1.0f);
        if (clip.w > 0.0001f) {
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            effects.sunScreenPos = glm::vec2(ndc.x * 0.5f + 0.5f,
                                             1.0f - (ndc.y * 0.5f + 0.5f));
            const float onScreenX = 1.0f - std::clamp(std::abs(effects.sunScreenPos.x - 0.5f) * 2.0f, 0.0f, 1.0f);
            const float onScreenY = 1.0f - std::clamp(std::abs(effects.sunScreenPos.y - 0.5f) * 2.0f, 0.0f, 1.0f);
            const float horizonFade = std::clamp((sunDirection.y + 0.05f) / 0.45f, 0.0f, 1.0f);
            effects.sunVisibility = std::clamp(onScreenX * onScreenY * horizonFade, 0.0f, 1.0f);
        }
    }

    return effects;
}

std::optional<FrameContext> RenderScene::buildFrameContext(
    const IWorldView& worldView,
    const Camera& camera,
    const Window& window,
    const glm::ivec2& frameRenderSize,
    const glm::ivec2& frameOutputSize,
    const float frameAspectRatio,
    const DayNightSystem& dayNightSystem,
    const WeatherSystem& weatherSystem) {
    FrameContext ctx;

    // Camera matrices
    ctx.camera.view = camera.getViewMatrix();
    ctx.camera.projection = camera.getProjectionMatrix(frameAspectRatio);
    ctx.camera.viewProj = ctx.camera.projection * ctx.camera.view;
    ctx.camera.invViewProj = glm::inverse(ctx.camera.viewProj);
    ctx.camera.position = camera.getPosition();
    ctx.camera.nearPlane = camera.getNear();
    ctx.camera.farPlane = camera.getFar();
    ctx.camera.fovDegrees = camera.getFOV();
    ctx.cameraPtr = &camera;
    ctx.windowPtr = &window;
    ctx.debugService = &m_debugService;
    ctx.renderLocalPlayerModel = m_renderLocalPlayerModel;

    ctx.renderExtent = {
        static_cast<uint32_t>(std::max(1, frameRenderSize.x)),
        static_cast<uint32_t>(std::max(1, frameRenderSize.y))
    };
    ctx.outputExtent = {
        static_cast<uint32_t>(std::max(1, frameOutputSize.x)),
        static_cast<uint32_t>(std::max(1, frameOutputSize.y))
    };
    ctx.swapchainColorTexture = m_shared.rhiDevice->currentSwapchainColorTexture();
    ctx.swapchainColorView = m_shared.rhiDevice->currentSwapchainColorView();
    ctx.swapchainDepthStencilView = m_shared.rhiDevice->currentSwapchainDepthStencilView();
    ctx.swapchainColorFormat = m_shared.rhiDevice->swapchainColorFormat();
    ctx.swapchainDepthStencilFormat = m_shared.rhiDevice->swapchainDepthStencilFormat();
    ctx.sceneCaptureColorTexture = m_postProcessPass.sceneColorTextureHandle();
    ctx.sceneCaptureDepthTexture = m_postProcessPass.sceneDepthTextureHandle();
    ctx.sceneCaptureColorView = m_postProcessPass.sceneColorTextureViewHandle();
    ctx.sceneCaptureDepthView = m_postProcessPass.sceneDepthTextureViewHandle();

    // Frame timing
    ctx.frameIndex = Time::getFrameIndex();
    ctx.deltaTime = static_cast<float>(Time::deltaTime);
    const double gameTime = Time::getGameTime();
    const double visualTime = Time::getRawTime();
    ctx.animationTime = static_cast<float>(std::fmod(gameTime, 16.0));
    ctx.shaderTime = static_cast<float>(std::fmod(visualTime, 8192.0));

    if (m_settings.upscale.type == TemporalUpscalerType::Fsr31) {
#if defined(MECRAFT_ENABLE_FSR31)
        const Fsr31JitterResult jitter = queryFsr31Jitter(
            ctx.frameIndex, ctx.renderExtent, ctx.outputExtent);
        if (!jitter.succeeded()) {
            return std::nullopt;
        }
        ctx.jitter = jitter.jitter;
#else
        return std::nullopt;
#endif
    } else if (m_settings.upscale.type == TemporalUpscalerType::Dlss) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        const DlssJitterResult jitter = queryDlssJitter(
            ctx.frameIndex, ctx.renderExtent, ctx.outputExtent);
        if (!jitter.succeeded()) {
            return std::nullopt;
        }
        ctx.jitter = jitter.jitter;
#else
        return std::nullopt;
#endif
    } else if (m_shared.deferredTargets) {
        // Native TAA uses the existing DerivativeMain quasi-random sequence.
        // Freezing the sequence holds a zero offset so temporal instability
        // can be attributed to reprojection instead of sub-pixel jitter.
        const float invW = 1.0f / static_cast<float>(std::max(1, m_shared.deferredTargets->width()));
        const float invH = 1.0f / static_cast<float>(std::max(1, m_shared.deferredTargets->height()));
        const float frameCounter = m_settings.taa.freezeJitter
            ? 0.0f
            : static_cast<float>(ctx.frameIndex);
        const float frameX = glm::fract(frameCounter / 1.3247179572f + 0.5f) * 2.0f - 1.0f;
        const float frameY = glm::fract(frameCounter / 1.7548776662f + 0.5f) * 2.0f - 1.0f;
        ctx.jitter.projectionOffset.x = frameX * invW;
        ctx.jitter.projectionOffset.y = frameY * invH;
        ctx.jitter.pixels.x = frameX;
        ctx.jitter.pixels.y = -frameY;
    }

    // Jittered projection matrix
    {
        glm::mat4 jitteredProj = ctx.camera.projection;
        for (int column = 0; column < 4; ++column) {
            jitteredProj[column][0] += ctx.jitter.projectionOffset.x * ctx.camera.projection[column][3];
            jitteredProj[column][1] += ctx.jitter.projectionOffset.y * ctx.camera.projection[column][3];
        }
        ctx.camera.jitteredViewProj = jitteredProj * ctx.camera.view;
        ctx.camera.jitteredInvViewProj = glm::inverse(ctx.camera.jitteredViewProj);
    }

    // Previous frame data (temporal)
    ctx.temporalReset = requiresTemporalReset(
        m_hasPreviousContext,
        m_previousContext.renderExtent,
        m_previousContext.outputExtent,
        ctx.renderExtent,
        ctx.outputExtent);
    if (!ctx.temporalReset) {
        ctx.prevCamera = m_previousContext.camera;
        ctx.previousJitter = m_previousContext.jitter;
        ctx.previousViewProj = m_previousContext.camera.viewProj;
        ctx.previousInvViewProj = m_previousContext.camera.invViewProj;
        ctx.previousJitteredViewProj = m_previousContext.camera.jitteredViewProj;
    } else {
        ctx.prevCamera = ctx.camera;
        ctx.previousJitter = ctx.jitter;
        ctx.previousViewProj = ctx.camera.viewProj;
        ctx.previousInvViewProj = ctx.camera.invViewProj;
        ctx.previousJitteredViewProj = ctx.camera.jitteredViewProj;
    }

    // Velocity reprojection matrix: apply the CURRENT frame's jitter to the
    // previous view-projection. Jitter is a post-divide NDC translation, so
    // reprojecting with an equally offset previous matrix cancels the jitter
    // term in "current - previous" exactly and keeps velocity jitter-free.
    ctx.previousViewProjWithCurrentJitter = ctx.previousViewProj;
    if (usesTemporalProjectionJitter(m_settings.upscale.type, m_settings.taa.enabled)) {
        for (int column = 0; column < 4; ++column) {
            ctx.previousViewProjWithCurrentJitter[column][0] +=
                ctx.jitter.projectionOffset.x * ctx.previousViewProj[column][3];
            ctx.previousViewProjWithCurrentJitter[column][1] +=
                ctx.jitter.projectionOffset.y * ctx.previousViewProj[column][3];
        }
    }

    // Velocity reprojection as one fp64-composed clip-to-previous-clip
    // transform. Building the product on the CPU in double precision removes
    // the ULP mismatch between the differently-composed jittered matrices
    // that otherwise leaks a per-frame sub-pixel offset into the velocity
    // buffer and makes the whole image shimmer under TAA.
    {
        const bool projectionJitter = usesTemporalProjectionJitter(
            m_settings.upscale.type, m_settings.taa.enabled);
        const glm::dmat4 currentRaster = glm::dmat4(
            projectionJitter ? ctx.camera.jitteredViewProj
                             : ctx.camera.viewProj);
        const glm::dmat4 previousRaster =
            glm::dmat4(ctx.previousViewProjWithCurrentJitter);
        ctx.velocityClipToPrevClip =
            glm::mat4(previousRaster * glm::inverse(currentRaster));
    }

    // Weather state from WeatherSystem
    const WeatherState& weather = weatherSystem.getRenderState();
    const WeatherDerived& weatherDerived = weatherSystem.getDerived();
    ctx.weather.wetness = weather.wetness;
    ctx.weather.storm = weather.storm;
    ctx.weather.surfaceWetness = weatherDerived.surfaceWetness;
    ctx.weather.skyWetness = weatherDerived.skyWetness;
    ctx.weather.fogWetness = weatherDerived.fogWetness;
    ctx.weather.cloudWetness = weatherDerived.cloudWetness;
    ctx.weather.precipitation = weatherDerived.precipitation;
    ctx.weather.rainStrength = weatherDerived.rainStrength;
    ctx.weather.thunderStrength = weatherDerived.thunderStrength;
    ctx.weather.lightningFlash = weatherDerived.lightningFlash;
    ctx.weather.aerialReduction = weather.aerialReduction;

    // Sky colors and illuminance
    if (m_shared.sky) {
        auto skyColors = m_shared.sky->computeSkyColors(dayNightSystem);
        ctx.skyColors = toSkyColorsData(skyColors);
        ctx.skyIlluminance = toSkyIlluminanceData(
            m_shared.sky->computeSkyIlluminance(skyColors, ctx.weather.wetness, ctx.weather.storm));
        ctx.skyIntensity = dayNightSystem.getSkyIntensity();
    }

    // Fog settings
    ctx.fog.enabled = m_settings.fog.enabled;
    ctx.fog.mode = m_settings.fog.mode;
    ctx.fog.color = m_settings.fog.color;
    ctx.fog.startDistance = m_settings.fog.startDistance;
    ctx.fog.endDistance = m_settings.fog.endDistance;
    ctx.fog.density = m_settings.fog.density;
    if (m_settings.fog.autoDistanceByRenderDistance) {
        const float chunkSize = 16.0f; // Chunk::SIZE_X
        const float renderDistanceChunks = static_cast<float>(std::max(1, worldView.getRenderDistance()));
        ctx.fog.endDistance = std::max(0.0f, (renderDistanceChunks + m_settings.fog.autoEndOffsetChunks) * chunkSize);
        ctx.fog.startDistance = std::max(0.0f, ctx.fog.endDistance - m_settings.fog.autoFadeWidthChunks * chunkSize);
    }
    ctx.fog.endDistance = std::max(ctx.fog.endDistance, ctx.fog.startDistance + 0.1f);

    // Volumetric settings
    ctx.volumetric.lightEnabled = m_settings.volumetric.lightEnabled;
    ctx.volumetric.uwLightEnabled = m_settings.volumetric.uwLightEnabled;
    ctx.volumetric.fogEnabled = m_settings.volumetric.fogEnabled;
    ctx.volumetric.fogStrength = m_settings.volumetric.fogStrength;
    ctx.volumetric.underwaterLightStrength = m_settings.volumetric.underwaterLightStrength;
    ctx.volumetric.fogCenterHeight = m_settings.volumetric.fogCenterHeight;
    ctx.volumetric.fogHeightSpread = m_settings.volumetric.fogHeightSpread;
    ctx.volumetric.fogNoiseScale = m_settings.volumetric.fogNoiseScale;
    ctx.volumetric.fogLightStrength = m_settings.volumetric.fogLightStrength;
    ctx.volumetric.fogDensityScale = m_settings.volumetric.fogDensityScale;
    ctx.volumetric.fogSamples = std::clamp(m_settings.volumetric.fogSamples, 2, 50);

    // Cloud settings
    ctx.cloud.shadowsEnabled = m_settings.cloud.shadowsEnabled;
    ctx.cloud.shadowStrength = m_settings.cloud.shadowStrength;
    ctx.cloud.shadowScale = m_settings.cloud.shadowScale;
    ctx.cloud.shadowSpeed = m_settings.cloud.shadowSpeed;
    ctx.cloud.timeScale = m_settings.cloud.timeScale;
    const float cloudWetForCoverage = std::clamp(ctx.weather.wetness + ctx.weather.storm * (4.0f / 3.0f), 0.0f, 1.0f);
    const float userCoverageBias = (std::clamp(m_settings.cloud.coverage, 0.0f, 1.0f) - 0.35f) * 0.45f;
    ctx.cloud.coverage = std::clamp(1.0f + cloudWetForCoverage * 0.2f + userCoverageBias, 0.5f, 1.5f);
    ctx.cloud.density = (0.85f + ctx.weather.wetness * 0.35f + ctx.weather.storm * 0.55f) *
                        std::clamp(m_settings.cloud.density, 0.0f, 2.5f);
    float cloudWet = std::clamp(ctx.weather.cloudWetness, 0.0f, 1.0f);
    ctx.cloud.height = std::max(100.0f, m_settings.cloud.height - cloudWet * 200.0f);
    ctx.cloud.thickness = std::max(50.0f, m_settings.cloud.thickness + cloudWet * 1600.0f);
    ctx.cloud.planarCoverage = m_settings.cloud.planarCoverage;
    ctx.cloud.planarDensity = m_settings.cloud.planarDensity;
    ctx.cloud.planarAltitude = m_settings.cloud.planarAltitude;

    // Atmosphere settings (from PostProcessSettings and WeatherRenderSettings)
    ctx.atmosphere.aerialStrength = m_settings.postProcess.aerialStrength;
    ctx.atmosphere.horizonScatterStrength = m_settings.postProcess.horizonScatterStrength;
    ctx.atmosphere.sunWarmth = m_settings.postProcess.sunWarmth;
    ctx.atmosphere.skyCoolness = m_settings.postProcess.skyCoolness;
    ctx.atmosphere.directWeatherOcclusionOverride = (m_settings.weather.directWeatherOcclusion >= 0.0f) ? 1 : 0;
    ctx.atmosphere.directWeatherOcclusion = std::clamp(m_settings.weather.directWeatherOcclusion, 0.0f, 1.0f);

    // State flags
    ctx.moonShadowActive = ctx.skyColors.moonVisibility > ctx.skyColors.sunVisibility;
    ctx.eyeInWater = m_eyeInWater;

    // Multi-ray outdoor check is only needed while sky precipitation effects are active.
    if (ctx.weather.skyWetness > 0.01f) {
        ctx.cameraRainVisibility = computeCameraRainVisibility(worldView, ctx.camera.position);
    }

    // Shared resources and world/environment pointers
    ctx.shared = &m_shared;
    ctx.worldView = &worldView;
    ctx.dayNightSystem = &dayNightSystem;
    ctx.weatherSystem = &weatherSystem;

    // Store current context as previous for next frame
    m_previousContext = ctx;
    m_hasPreviousContext = true;

    return ctx;
}

std::optional<glm::ivec2> RenderScene::internalRenderSize(
    const glm::ivec2& displaySize) const {
    const int displayWidth = std::max(1, displaySize.x);
    const int displayHeight = std::max(1, displaySize.y);
    if (m_settings.upscale.type == TemporalUpscalerType::Fsr31) {
#if defined(MECRAFT_ENABLE_FSR31)
        const Fsr31RenderExtentResult renderExtent = queryFsr31RenderExtent(
            m_settings.upscale.quality,
            {static_cast<uint32_t>(displayWidth),
             static_cast<uint32_t>(displayHeight)});
        if (!renderExtent.succeeded()) {
            return std::nullopt;
        }
        return glm::ivec2(
            static_cast<int>(renderExtent.extent.width),
            static_cast<int>(renderExtent.extent.height));
#else
        return std::nullopt;
#endif
    }
    if (m_settings.upscale.type == TemporalUpscalerType::Dlss) {
#if defined(MECRAFT_ENABLE_STREAMLINE)
        const DlssRenderExtentResult renderExtent = queryDlssRenderExtent(
            m_settings.upscale.quality,
            {static_cast<uint32_t>(displayWidth),
             static_cast<uint32_t>(displayHeight)});
        if (!renderExtent.succeeded()) {
            return std::nullopt;
        }
        return glm::ivec2(
            static_cast<int>(renderExtent.extent.width),
            static_cast<int>(renderExtent.extent.height));
#else
        return std::nullopt;
#endif
    }
    if (!isFsr1RuntimeEnabled()) {
        return glm::ivec2(displayWidth, displayHeight);
    }
    const float scale = std::clamp(m_settings.upscale.fsr1RenderScale, 0.5f, 1.0f);
    return glm::ivec2(std::max(1, static_cast<int>(std::round(static_cast<float>(displayWidth) * scale))),
                      std::max(1, static_cast<int>(std::round(static_cast<float>(displayHeight) * scale))));
}

bool RenderScene::isFsr1RuntimeEnabled() const {
    return m_fsr1Supported && m_settings.upscale.fsr1Enabled &&
           m_settings.upscale.fsr1RenderScale < 0.999f &&
           m_settings.pipelineMode == PipelineMode::Deferred;
}

void RenderScene::invalidateFrameHistory() {
    m_hasPreviousContext = false;
    m_temporalFrameInput.reset();
    m_temporalUpscaleResult.reset();
    if (m_deferredPipeline) {
        m_deferredPipeline->invalidateHistory();
    }
    m_lastFrameOutput = {};
}

void RenderScene::refreshTemporalFrameInput() {
    m_temporalFrameInput.reset();
    if (!m_lastFrameOutput.hasDeferredInputs || m_shared.deferredTargets == nullptr ||
        isFsr1RuntimeEnabled()) {
        return;
    }

    TemporalFrameInput input;
    input.frameIndex = m_currentContext.frameIndex;
    input.renderExtent = m_currentContext.renderExtent;
    input.outputExtent = m_currentContext.outputExtent;
    input.jitter = m_currentContext.jitter;
    input.motionVectorScale = {
        static_cast<float>(m_currentContext.renderExtent.width),
        static_cast<float>(m_currentContext.renderExtent.height)
    };
    input.frameDeltaMilliseconds = m_currentContext.deltaTime * 1000.0f;
    input.preExposure = 1.0f;
    input.cameraNear = m_currentContext.camera.nearPlane;
    input.cameraFar = m_currentContext.camera.farPlane;
    input.verticalFovRadians = glm::radians(m_currentContext.camera.fovDegrees);
    input.cameraAspectRatio = static_cast<float>(m_currentContext.outputExtent.width) /
                              static_cast<float>(m_currentContext.outputExtent.height);
    input.cameraViewToClip = m_currentContext.camera.projection;
    input.clipToCameraView = glm::inverse(m_currentContext.camera.projection);
    input.clipToPrevClip = m_currentContext.previousViewProj *
                           glm::inverse(m_currentContext.camera.viewProj);
    input.prevClipToClip = glm::inverse(input.clipToPrevClip);
    const glm::mat4 inverseView = glm::inverse(m_currentContext.camera.view);
    input.cameraPosition = m_currentContext.camera.position;
    input.cameraRight = glm::normalize(glm::vec3(inverseView[0]));
    input.cameraUp = glm::normalize(glm::vec3(inverseView[1]));
    input.cameraForward = glm::normalize(-glm::vec3(inverseView[2]));
    input.depthInverted = false;
    input.reset = m_currentContext.temporalReset;
    input.textures.hdrColor = m_postProcessPass.sceneColorTextureHandle();
    input.textures.hdrColorView = m_postProcessPass.sceneColorTextureViewHandle();
    input.textures.depth = m_lastFrameOutput.gbufferDepth;
    input.textures.depthView = m_shared.deferredTargets->depthTextureViewHandle();
    input.textures.velocity = m_shared.deferredTargets->velocityTextureHandle();
    input.textures.velocityView = m_shared.deferredTargets->velocityTextureViewHandle();
    input.textures.exposure = m_postProcessPass.exposureTextureHandle();
    input.textures.exposureView = m_postProcessPass.exposureTextureViewHandle();
    input.textures.reactiveMask = m_lastFrameOutput.reactiveMask;
    input.textures.reactiveMaskView =
        m_shared.deferredTargets->reactiveMaskTextureViewHandle();
    input.textures.transparencyMask = m_lastFrameOutput.transparencyMask;
    input.textures.transparencyMaskView =
        m_shared.deferredTargets->transparencyMaskTextureViewHandle();
    if (m_settings.upscale.type == TemporalUpscalerType::Native) {
        input.textures.outputHdrColor = m_lastFrameOutput.sceneColor;
        input.textures.outputHdrColorView =
            m_postProcessPass.sceneColorTextureViewHandle();
    } else {
        input.textures.outputHdrColor = m_temporalUpscalePass.outputTextureHandle();
        input.textures.outputHdrColorView =
            m_temporalUpscalePass.outputTextureViewHandle();
    }
    m_temporalFrameInput = input;
}

float RenderScene::computeCameraRainVisibility(const IWorldView& worldView, const glm::vec3& cameraPos) const {
    constexpr float kOffsets[5][2] = {{0.0f, 0.0f}, {0.4f, 0.0f}, {-0.4f, 0.0f}, {0.0f, 0.4f}, {0.0f, -0.4f}};
    constexpr int kRayCount = 5;
    int skyHits = 0;
    const int startY = static_cast<int>(std::floor(cameraPos.y)) + 1;
    for (int r = 0; r < kRayCount; ++r) {
        const int bx = static_cast<int>(std::floor(cameraPos.x + kOffsets[r][0]));
        const int bz = static_cast<int>(std::floor(cameraPos.z + kOffsets[r][1]));
        bool blocked = false;
        for (int y = startY; y < 256; ++y) {
            const BlockStateId above = worldView.getBlock(bx, y, bz);
            if (above != NULL_BLOCK_STATE &&
                BlockRegistry::getOpacityFast(BlockStateRegistry::getBlockId(above)) > 0) {
                blocked = true;
                break;
            }
        }
        if (!blocked) ++skyHits;
    }
    return static_cast<float>(skyHits) / static_cast<float>(kRayCount);
}
