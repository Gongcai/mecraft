#include "ModelSceneDeferredRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

#include "engine/platform/Time.h"
#include "renderer/core/DeferredPipeline.h"
#include "renderer/core/IDeferredGeometryProvider.h"
#include "renderer/core/RenderScene.h"
#include "renderer/debug/RenderDebugService.h"
#include "renderer/passes/PostProcessPass.h"
#include "renderer/renderers/GameplaySkyRenderer.h"
#include "renderer/rhi/RhiCommandList.h"
#include "renderer/rhi/RhiCommandListPool.h"
#include "renderer/rhi/RhiDevice.h"
#include "renderer/shadow/ShadowRenderer.h"
#include "renderer/targets/DeferredRenderTargets.h"
#include "resource/ResourceMgr.h"
#include "ui/imgui/ImGuiRhiRenderer.h"
#include "world/DayNightSystem.h"
#include "world/WeatherSystem.h"

namespace {
SkyColorsData convertSkyColors(const GameplaySkyRenderer::SkyColors& source) {
    SkyColorsData result;
    result.top = source.top;
    result.horizon = source.horizon;
    result.fog = source.fog;
    result.halo = source.halo;
    result.sunDirection = source.sunDirection;
    result.sunScatter = source.sunScatter;
    result.sunLightColor = source.sunLightColor;
    result.skyAmbientColor = source.skyAmbientColor;
    result.shadowTintColor = source.shadowTintColor;
    result.horizonScatterColor = source.horizonScatterColor;
    result.cloudColor = source.cloudColor;
    result.moonDirection = source.moonDirection;
    result.moonLightColor = source.moonLightColor;
    result.haloStrength = source.haloStrength;
    result.horizonHaze = source.horizonHaze;
    result.sunGlare = source.sunGlare;
    result.sunVisibility = source.sunVisibility;
    result.moonVisibility = source.moonVisibility;
    result.moonPhaseAngle = source.moonPhaseAngle;
    result.dayFactor = source.dayFactor;
    result.nightFactor = source.nightFactor;
    result.horizonFactor = source.horizonFactor;
    result.rainFactor = source.rainFactor;
    result.wetnessFactor = source.wetnessFactor;
    result.cloudinessFactor = source.cloudinessFactor;
    return result;
}

SkyIlluminanceData convertSkyIlluminance(
    const GameplaySkyRenderer::SkyIlluminanceData& source) {
    SkyIlluminanceData result;
    result.directIlluminance = source.directIlluminance;
    result.skyIlluminance = source.skyIlluminance;
    result.sunIlluminance = source.sunIlluminance;
    result.moonIlluminance = source.moonIlluminance;
    result.cloudDynamicWeather = source.cloudDynamicWeather;
    return result;
}

RenderSettings modelSceneSettings() {
    RenderSettings settings;
    settings.pipelineMode = PipelineMode::Deferred;
    settings.occlusion.hiZEnabled = false;
    settings.voxelGi.enabled = false;
    settings.transparent.waterEffectsEnabled = false;
    settings.transparent.compositeEnabled = false;
    settings.weather.particlesEnabled = false;
    settings.weather.rainLinesEnabled = false;
    settings.taa.enabled = false;
    settings.upscale.type = TemporalUpscalerType::Native;
    settings.postProcess.motionBlurEnabled = false;
    settings.postProcess.dofEnabled = false;
    settings.shadow.gpuCascadeCullEnabled = false;
    settings.renderGraph.multithreadedRecordEnabled = false;
    settings.fog.autoDistanceByRenderDistance = false;
    return settings;
}

PostProcessEffects buildPostProcessEffects(
    const RenderSettings& settings,
    const FrameContext& context) {
    PostProcessEffects effects;
    effects.bloomEnabled = settings.postProcess.bloomEnabled;
    effects.bloomMipCount = settings.postProcess.bloomMipCount;
    effects.bloomThreshold = settings.postProcess.bloomThreshold;
    effects.bloomStrength = settings.postProcess.bloomStrength;
    effects.autoExposureEnabled = settings.postProcess.autoExposureEnabled;
    effects.autoExposureMin = settings.postProcess.autoExposureMin;
    effects.autoExposureMax = settings.postProcess.autoExposureMax;
    effects.autoExposureSpeed = settings.postProcess.autoExposureSpeed;
    effects.autoExposureBias = settings.postProcess.autoExposureBias;
    effects.autoExposureDayFactor = context.skyIntensity;
    effects.sunRaysEnabled = settings.postProcess.sunRaysEnabled;
    effects.sunRayStrength = settings.postProcess.sunRayStrength;
    effects.shaderpackGradingEnabled =
        settings.postProcess.shaderpackGradingEnabled;
    effects.tonemapMode = settings.postProcess.tonemapMode;
    effects.colorTemperature = settings.postProcess.colorTemperature;
    effects.vibrance = settings.postProcess.vibrance;
    effects.highlightCompression = settings.postProcess.highlightCompression;
    effects.filmEmulationStrength =
        settings.postProcess.filmEmulationStrength;
    effects.redModifierStrength = settings.postProcess.redModifierStrength;
    effects.colorLuma = glm::vec3(
        settings.postProcess.colorLumaR,
        settings.postProcess.colorLumaG,
        settings.postProcess.colorLumaB);
    effects.splitToneStrength = settings.postProcess.splitToneStrength;
    effects.vignetteStrength = settings.postProcess.vignetteStrength;
    effects.noiseDitherStrength = settings.postProcess.noiseDitherStrength;
    effects.sharpenStrength = settings.postProcess.sharpenStrength;
    effects.exposure = settings.postProcess.exposure;
    effects.gamma = settings.postProcess.gamma;
    effects.saturation = settings.postProcess.saturation;
    effects.contrast = settings.postProcess.contrast;
    effects.purkinjeShiftEnabled = settings.postProcess.purkinjeShiftEnabled;
    effects.bloomyFogEnabled = settings.postProcess.bloomyFogEnabled;
    effects.weatherWetness = context.weather.wetness;
    effects.weatherStorm = context.weather.storm;
    effects.skyWetness = context.weather.skyWetness;
    effects.fogWetness = context.weather.fogWetness;
    effects.cloudWetness = context.weather.cloudWetness;
    effects.weatherExposureBias = settings.weather.exposureBias;
    effects.weatherPostRainFog = settings.weather.postRainFog;
    effects.gameTime = context.shaderTime;

    const glm::vec4 sunClip = context.camera.viewProj * glm::vec4(
        context.camera.position + context.skyColors.sunDirection * 256.0f,
        1.0f);
    if (sunClip.w > 0.0001f) {
        const glm::vec3 ndc = glm::vec3(sunClip) / sunClip.w;
        effects.sunScreenPos = glm::vec2(
            ndc.x * 0.5f + 0.5f,
            1.0f - (ndc.y * 0.5f + 0.5f));
        const float onScreenX = 1.0f - std::clamp(
            std::abs(effects.sunScreenPos.x - 0.5f) * 2.0f,
            0.0f, 1.0f);
        const float onScreenY = 1.0f - std::clamp(
            std::abs(effects.sunScreenPos.y - 0.5f) * 2.0f,
            0.0f, 1.0f);
        const float horizonFade = std::clamp(
            (context.skyColors.sunDirection.y + 0.05f) / 0.45f,
            0.0f, 1.0f);
        effects.sunVisibility = std::clamp(
            onScreenX * onScreenY * horizonFade, 0.0f, 1.0f);
    }
    return effects;
}
} // namespace

struct ModelSceneDeferredRenderer::Impl {
    [[nodiscard]] bool beginDebugFrame() {
        RhiCommandList* commandList =
            commandListPool->acquire(RhiCommandListType::Graphics);
        if (commandList == nullptr ||
            !commandList->begin({
                "ModelScene.DebugFrame", RhiCommandListType::Graphics})) {
            return false;
        }
        debugService.beginFrame(*commandList);
        if (!commandList->end()) {
            return false;
        }
        RhiCommandList* submitted[] = {commandList};
        return rhiDevice->submit({
            "ModelScene.DebugFrame.Submit", submitted, 1u,
            RhiQueueType::Graphics});
    }

    [[nodiscard]] FrameContext buildFrameContext(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const float deltaTime) {
        FrameContext context;
        context.camera.view = view;
        context.camera.projection = projection;
        context.camera.viewProj = projection * view;
        context.camera.jitteredViewProj = context.camera.viewProj;
        context.camera.invViewProj = glm::inverse(context.camera.viewProj);
        context.camera.jitteredInvViewProj = context.camera.invViewProj;
        context.camera.position = cameraPosition;
        context.camera.nearPlane = 0.05f;
        context.camera.farPlane = 500.0f;
        context.camera.fovDegrees = 55.0f;
        context.renderExtent = {width, height};
        context.outputExtent = context.renderExtent;
        context.sceneCaptureColorTexture =
            postProcess.sceneColorTextureHandle();
        context.sceneCaptureDepthTexture =
            postProcess.sceneDepthTextureHandle();
        context.sceneCaptureColorView =
            postProcess.sceneColorTextureViewHandle();
        context.sceneCaptureDepthView =
            postProcess.sceneDepthTextureViewHandle();
        context.frameIndex = Time::getFrameIndex();
        context.deltaTime = deltaTime;
        context.animationTime = static_cast<float>(
            std::fmod(Time::getGameTime(), 16.0));
        context.shaderTime = static_cast<float>(
            std::fmod(Time::getRawTime(), 8192.0));
        context.temporalReset = !hasPreviousContext;
        if (hasPreviousContext) {
            context.prevCamera = previousContext.camera;
            context.previousViewProj = previousContext.camera.viewProj;
            context.previousInvViewProj = previousContext.camera.invViewProj;
            context.previousJitteredViewProj =
                previousContext.camera.jitteredViewProj;
        } else {
            context.prevCamera = context.camera;
            context.previousViewProj = context.camera.viewProj;
            context.previousInvViewProj = context.camera.invViewProj;
            context.previousJitteredViewProj =
                context.camera.jitteredViewProj;
        }
        context.previousViewProjWithCurrentJitter =
            context.previousViewProj;
        context.velocityClipToPrevClip = glm::mat4(
            glm::dmat4(context.previousViewProj) *
            glm::inverse(glm::dmat4(context.camera.viewProj)));

        const WeatherState& weatherState = weather.getRenderState();
        const WeatherDerived& weatherDerived = weather.getDerived();
        context.weather.wetness = weatherState.wetness;
        context.weather.storm = weatherState.storm;
        context.weather.surfaceWetness = weatherDerived.surfaceWetness;
        context.weather.skyWetness = weatherDerived.skyWetness;
        context.weather.fogWetness = weatherDerived.fogWetness;
        context.weather.cloudWetness = weatherDerived.cloudWetness;
        context.weather.precipitation = weatherDerived.precipitation;
        context.weather.rainStrength = weatherDerived.rainStrength;
        context.weather.thunderStrength = weatherDerived.thunderStrength;
        context.weather.lightningFlash = weatherDerived.lightningFlash;
        context.weather.aerialReduction = weatherState.aerialReduction;

        const GameplaySkyRenderer::SkyColors skyColors =
            sky.computeSkyColors(dayNight);
        context.skyColors = convertSkyColors(skyColors);
        context.skyIlluminance = convertSkyIlluminance(
            sky.computeSkyIlluminance(
                skyColors, context.weather.wetness,
                context.weather.storm));
        context.skyIntensity = dayNight.getSkyIntensity();

        context.fog.enabled = settings.fog.enabled;
        context.fog.mode = settings.fog.mode;
        context.fog.color = settings.fog.color;
        context.fog.startDistance = settings.fog.startDistance;
        context.fog.endDistance = settings.fog.endDistance;
        context.fog.density = settings.fog.density;
        context.volumetric.lightEnabled =
            settings.volumetric.lightEnabled;
        context.volumetric.uwLightEnabled =
            settings.volumetric.uwLightEnabled;
        context.volumetric.fogEnabled = settings.volumetric.fogEnabled;
        context.volumetric.fogStrength = settings.volumetric.fogStrength;
        context.volumetric.underwaterLightStrength =
            settings.volumetric.underwaterLightStrength;
        context.volumetric.fogCenterHeight =
            settings.volumetric.fogCenterHeight;
        context.volumetric.fogHeightSpread =
            settings.volumetric.fogHeightSpread;
        context.volumetric.fogNoiseScale =
            settings.volumetric.fogNoiseScale;
        context.volumetric.fogLightStrength =
            settings.volumetric.fogLightStrength;
        context.volumetric.fogDensityScale =
            settings.volumetric.fogDensityScale;
        context.volumetric.fogSamples = std::clamp(
            settings.volumetric.fogSamples, 2, 50);
        context.cloud.shadowsEnabled = settings.cloud.shadowsEnabled;
        context.cloud.shadowStrength = settings.cloud.shadowStrength;
        context.cloud.shadowScale = settings.cloud.shadowScale;
        context.cloud.shadowSpeed = settings.cloud.shadowSpeed;
        context.cloud.timeScale = settings.cloud.timeScale;
        context.cloud.coverage = 1.0f;
        context.cloud.density =
            0.85f * std::clamp(settings.cloud.density, 0.0f, 2.5f);
        context.cloud.height = settings.cloud.height;
        context.cloud.thickness = settings.cloud.thickness;
        context.cloud.planarCoverage = settings.cloud.planarCoverage;
        context.cloud.planarDensity = settings.cloud.planarDensity;
        context.cloud.planarAltitude = settings.cloud.planarAltitude;
        context.atmosphere.aerialStrength =
            settings.postProcess.aerialStrength;
        context.atmosphere.horizonScatterStrength =
            settings.postProcess.horizonScatterStrength;
        context.atmosphere.sunWarmth = settings.postProcess.sunWarmth;
        context.atmosphere.skyCoolness = settings.postProcess.skyCoolness;
        context.atmosphere.directWeatherOcclusionOverride = 0;
        context.atmosphere.directWeatherOcclusion = 0.0f;
        context.moonShadowActive =
            context.skyColors.moonVisibility >
            context.skyColors.sunVisibility;
        context.shared = &shared;
        context.dayNightSystem = &dayNight;
        context.weatherSystem = &weather;
        context.debugService = &debugService;
        return context;
    }

    ResourceMgr* resourceMgr = nullptr;
    RhiDevice* rhiDevice = nullptr;
    RhiCommandListPool* commandListPool = nullptr;
    ImGuiRhiRenderer* imguiRenderer = nullptr;
    IDeferredGeometryProvider* geometryProvider = nullptr;
    DeferredRenderTargets targets;
    GameplaySkyRenderer sky;
    shadow::ShadowRenderer shadowRenderer;
    DeferredPipeline pipeline;
    PostProcessPass postProcess;
    RenderDebugService debugService;
    DayNightSystem dayNight;
    WeatherSystem weather;
    SharedRenderResources shared;
    RenderSettings settings = modelSceneSettings();
    FrameContext previousContext;
    RhiSamplerHandle viewportSampler;
    uint64_t textureId = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    bool initialized = false;
    bool hasPreviousContext = false;
    std::string error;
};

ModelSceneDeferredRenderer::ModelSceneDeferredRenderer()
    : m_impl(std::make_unique<Impl>()) {}

ModelSceneDeferredRenderer::~ModelSceneDeferredRenderer() {
    shutdown();
}

bool ModelSceneDeferredRenderer::init(
    ResourceMgr& resourceMgr,
    RhiDevice& rhiDevice,
    RhiCommandListPool& commandListPool,
    ImGuiRhiRenderer& imguiRenderer,
    IDeferredGeometryProvider& geometryProvider) {
    shutdown();
    Impl& state = *m_impl;
    state.resourceMgr = &resourceMgr;
    state.rhiDevice = &rhiDevice;
    state.commandListPool = &commandListPool;
    state.imguiRenderer = &imguiRenderer;
    state.geometryProvider = &geometryProvider;
    if (!state.targets.init(rhiDevice, commandListPool) ||
        !state.targets.loadAtmosphereLut(
            "assets/textures/atmosphere/Final.lut")) {
        state.error = "failed to initialize model scene deferred targets";
        shutdown();
        return false;
    }
    state.sky.init(resourceMgr, rhiDevice);
    state.postProcess.init(resourceMgr, commandListPool);
    state.debugService.init(rhiDevice);
    state.dayNight.setTimeOfDay(300.0f);
    state.weather.setDebugWeatherPresetInstant(WeatherType::Clear);

    RhiSamplerDesc samplerDesc;
    samplerDesc.minFilter = RhiFilter::Linear;
    samplerDesc.magFilter = RhiFilter::Linear;
    samplerDesc.mipmapMode = RhiMipmapMode::Nearest;
    samplerDesc.addressU = RhiAddressMode::ClampToEdge;
    samplerDesc.addressV = RhiAddressMode::ClampToEdge;
    samplerDesc.addressW = RhiAddressMode::ClampToEdge;
    state.viewportSampler = rhiDevice.createSampler(samplerDesc);
    if (!state.viewportSampler.isValid()) {
        state.error = "failed to create model scene viewport sampler";
        shutdown();
        return false;
    }

    state.shared.rhiDevice = &rhiDevice;
    state.shared.commandListPool = &commandListPool;
    state.shared.deferredTargets = &state.targets;
    state.shared.sky = &state.sky;
    state.shared.shadowRenderer = &state.shadowRenderer;
    state.shared.resources = &resourceMgr;
    state.shared.deferredGeometryProvider = &geometryProvider;
    state.pipeline.init(state.shared);
    state.initialized = true;
    return true;
}

void ModelSceneDeferredRenderer::shutdown() {
    if (!m_impl) {
        return;
    }
    Impl& state = *m_impl;
    if (state.imguiRenderer != nullptr && state.textureId != 0u) {
        state.imguiRenderer->unregisterTexture(
            static_cast<ImTextureID>(state.textureId));
    }
    state.textureId = 0u;
    if (state.initialized) {
        state.pipeline.shutdown();
    }
    state.postProcess.shutdown();
    state.debugService.shutdown();
    state.sky.shutdown();
    state.targets.shutdown();
    if (state.rhiDevice != nullptr && state.viewportSampler.isValid()) {
        state.rhiDevice->destroySampler(state.viewportSampler);
    }
    state.viewportSampler = {};
    state.shared = {};
    state.resourceMgr = nullptr;
    state.rhiDevice = nullptr;
    state.commandListPool = nullptr;
    state.imguiRenderer = nullptr;
    state.geometryProvider = nullptr;
    state.width = 0u;
    state.height = 0u;
    state.hasPreviousContext = false;
    state.initialized = false;
}

bool ModelSceneDeferredRenderer::ensureViewport(
    const uint32_t width,
    const uint32_t height) {
    Impl& state = *m_impl;
    if (!state.initialized || width == 0u || height == 0u) {
        return false;
    }
    const bool resized = state.width != width || state.height != height;
    if (resized && state.textureId != 0u) {
        state.imguiRenderer->unregisterTexture(
            static_cast<ImTextureID>(state.textureId));
        state.textureId = 0u;
    }
    if (!state.postProcess.beginSceneCapture(
            *state.rhiDevice, static_cast<int>(width),
            static_cast<int>(height)) ||
        !state.postProcess.prepareTextureOutput(
            *state.rhiDevice, static_cast<int>(width),
            static_cast<int>(height))) {
        state.error = "failed to allocate model scene post-process targets";
        return false;
    }
    if (resized) {
        state.width = width;
        state.height = height;
        state.hasPreviousContext = false;
        state.pipeline.invalidateHistory();
    }
    if (state.textureId == 0u) {
        state.textureId = static_cast<uint64_t>(
            state.imguiRenderer->registerTexture(
                state.postProcess.compositeTextureViewHandle(),
                state.viewportSampler));
        if (state.textureId == 0u) {
            state.error =
                "failed to register model scene post-process texture";
            return false;
        }
    }
    state.error.clear();
    return true;
}

bool ModelSceneDeferredRenderer::render(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const float deltaTime) {
    Impl& state = *m_impl;
    if (!state.initialized || state.width == 0u || state.height == 0u ||
        !state.beginDebugFrame()) {
        state.error = "failed to begin model scene deferred frame";
        return false;
    }
    FrameContext context = state.buildFrameContext(
        view, projection, cameraPosition, deltaTime);
    const FrameOutput output = state.pipeline.renderFrame(
        context, state.settings);
    if (!output.sceneColor.isValid() || !output.gbufferDepth.isValid() ||
        !state.postProcess.setHdrInput(
            state.postProcess.sceneColorTextureHandle(),
            state.postProcess.sceneColorTextureViewHandle(),
            static_cast<int>(state.width),
            static_cast<int>(state.height))) {
        state.error = "failed to render model scene deferred pipeline";
        return false;
    }
    state.postProcess.setFrameEffects(
        buildPostProcessEffects(state.settings, context));
    const RhiTextureHandle texture = state.postProcess.compositeToTexture(
        *state.rhiDevice, deltaTime, output.gbufferDepth,
        state.debugService);
    if (!texture.isValid()) {
        state.error = "failed to composite model scene post-process output";
        return false;
    }
    state.previousContext = context;
    state.hasPreviousContext = true;
    state.error.clear();
    return true;
}

void ModelSceneDeferredRenderer::setTimeOfDay(const float timeOfDaySeconds) {
    Impl& state = *m_impl;
    if (!state.initialized) {
        std::abort();
    }
    const float currentTime = state.dayNight.getTimeOfDay();
    if (std::abs(currentTime - timeOfDaySeconds) <= 0.001f) {
        return;
    }
    state.dayNight.setTimeOfDay(timeOfDaySeconds);
    state.hasPreviousContext = false;
    state.pipeline.invalidateHistory();
}

float ModelSceneDeferredRenderer::timeOfDay() const {
    if (!m_impl->initialized) {
        std::abort();
    }
    return m_impl->dayNight.getTimeOfDay();
}

uint64_t ModelSceneDeferredRenderer::viewportTextureId() const {
    return m_impl->textureId;
}

uint32_t ModelSceneDeferredRenderer::viewportWidth() const {
    return m_impl->width;
}

uint32_t ModelSceneDeferredRenderer::viewportHeight() const {
    return m_impl->height;
}

const std::string& ModelSceneDeferredRenderer::lastError() const {
    return m_impl->error;
}
