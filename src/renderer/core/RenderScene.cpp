#include "RenderScene.h"
#include "Renderer.h"
#include "ForwardPipeline.h"
#include "DeferredPipeline.h"
#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../world/World.h"
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
} // anonymous namespace

RenderScene::RenderScene() = default;
RenderScene::~RenderScene() = default;

void RenderScene::init(ResourceMgr& resourceMgr) {
    // Phase 5: Initialize shared post-process pass
    m_postProcessPass.init(resourceMgr);

    // Phase 9: Populate shared resources
    m_shared.resources = &resourceMgr;

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
    // (terrain, targets, sky). This happens when setLegacyRenderer() is called
    // and the Renderer exposes its resources.
}

void RenderScene::shutdown() {
    // Phase 9: Shutdown pipelines
    if (m_activePipeline) {
        m_activePipeline->shutdown();
        m_activePipeline = nullptr;
    }
    m_forwardPipeline.reset();
    m_deferredPipeline.reset();

    // Phase 5: Shutdown shared post-process pass
    m_postProcessPass.shutdown();
}

void RenderScene::renderFrame(const World& world, const Camera& camera, const Window& window,
                              const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak) {
    // Build frame context
    m_currentContext = buildFrameContext(world, camera, window);

    // Phase 9: Use active pipeline only if fully initialized and ready.
    // All shared resources must be populated AND pipeline must have been init'd.
    const bool newPipelineReady = m_activePipeline &&
                                  m_shared.terrain &&
                                  m_shared.commonTargets &&
                                  m_shared.sky &&
                                  m_shared.resources;

    if (newPipelineReady && m_newPipelineActive) {
        // New pipeline path (Phase 10+ will fully implement this)
        m_lastFrameOutput = m_activePipeline->renderFrame(m_currentContext);

        // Post-process is handled by RenderScene for both pipelines
        // if (!m_lastFrameOutput.skipPostProcess) {
        //     m_postProcessPass.execute(m_currentContext, m_lastFrameOutput);
        // }
    } else if (m_legacyRenderer) {
        // Legacy path (Phase 1-8 compatibility)
        // This is the active rendering path until Phase 10 migrates orchestration.
        m_legacyRenderer->render(world, camera, window, target, blockBreak);

        // Update frame output from legacy state (bridge)
        m_lastFrameOutput.hasDeferredInputs = m_legacyRenderer->isDeferredFrameActive();
        m_lastFrameOutput.gbufferDepthTex = m_legacyRenderer->gbufDepthTexture();
        m_lastFrameOutput.weatherMaskTex = m_legacyRenderer->weatherMaskTexture();

        // Convert HeldItemShadowData to FirstPersonShadowData (field-by-field copy)
        auto legacyShadow = m_legacyRenderer->getHeldItemShadowData();
        for (int i = 0; i < 4; ++i) {
            m_lastFrameOutput.heldItemShadow.cascadeViewProj[i] = legacyShadow.cascadeViewProj[i];
            m_lastFrameOutput.heldItemShadow.cascadeSplitFar[i] = legacyShadow.cascadeSplitFar[i];
            m_lastFrameOutput.heldItemShadow.cascadeTexelWorldSize[i] = legacyShadow.cascadeTexelWorldSize[i];
        }
        m_lastFrameOutput.heldItemShadow.shadowTexture = legacyShadow.shadowTexture;
        m_lastFrameOutput.heldItemShadow.shadowDepthRaw = legacyShadow.shadowDepthRaw;
        m_lastFrameOutput.heldItemShadow.shadowDepthAll = legacyShadow.shadowDepthAll;
        m_lastFrameOutput.heldItemShadow.shadowDepthAllRaw = legacyShadow.shadowDepthAllRaw;
        m_lastFrameOutput.heldItemShadow.shadowColor0 = legacyShadow.shadowColor0;
        m_lastFrameOutput.heldItemShadow.shadowColor1 = legacyShadow.shadowColor1;
        m_lastFrameOutput.heldItemShadow.cameraPos = legacyShadow.cameraPos;
        m_lastFrameOutput.heldItemShadow.sunDirection = legacyShadow.sunDirection;
        m_lastFrameOutput.heldItemShadow.shadowDistance = legacyShadow.shadowDistance;
        m_lastFrameOutput.heldItemShadow.constantBias = legacyShadow.constantBias;
        m_lastFrameOutput.heldItemShadow.slopeBias = legacyShadow.slopeBias;
        m_lastFrameOutput.heldItemShadow.normalOffset = legacyShadow.normalOffset;
        m_lastFrameOutput.heldItemShadow.softness = legacyShadow.softness;
        m_lastFrameOutput.heldItemShadow.pcssStrength = legacyShadow.pcssStrength;
        m_lastFrameOutput.heldItemShadow.cascadeCount = legacyShadow.cascadeCount;
        m_lastFrameOutput.heldItemShadow.softShadowsEnabled = legacyShadow.softShadowsEnabled;
        m_lastFrameOutput.heldItemShadow.pcssShadowsEnabled = legacyShadow.pcssShadowsEnabled;
        m_lastFrameOutput.heldItemShadow.shadowsEnabled = legacyShadow.shadowsEnabled;
        m_lastFrameOutput.heldItemShadow.skyIntensity = legacyShadow.skyIntensity;
    }
}

void RenderScene::setPipelineMode(PipelineMode mode) {
    if (m_settings.pipelineMode == mode) return;

    m_settings.pipelineMode = mode;
    invalidateFrameHistory();

    // Phase 9: Switch active pipeline
    if (m_forwardPipeline && m_deferredPipeline) {
        // Shutdown current pipeline
        if (m_activePipeline) {
            m_activePipeline->shutdown();
        }

        // Switch to new pipeline
        m_activePipeline = (mode == PipelineMode::Deferred)
            ? static_cast<RenderPipeline*>(m_deferredPipeline.get())
            : static_cast<RenderPipeline*>(m_forwardPipeline.get());

        // Initialize new pipeline with shared resources
        m_activePipeline->init(m_shared);
    }

    // Phase 1: Also update legacy renderer settings for compatibility
    if (m_legacyRenderer) {
        auto legacySettings = m_legacyRenderer->getRenderPipelineSettings();
        legacySettings.mode = (mode == PipelineMode::Deferred)
            ? Renderer::RenderPipelineMode::HybridDeferred
            : Renderer::RenderPipelineMode::ForwardLegacy;
        m_legacyRenderer->setRenderPipelineSettings(legacySettings);
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
    // Phase 9: Detect pipeline mode change and trigger switch
    if (settings.pipelineMode != m_settings.pipelineMode) {
        setPipelineMode(settings.pipelineMode);
    }

    m_settings = settings;

    // Phase 1: Convert and apply to legacy renderer
    // In later phases, settings will be passed through FrameContext
    if (m_legacyRenderer) {
        Renderer::RenderPipelineSettings legacy;
        legacy.mode = (settings.pipelineMode == PipelineMode::Deferred)
            ? Renderer::RenderPipelineMode::HybridDeferred
            : Renderer::RenderPipelineMode::ForwardLegacy;

        // Shadow
        legacy.shadowsEnabled = settings.shadow.enabled;
        legacy.softShadowsEnabled = settings.shadow.softShadowsEnabled;
        legacy.pcssShadowsEnabled = settings.shadow.pcssShadowsEnabled;
        legacy.contactShadowsEnabled = settings.shadow.contactShadowsEnabled;
        legacy.cloudShadowsEnabled = settings.shadow.cloudShadowsEnabled;
        legacy.shadowResolution = settings.shadow.resolution;
        legacy.shadowDistance = settings.shadow.distance;
        legacy.shadowSoftness = settings.shadow.softness;
        legacy.shadowPcssStrength = settings.shadow.pcssStrength;
        legacy.shadowConstantBias = settings.shadow.constantBias;
        legacy.shadowSlopeBias = settings.shadow.slopeBias;
        legacy.shadowNormalOffset = settings.shadow.normalOffset;
        legacy.contactShadowStrength = settings.shadow.contactShadowStrength;
        legacy.cloudShadowStrength = settings.shadow.cloudShadowStrength;
        legacy.cloudShadowScale = settings.shadow.cloudShadowScale;
        legacy.cloudShadowSpeed = settings.shadow.cloudShadowSpeed;

        // SSAO
        legacy.ssaoEnabled = settings.ssao.enabled;
        legacy.ssaoFilterEnabled = settings.ssao.filterEnabled;
        legacy.ssaoTemporalEnabled = settings.ssao.temporalEnabled;
        legacy.ssaoHistoryWeight = settings.ssao.historyWeight;
        legacy.ssaoRadius = settings.ssao.radius;
        legacy.ssaoStrength = settings.ssao.strength;
        legacy.ssaoSamples = settings.ssao.samples;

        // Volumetric
        legacy.volumetricLightEnabled = settings.volumetric.lightEnabled;
        legacy.uwVolumetricLightEnabled = settings.volumetric.uwLightEnabled;
        legacy.volumetricFogEnabled = settings.volumetric.fogEnabled;
        legacy.volumetricSkyRayEnabled = settings.volumetric.skyRayEnabled;
        legacy.volumetricTimeFadeEnabled = settings.volumetric.timeFadeEnabled;
        legacy.volumetricTemporalEnabled = settings.volumetric.temporalEnabled;
        legacy.volumetricQualityTier = settings.volumetric.qualityTier;
        legacy.volumetricFogSamples = settings.volumetric.fogSamples;
        legacy.volumetricTemporalWeight = settings.volumetric.temporalWeight;
        legacy.volumetricShadowBiasScale = settings.volumetric.shadowBiasScale;
        legacy.volumetricFogStrength = settings.volumetric.fogStrength;
        legacy.underwaterVolumetricLightStrength = settings.volumetric.underwaterLightStrength;
        legacy.vfogCenterHeight = settings.volumetric.fogCenterHeight;
        legacy.vfogHeightSpread = settings.volumetric.fogHeightSpread;
        legacy.vfogNoiseScale = settings.volumetric.fogNoiseScale;
        legacy.vfogLightStrength = settings.volumetric.fogLightStrength;
        legacy.vfogDensityScale = settings.volumetric.fogDensityScale;
        legacy.freezeR1 = settings.volumetric.freezeR1;
        legacy.freezeBias = settings.volumetric.freezeBias;

        // Cloud
        legacy.cloudTimeScale = settings.cloud.timeScale;
        legacy.sceneCloudCompositeStrength = settings.cloud.sceneCloudCompositeStrength;

        // Reflection
        legacy.reflectionFilterEnabled = settings.reflection.filterEnabled;
        legacy.reflectionTemporalEnabled = settings.reflection.temporalEnabled;
        legacy.reflectionHistoryWeight = settings.reflection.historyWeight;
        legacy.reflectionFilterStrength = settings.reflection.filterStrength;
        legacy.sceneReflectionCompositeStrength = settings.reflection.sceneReflectionCompositeStrength;

        // TAA
        legacy.taaEnabled = settings.taa.enabled;
        legacy.taaBlendMin = settings.taa.blendMin;
        legacy.taaBlendMax = settings.taa.blendMax;
        legacy.forceZeroVelocity = settings.taa.forceZeroVelocity;
        legacy.freezeTaaJitter = settings.taa.freezeJitter;

        // Post-process
        legacy.bloomEnabled = settings.postProcess.bloomEnabled;
        legacy.bloomThreshold = settings.postProcess.bloomThreshold;
        legacy.bloomStrength = settings.postProcess.bloomStrength;
        legacy.bloomyFogEnabled = settings.postProcess.bloomyFogEnabled;
        legacy.autoExposureEnabled = settings.postProcess.autoExposureEnabled;
        legacy.autoExposureMin = settings.postProcess.autoExposureMin;
        legacy.autoExposureMax = settings.postProcess.autoExposureMax;
        legacy.autoExposureSpeed = settings.postProcess.autoExposureSpeed;
        legacy.autoExposureBias = settings.postProcess.autoExposureBias;
        legacy.exposure = settings.postProcess.exposure;
        legacy.tonemapMode = settings.postProcess.tonemapMode;
        legacy.gamma = settings.postProcess.gamma;
        legacy.saturation = settings.postProcess.saturation;
        legacy.contrast = settings.postProcess.contrast;
        legacy.colorTemperature = settings.postProcess.colorTemperature;
        legacy.vibrance = settings.postProcess.vibrance;
        legacy.highlightCompression = settings.postProcess.highlightCompression;
        legacy.filmEmulationStrength = settings.postProcess.filmEmulationStrength;
        legacy.redModifierStrength = settings.postProcess.redModifierStrength;
        legacy.colorLumaR = settings.postProcess.colorLumaR;
        legacy.colorLumaG = settings.postProcess.colorLumaG;
        legacy.colorLumaB = settings.postProcess.colorLumaB;
        legacy.albedoDesaturation = settings.postProcess.albedoDesaturation;
        legacy.splitToneStrength = settings.postProcess.splitToneStrength;
        legacy.vignetteStrength = settings.postProcess.vignetteStrength;
        legacy.sunWarmth = settings.postProcess.sunWarmth;
        legacy.skyCoolness = settings.postProcess.skyCoolness;
        legacy.shadowDesaturation = settings.postProcess.shadowDesaturation;
        legacy.shadowTintStrength = settings.postProcess.shadowTintStrength;
        legacy.directSunStrength = settings.postProcess.directSunStrength;
        legacy.skyAmbientStrength = settings.postProcess.skyAmbientStrength;
        legacy.minimumAmbient = settings.postProcess.minimumAmbient;
        legacy.shadowMinLight = settings.postProcess.shadowMinLight;
        legacy.shadowContrast = settings.postProcess.shadowContrast;
        legacy.blockLightStrength = settings.postProcess.blockLightStrength;
        legacy.fakeBounceStrength = settings.postProcess.fakeBounceStrength;
        legacy.aerialStrength = settings.postProcess.aerialStrength;
        legacy.horizonScatterStrength = settings.postProcess.horizonScatterStrength;
        legacy.sharpenStrength = settings.postProcess.sharpenStrength;
        legacy.noiseDitherStrength = settings.postProcess.noiseDitherStrength;
        legacy.purkinjeShiftEnabled = settings.postProcess.purkinjeShiftEnabled;
        legacy.sunRaysEnabled = settings.postProcess.sunRaysEnabled;
        legacy.sunRayStrength = settings.postProcess.sunRayStrength;
        legacy.motionBlurEnabled = settings.postProcess.motionBlurEnabled;
        legacy.motionBlurStrength = settings.postProcess.motionBlurStrength;
        legacy.motionBlurSamples = settings.postProcess.motionBlurSamples;
        legacy.dofEnabled = settings.postProcess.dofEnabled;
        legacy.dofIntensity = settings.postProcess.dofIntensity;
        legacy.dofAperture = settings.postProcess.dofAperture;
        legacy.dofFocusDistance = settings.postProcess.dofFocusDistance;

        // Debug
        legacy.debugViewMode = settings.debug.viewMode;
        legacy.deferredLightDebugMode = settings.debug.lightDebugMode;
        legacy.postprocessDebugMode = settings.debug.postprocessDebugMode;
        legacy.reflectionDebugMode = settings.debug.reflectionDebugMode;
        legacy.derivativeStrictMode = settings.debug.derivativeStrictMode;
        legacy.debugDisableGreedyMeshing = settings.debug.disableGreedyMeshing;

        // Weather
        legacy.weatherSkylightScale = settings.weather.skylightScale;
        legacy.weatherExposureBias = settings.weather.exposureBias;
        legacy.weatherPostRainFog = settings.weather.postRainFog;
        legacy.weatherRainAlphaScale = settings.weather.rainAlphaScale;
        legacy.weatherRainLinesEnabled = settings.weather.rainLinesEnabled;
        legacy.sceneParticlesEnabled = settings.weather.particlesEnabled;
        legacy.rainWetSurfacesEnabled = settings.weather.wetSurfacesEnabled;
        legacy.rainSurfaceRipplesEnabled = settings.weather.surfaceRipplesEnabled;
        legacy.directWeatherOcclusion = settings.weather.directWeatherOcclusion;

        m_legacyRenderer->setRenderPipelineSettings(legacy);
    }
}

const RenderSettings& RenderScene::getSettings() const {
    return m_settings;
}

void RenderScene::setHumanoidRenderer(HumanoidRenderer* hr) {
    m_humanoidRenderer = hr;
    m_shared.humanoidRenderer = hr;
    if (m_legacyRenderer) {
        m_legacyRenderer->setHumanoidRenderer(hr);
    }
}

void RenderScene::setDropRenderer(DropRenderer* dr) {
    m_dropRenderer = dr;
    m_shared.dropRenderer = dr;
    if (m_legacyRenderer) {
        m_legacyRenderer->setDropRenderer(dr);
    }
}

void RenderScene::setParticleSystem(ParticleSystem* ps) {
    m_particleSystem = ps;
    m_shared.particleSystem = ps;
    if (m_legacyRenderer) {
        m_legacyRenderer->setParticleSystem(ps);
    }
}

void RenderScene::setDropSystem(DropSystem* ds) {
    m_dropSystem = ds;
    m_shared.dropSystem = ds;
    if (m_legacyRenderer) {
        m_legacyRenderer->setDropSystem(ds);
    }
}

void RenderScene::setGameplayRegistry(ecs::GameplayRegistry* reg) {
    m_gameplayRegistry = reg;
    if (m_legacyRenderer) {
        m_legacyRenderer->setGameplayRegistry(reg);
    }
}

const FrameOutput& RenderScene::getLastFrameOutput() const {
    return m_lastFrameOutput;
}

void RenderScene::setLegacyRenderer(Renderer* renderer) {
    m_legacyRenderer = renderer;

    // Phase 9/10: Populate shared resources from legacy renderer
    if (renderer) {
        m_shared.terrainCache = &renderer->getTerrainRenderCache();
        m_shared.terrain = &renderer->getTerrainRenderer();
        m_shared.worldRenderBuffer = &renderer->getWorldRenderBuffer();
        m_shared.meshingService = &renderer->getChunkMeshingService();
        m_shared.deferredTargets = &renderer->getDeferredRenderTargets();
        m_shared.sky = &renderer->getGameplaySkyRenderer();
        m_shared.shadowRenderer = &renderer->getShadowRenderer();
        m_shared.threadPool = renderer->getThreadPool();
        // Note: commonTargets will be added when CommonFrameTargets is fully integrated
    }
}

FrameContext RenderScene::buildFrameContext(const World& world, const Camera& camera, const Window& window) {
    FrameContext ctx;

    // Camera matrices
    ctx.camera.view = camera.getViewMatrix();
    ctx.camera.projection = camera.getProjectionMatrix(window.getAspectRatio());
    ctx.camera.viewProj = ctx.camera.projection * ctx.camera.view;
    ctx.camera.invViewProj = glm::inverse(ctx.camera.viewProj);
    ctx.camera.position = camera.getPosition();
    ctx.camera.nearPlane = camera.getNear();
    ctx.camera.farPlane = camera.getFar();

    // Screen dimensions
    ctx.frameWidth = window.getWidth();
    ctx.frameHeight = window.getHeight();

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

    // Weather state from World::WeatherSystem
    const WeatherState& weather = world.getWeatherSystem().getRenderState();
    const WeatherDerived& weatherDerived = world.getWeatherSystem().getDerived();
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
        auto skyColors = m_shared.sky->computeSkyColors(world.getDayNightSystem());
        ctx.skyColors = toSkyColorsData(skyColors);
        ctx.skyIlluminance = toSkyIlluminanceData(
            m_shared.sky->computeSkyIlluminance(skyColors, ctx.weather.wetness, ctx.weather.storm));
        ctx.skyIntensity = world.getDayNightSystem().getSkyIntensity();
    }

    // Fog settings
    ctx.fog.enabled = m_settings.fog.enabled;
    ctx.fog.color = m_settings.fog.color;
    ctx.fog.startDistance = m_settings.fog.startDistance;
    ctx.fog.endDistance = m_settings.fog.endDistance;
    ctx.fog.density = m_settings.fog.density;
    if (m_settings.fog.autoDistanceByRenderDistance) {
        const float chunkSize = 16.0f; // Chunk::SIZE_X
        const float renderDistanceChunks = static_cast<float>(std::max(1, world.getRenderDistance()));
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
    ctx.cloud.coverage = std::clamp(1.0f + cloudWetForCoverage * 0.2f, 0.0f, 1.5f);
    ctx.cloud.density = 0.85f + ctx.weather.wetness * 0.35f + ctx.weather.storm * 0.55f;
    float cloudWet = std::clamp(ctx.weather.cloudWetness, 0.0f, 1.0f);
    ctx.cloud.height = 1000.0f + cloudWet * (800.0f - 1000.0f);
    ctx.cloud.thickness = 1400.0f + cloudWet * (3000.0f - 1400.0f);

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

    // Shared resources and world pointer
    ctx.shared = &m_shared;
    ctx.world = &world;

    // Store current context as previous for next frame
    m_previousContext = ctx;
    m_hasPreviousContext = true;

    return ctx;
}

void RenderScene::invalidateFrameHistory() {
    // Phase 1: Notify legacy renderer to invalidate temporal resources
    // In later phases, this will reset TAA history, SSAO history, etc.
}
