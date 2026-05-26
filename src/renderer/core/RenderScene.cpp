#include "RenderScene.h"
#include "Renderer.h"
#include "engine/camera/Camera.h"
#include "engine/platform/Window.h"
#include "../../world/World.h"

RenderScene::RenderScene() = default;
RenderScene::~RenderScene() = default;

void RenderScene::init(ResourceMgr& resourceMgr) {
    // Phase 5: Initialize shared post-process pass
    m_postProcessPass.init(resourceMgr);
}

void RenderScene::shutdown() {
    // Phase 5: Shutdown shared post-process pass
    m_postProcessPass.shutdown();
}

void RenderScene::renderFrame(const World& world, const Camera& camera, const Window& window,
                              const BlockTargetRenderData& target, const BlockBreakRenderData& blockBreak) {
    // Phase 1: Delegate to legacy Renderer
    // In later phases, this will orchestrate the active pipeline

    if (m_legacyRenderer) {
        // Build frame context for future use
        m_currentContext = buildFrameContext(world, camera, window);

        // Delegate to legacy renderer
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

    // Phase 1: Update legacy renderer settings
    // In later phases, this will switch active pipeline
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
    switch (m_settings.pipelineMode) {
        case PipelineMode::Forward: return "Forward (Vanilla)";
        case PipelineMode::Deferred: return "Deferred (Shader Effects)";
        default: return "Unknown";
    }
}

void RenderScene::setSettings(const RenderSettings& settings) {
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
    if (m_legacyRenderer) {
        m_legacyRenderer->setHumanoidRenderer(hr);
    }
}

void RenderScene::setDropRenderer(DropRenderer* dr) {
    m_dropRenderer = dr;
    if (m_legacyRenderer) {
        m_legacyRenderer->setDropRenderer(dr);
    }
}

void RenderScene::setParticleSystem(ParticleSystem* ps) {
    m_particleSystem = ps;
    if (m_legacyRenderer) {
        m_legacyRenderer->setParticleSystem(ps);
    }
}

void RenderScene::setDropSystem(DropSystem* ds) {
    m_dropSystem = ds;
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
}

FrameContext RenderScene::buildFrameContext(const World& world, const Camera& camera, const Window& window) {
    FrameContext ctx;

    // Camera data (Phase 1: minimal bridge)
    // In later phases, this will be computed from camera state
    ctx.camera.position = camera.getPosition();
    ctx.camera.nearPlane = 0.1f;
    ctx.camera.farPlane = 500.0f;

    // Frame timing
    ctx.frameIndex = 0; // Will be populated from renderer state
    ctx.deltaTime = 0.0f;
    ctx.animationTime = 0.0f;
    ctx.shaderTime = 0.0f;

    // State
    ctx.world = &world;
    ctx.shared = &m_shared;

    return ctx;
}

void RenderScene::invalidateFrameHistory() {
    // Phase 1: Notify legacy renderer to invalidate temporal resources
    // In later phases, this will reset TAA history, SSAO history, etc.
}
