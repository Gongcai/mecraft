#include "RenderScene.h"
#include "../../Diagnostics.h"
#include "RenderResourceHub.h"
#include "SettingsMapper.h"
#include "ForwardPipeline.h"
#include "DeferredPipeline.h"
#include "../debug/RenderDebugLabels.h"
#include "../rhi/RhiCommandList.h"
#include "../rhi/RhiDevice.h"
#include "../rhi/RhiResources.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"
#include "../targets/DeferredRenderTargets.h"
#include "../renderers/BlockEntityRenderer.h"
#include "../renderers/FirstPersonHeldItemRenderer.h"
#include <glad/glad.h>
#include "engine/camera/Camera.h"
#include "../mesh/TerrainStreamingService.h"

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

RhiCommandList* beginSceneCaptureRendering(RhiDevice& rhiDevice,
                                           const FrameContext& ctx,
                                           const char* debugName) {
    if (!ctx.sceneCaptureColorView.isValid() || !ctx.sceneCaptureDepthView.isValid()) {
        return nullptr;
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
        static_cast<uint32_t>(std::max(1, ctx.frameWidth)),
        static_cast<uint32_t>(std::max(1, ctx.frameHeight))
    };
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.depthStencilAttachment = &depthAttachment;

    RhiCommandList& commandList = rhiDevice.beginFrame();
    commandList.beginRendering(renderingInfo);
    return &commandList;
}

void endSceneCaptureRendering(RhiDevice& rhiDevice, RhiCommandList* commandList) {
    if (commandList == nullptr) {
        return;
    }

    commandList->endRendering();
    rhiDevice.submitFrame(*commandList);
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
    // Phase 5: Initialize shared post-process pass
    m_postProcessPass.init(resourceMgr);
    m_fsr1Pass.init(resourceMgr);

    // Phase 9: Populate shared resources
    m_shared.resources = &resourceMgr;

    // Phase R4: Initialize terrain streaming service
    // Note: Thread pool initialization is deferred until setupResources() is called

    // Phase R5: Initialize overlay renderer
    m_overlayRenderer.init(resourceMgr);

    // Phase R6: Initialize debug service
    m_debugService.init();

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
    m_postProcessPass.shutdown();
}

void RenderScene::renderFrame(const IWorldView& worldView, const Camera& camera, const Window& window,
                              const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak,
                              const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem) {
    if (!prepareFrameResources(window)) {
        return;
    }

    m_debugService.beginFrame();
    m_terrainStreamingService.beginFrame();
    if (m_blockEntityRenderer != nullptr) {
        m_blockEntityRenderer->beginFrame();
    }

    // Build frame context
    m_currentContext = buildFrameContext(worldView, camera, window, dayNightSystem, weatherSystem);

    // Phase 9: Use active pipeline only if fully initialized and ready.
    // All shared resources must be populated AND pipeline must have been init'd.
    const bool newPipelineReady = m_activePipelineInitialized && isNewPipelineReady();

    // R7: New pipeline is the only path (legacy fallback removed)
    if (!newPipelineReady || !m_newPipelineActive) {
        // This should not happen if Game properly initializes the pipeline
        return;
    }

    // New pipeline path
    char frameLabel[64];
    std::snprintf(frameLabel, sizeof(frameLabel), "Frame %llu %s",
                  static_cast<unsigned long long>(m_currentContext.frameIndex),
                  m_activePipeline ? m_activePipeline->name() : "Unknown");
    renderer::debug::ScopedDebugGroup frameGroup(frameLabel);
    m_lastFrameOutput = m_activePipeline->renderFrame(m_currentContext, m_settings);

    // R5: Render block interaction overlays (outline + break overlay)
    const glm::mat4 viewProj = m_currentContext.camera.projection * m_currentContext.camera.view;
    RhiCommandList* overlayCommandList = beginSceneCaptureRendering(
        *m_shared.rhiDevice, m_currentContext, "SceneCapture.BlockOverlay");
    m_overlayRenderer.render(worldView, viewProj, target, blockBreak);
    endSceneCaptureRendering(*m_shared.rhiDevice, overlayCommandList);
}

void RenderScene::renderGameplayFrame(const RenderGameplayFrameRequest& request) {
    // Activate the pipeline when shared resources become available after target initialization.
    if (!isNewPipelineActive() && isNewPipelineReady()) {
        setNewPipelineActive(true);
    }

    const bool skipPostProcess = getPipelineMode() == PipelineMode::Forward;
    const glm::ivec2 frameRenderSize = skipPostProcess
        ? glm::ivec2(std::max(1, request.window.getWidth()), std::max(1, request.window.getHeight()))
        : internalRenderSize(request.window);
    if (!skipPostProcess &&
        !m_postProcessPass.beginSceneCapture(*m_shared.rhiDevice,
                                             frameRenderSize.x,
                                             frameRenderSize.y)) {
        MECRAFT_LOG_STREAM(std::cerr << "[RenderScene] Failed to begin post-process scene capture\n");
        m_terrainStreamingService.endFrame();
        return;
    }

    const bool lightDebugActive = isLightDebugActive();
    float cameraRainVisibility = 1.0f;

    renderFrame(request.worldView, request.camera, request.window,
                request.target, request.blockBreak,
                request.dayNightSystem, request.weatherSystem);

    if (!lightDebugActive) {
        cameraRainVisibility = m_currentContext.cameraRainVisibility;
        if (m_settings.weather.rainLinesEnabled) {
            const auto& weather = request.weatherSystem.getDerived();
            RhiCommandList* weatherCommandList = nullptr;
            if (weather.rainStrength > 0.01f || weather.snowStrength > 0.01f) {
                weatherCommandList = beginSceneCaptureRendering(
                    *m_shared.rhiDevice, m_currentContext, "SceneCapture.Weather");
            }
            const glm::vec3 camPos = request.camera.getPosition();
            const float frameAspect = static_cast<float>(frameRenderSize.x) /
                                      static_cast<float>(std::max(1, frameRenderSize.y));
            auto projMat = request.camera.getProjectionMatrix(frameAspect);
            auto viewMat = request.camera.getViewMatrix();
            const float alphaScale = m_settings.weather.rainAlphaScale;
            const bool forwardVanillaActive = isNewPipelineActive() &&
                                               getPipelineMode() == PipelineMode::Forward;
            const GLuint depthTex = forwardVanillaActive
                ? 0
                : static_cast<GLuint>(renderer::rhi::gl::textureId(m_lastFrameOutput.gbufferDepth));
            const bool hardwareDepthTest = !isNewPipelineActive() || forwardVanillaActive;
            const glm::vec2 precipitationScreenSize(
                static_cast<float>(frameRenderSize.x),
                static_cast<float>(frameRenderSize.y));

            if (weather.rainStrength > 0.01f) {
                request.rainRenderer.render(projMat, viewMat, camPos,
                                             weather.rainStrength, cameraRainVisibility,
                                             alphaScale, depthTex,
                                             precipitationScreenSize, request.frameTime,
                                             hardwareDepthTest);
            }
            if (weather.snowStrength > 0.01f) {
                request.rainRenderer.renderSnow(projMat, viewMat, camPos,
                                                weather.snowStrength, cameraRainVisibility,
                                                alphaScale * 0.6f, depthTex,
                                                precipitationScreenSize, request.frameTime,
                                                hardwareDepthTest);
            }
            endSceneCaptureRendering(*m_shared.rhiDevice, weatherCommandList);
        }
    }

    if (request.renderFirstPersonHeldItem &&
        request.firstPersonHeldItemRenderer != nullptr &&
        request.firstPersonInventory != nullptr &&
        request.firstPersonHeldItemMotion != nullptr) {
        request.firstPersonHeldItemRenderer->setForwardMode(getPipelineMode() == PipelineMode::Forward);
        request.firstPersonHeldItemRenderer->setShadowData(
            FirstPersonHeldItemRenderer::fromFirstPersonShadowData(getHeldItemShadowData()));
        const glm::vec2 heldLight = sampleHeldItemLight(request.worldView, request.camera.getPosition());
        request.firstPersonHeldItemRenderer->setEnvironmentLight(heldLight.x, heldLight.y);
        request.firstPersonHeldItemRenderer->setSceneHdrScale(
            computeHeldItemSceneHdrScale(m_currentContext, m_settings, getPipelineMode()));
        RhiCommandList* heldItemCommandList = beginSceneCaptureRendering(
            *m_shared.rhiDevice, m_currentContext, "SceneCapture.FirstPersonHeldItem");
        request.firstPersonHeldItemRenderer->render(
            frameRenderSize.x,
            frameRenderSize.y,
            *request.firstPersonInventory,
            *request.firstPersonHeldItemMotion,
            static_cast<float>(Time::getGameTime()));
        endSceneCaptureRendering(*m_shared.rhiDevice, heldItemCommandList);
    }

    if (!skipPostProcess) {
        const bool postTimerStarted = m_debugService.beginGpuTimer(GpuTimerPass::Post);
        PostProcessEffects effects = buildPostProcessEffects(
            request.worldView, request.camera, request.window,
            cameraRainVisibility, request.screenRollRadians,
            request.dayNightSystem, request.weatherSystem);
        m_postProcessPass.setFrameEffects(effects);
        if (lightDebugActive) {
            m_postProcessPass.blitSceneCaptureToBackbuffer(*m_shared.rhiDevice,
                                                           m_currentContext.swapchainColorView,
                                                           request.window);
        } else {
            const bool fsrEnabled = m_settings.upscale.fsr1Enabled &&
                                    m_settings.upscale.renderScale < 0.999f;
            if (fsrEnabled) {
                const RhiTextureHandle postTexture = m_postProcessPass.compositeToTexture(
                    *m_shared.rhiDevice,
                    request.window,
                    request.frameTime,
                    m_lastFrameOutput.gbufferDepth);
                bool upscaled = false;
                if (postTexture.isValid()) {
                    const int inputWidth = m_postProcessPass.targetWidth();
                    const int inputHeight = m_postProcessPass.targetHeight();
                    upscaled = m_fsr1Pass.execute(
                        *m_shared.rhiDevice,
                        m_currentContext.swapchainColorView,
                        m_postProcessPass.compositeTextureViewHandle(),
                        inputWidth,
                        inputHeight,
                        std::max(1, request.window.getWidth()),
                        std::max(1, request.window.getHeight()),
                        m_settings.upscale.sharpness);
                }
                if (!upscaled && postTexture.isValid()) {
                    m_postProcessPass.blitCompositeToBackbuffer(*m_shared.rhiDevice,
                                                                m_currentContext.swapchainColorView,
                                                                request.window);
                } else if (!upscaled) {
                    m_postProcessPass.compositeToBackbuffer(
                        *m_shared.rhiDevice,
                        m_currentContext.swapchainColorView,
                        m_currentContext.swapchainColorFormat,
                        request.window,
                        request.frameTime,
                        m_lastFrameOutput.gbufferDepth);
                }
            } else {
                m_postProcessPass.compositeToBackbuffer(
                    *m_shared.rhiDevice,
                    m_currentContext.swapchainColorView,
                    m_currentContext.swapchainColorFormat,
                    request.window,
                    request.frameTime,
                    m_lastFrameOutput.gbufferDepth);
            }
        }
        if (postTimerStarted) {
            m_debugService.endGpuTimer(GpuTimerPass::Post);
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
    // Detect pipeline mode change and trigger switch
    if (settings.pipelineMode != m_settings.pipelineMode) {
        setPipelineMode(settings.pipelineMode);
    }

    const bool upscaleChanged =
        settings.upscale.fsr1Enabled != m_settings.upscale.fsr1Enabled ||
        std::abs(settings.upscale.renderScale - m_settings.upscale.renderScale) > 0.0001f;

    m_settings = settings;

    if (upscaleChanged) {
        invalidateFrameHistory();
    }

    if (m_settingsChangedCallback) {
        m_settingsChangedCallback(m_settings);
    }
}

const RenderSettings& RenderScene::getSettings() const {
    return m_settings;
}

void RenderScene::setSettingsChangedCallback(std::function<void(const RenderSettings&)> callback) {
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

bool RenderScene::prepareFrameResources(const Window& window) {
    if (!m_activePipeline || !m_activePipeline->supportsDeferred() || m_shared.deferredTargets == nullptr) {
        return true;
    }

    DeferredRenderTargets& targets = *m_shared.deferredTargets;
    if (!targets.init()) {
        return false;
    }

    const glm::ivec2 internalSize = internalRenderSize(window);
    return targets.ensureSize(internalSize.x, internalSize.y, m_settings.shadow.resolution);
}

PostProcessEffects RenderScene::buildPostProcessEffects(const IWorldView& worldView, const Camera& camera,
                                                         const Window& window, float cameraRainVisibility,
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

    // Sun screen position calculation
    {
        const float sunAngle = dayNightSystem.getCelestialAngleRadians();
        glm::vec3 sunDirection(0.25f, std::sin(sunAngle), -std::cos(sunAngle));
        if (glm::length(sunDirection) > 0.0001f) {
            sunDirection = glm::normalize(sunDirection);
        } else {
            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        const glm::mat4 viewProj = camera.getProjectionMatrix(window.getAspectRatio()) * camera.getViewMatrix();
        const glm::vec4 clip = viewProj * glm::vec4(camera.getPosition() + sunDirection * 256.0f, 1.0f);
        if (clip.w > 0.0001f) {
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            effects.sunScreenPos = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
            const float onScreenX = 1.0f - std::clamp(std::abs(effects.sunScreenPos.x - 0.5f) * 2.0f, 0.0f, 1.0f);
            const float onScreenY = 1.0f - std::clamp(std::abs(effects.sunScreenPos.y - 0.5f) * 2.0f, 0.0f, 1.0f);
            const float horizonFade = std::clamp((sunDirection.y + 0.05f) / 0.45f, 0.0f, 1.0f);
            effects.sunVisibility = std::clamp(onScreenX * onScreenY * horizonFade, 0.0f, 1.0f);
        }
    }

    return effects;
}

FrameContext RenderScene::buildFrameContext(const IWorldView& worldView, const Camera& camera, const Window& window,
                                            const DayNightSystem& dayNightSystem, const WeatherSystem& weatherSystem) {
    FrameContext ctx;

    // Camera matrices
    ctx.camera.view = camera.getViewMatrix();
    ctx.camera.projection = camera.getProjectionMatrix(window.getAspectRatio());
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

    // Internal scene dimensions. UI and final presentation still use the real window size.
    const glm::ivec2 internalSize = internalRenderSize(window);
    ctx.frameWidth = internalSize.x;
    ctx.frameHeight = internalSize.y;
    ctx.swapchainColorView = m_shared.rhiDevice->currentSwapchainColorView();
    ctx.swapchainDepthStencilView = m_shared.rhiDevice->currentSwapchainDepthStencilView();
    ctx.swapchainColorFormat = m_shared.rhiDevice->swapchainColorFormat();
    ctx.swapchainDepthStencilFormat = m_shared.rhiDevice->swapchainDepthStencilFormat();
    ctx.sceneCaptureColorTexture = m_postProcessPass.sceneColorTextureHandle();
    ctx.sceneCaptureDepthTexture = m_postProcessPass.sceneDepthTextureHandle();
    ctx.sceneCaptureColorView = m_postProcessPass.sceneColorTextureViewHandle();
    ctx.sceneCaptureDepthView = m_postProcessPass.sceneDepthTextureViewHandle();

    // Frame timing
    ctx.frameIndex = m_frameCounter++;
    ctx.deltaTime = static_cast<float>(Time::deltaTime);
    const double gameTime = Time::getGameTime();
    const double visualTime = Time::getRawTime();
    ctx.animationTime = static_cast<float>(std::fmod(gameTime, 16.0));
    ctx.shaderTime = static_cast<float>(std::fmod(visualTime, 8192.0));

    // TAA jitter (DerivativeMain shaders.properties)
    if (m_shared.deferredTargets) {
        const float invW = 1.0f / static_cast<float>(std::max(1, m_shared.deferredTargets->width()));
        const float invH = 1.0f / static_cast<float>(std::max(1, m_shared.deferredTargets->height()));
        const float frameCounter = static_cast<float>(ctx.frameIndex);
        const float frameX = glm::fract(frameCounter / 1.3247179572f + 0.5f) * 2.0f - 1.0f;
        const float frameY = glm::fract(frameCounter / 1.7548776662f + 0.5f) * 2.0f - 1.0f;
        ctx.jitter.x = frameX * invW;
        ctx.jitter.y = frameY * invH;
    }

    // Jittered projection matrix
    {
        glm::mat4 jitteredProj = ctx.camera.projection;
        for (int column = 0; column < 4; ++column) {
            jitteredProj[column][0] += ctx.jitter.x * ctx.camera.projection[column][3];
            jitteredProj[column][1] += ctx.jitter.y * ctx.camera.projection[column][3];
        }
        ctx.camera.jitteredViewProj = jitteredProj * ctx.camera.view;
        ctx.camera.jitteredInvViewProj = glm::inverse(ctx.camera.jitteredViewProj);
    }

    // Previous frame data (temporal)
    if (m_hasPreviousContext) {
        ctx.prevCamera = m_previousContext.camera;
        ctx.prevJitter = m_previousContext.jitter;
        ctx.previousViewProj = m_previousContext.camera.viewProj;
        ctx.previousInvViewProj = m_previousContext.camera.invViewProj;
        ctx.previousJitteredViewProj = m_previousContext.camera.jitteredViewProj;
        ctx.hasPreviousFrame = true;
    } else {
        ctx.prevCamera = ctx.camera;
        ctx.prevJitter = ctx.jitter;
        ctx.previousViewProj = ctx.camera.viewProj;
        ctx.previousInvViewProj = ctx.camera.invViewProj;
        ctx.previousJitteredViewProj = ctx.camera.jitteredViewProj;
        ctx.hasPreviousFrame = false;
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

glm::ivec2 RenderScene::internalRenderSize(const Window& window) const {
    const int displayWidth = std::max(1, window.getWidth());
    const int displayHeight = std::max(1, window.getHeight());
    if (!m_settings.upscale.fsr1Enabled || m_settings.upscale.renderScale >= 0.999f ||
        m_settings.pipelineMode != PipelineMode::Deferred) {
        return glm::ivec2(displayWidth, displayHeight);
    }
    const float scale = std::clamp(m_settings.upscale.renderScale, 0.5f, 1.0f);
    return glm::ivec2(std::max(1, static_cast<int>(std::round(static_cast<float>(displayWidth) * scale))),
                      std::max(1, static_cast<int>(std::round(static_cast<float>(displayHeight) * scale))));
}

void RenderScene::invalidateFrameHistory() {
    m_hasPreviousContext = false;
    if (m_deferredPipeline) {
        m_deferredPipeline->invalidateHistory();
    }
    m_lastFrameOutput = {};
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
