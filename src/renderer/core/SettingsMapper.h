#ifndef MECRAFT_SETTINGS_MAPPER_H
#define MECRAFT_SETTINGS_MAPPER_H

#include "RenderSettings.h"
#include "Renderer.h"

/// Unified conversion between RenderPipelineSettings (legacy) and RenderSettings (new).
/// All conversions must go through this file to prevent field drift.
namespace settings_mapper {

/// Convert legacy RenderPipelineSettings to new RenderSettings.
/// Used by Renderer internals when calling passes that expect RenderSettings.
inline RenderSettings toRenderSettings(const Renderer::RenderPipelineSettings& p) {
    RenderSettings rs;

    // Pipeline mode
    rs.pipelineMode = (p.mode == Renderer::RenderPipelineMode::HybridDeferred)
        ? PipelineMode::Deferred : PipelineMode::Forward;

    // Shadow
    rs.shadow.enabled = p.shadowsEnabled;
    rs.shadow.softShadowsEnabled = p.softShadowsEnabled;
    rs.shadow.pcssShadowsEnabled = p.pcssShadowsEnabled;
    rs.shadow.contactShadowsEnabled = p.contactShadowsEnabled;
    rs.shadow.cloudShadowsEnabled = p.cloudShadowsEnabled;
    rs.shadow.resolution = p.shadowResolution;
    rs.shadow.distance = p.shadowDistance;
    rs.shadow.softness = p.shadowSoftness;
    rs.shadow.pcssStrength = p.shadowPcssStrength;
    rs.shadow.constantBias = p.shadowConstantBias;
    rs.shadow.slopeBias = p.shadowSlopeBias;
    rs.shadow.normalOffset = p.shadowNormalOffset;
    rs.shadow.contactShadowStrength = p.contactShadowStrength;
    rs.shadow.cloudShadowStrength = p.cloudShadowStrength;
    rs.shadow.cloudShadowScale = p.cloudShadowScale;
    rs.shadow.cloudShadowSpeed = p.cloudShadowSpeed;

    // SSAO
    rs.ssao.enabled = p.ssaoEnabled;
    rs.ssao.filterEnabled = p.ssaoFilterEnabled;
    rs.ssao.temporalEnabled = p.ssaoTemporalEnabled;
    rs.ssao.historyWeight = p.ssaoHistoryWeight;
    rs.ssao.radius = p.ssaoRadius;
    rs.ssao.strength = p.ssaoStrength;
    rs.ssao.samples = p.ssaoSamples;

    // Volumetric
    rs.volumetric.lightEnabled = p.volumetricLightEnabled;
    rs.volumetric.uwLightEnabled = p.uwVolumetricLightEnabled;
    rs.volumetric.fogEnabled = p.volumetricFogEnabled;
    rs.volumetric.skyRayEnabled = p.volumetricSkyRayEnabled;
    rs.volumetric.timeFadeEnabled = p.volumetricTimeFadeEnabled;
    rs.volumetric.temporalEnabled = p.volumetricTemporalEnabled;
    rs.volumetric.qualityTier = p.volumetricQualityTier;
    rs.volumetric.fogSamples = p.volumetricFogSamples;
    rs.volumetric.temporalWeight = p.volumetricTemporalWeight;
    rs.volumetric.shadowBiasScale = p.volumetricShadowBiasScale;
    rs.volumetric.fogStrength = p.volumetricFogStrength;
    rs.volumetric.underwaterLightStrength = p.underwaterVolumetricLightStrength;
    rs.volumetric.fogCenterHeight = p.vfogCenterHeight;
    rs.volumetric.fogHeightSpread = p.vfogHeightSpread;
    rs.volumetric.fogNoiseScale = p.vfogNoiseScale;
    rs.volumetric.fogLightStrength = p.vfogLightStrength;
    rs.volumetric.fogDensityScale = p.vfogDensityScale;
    rs.volumetric.freezeR1 = p.freezeR1;
    rs.volumetric.freezeBias = p.freezeBias;

    // Cloud
    rs.cloud.shadowsEnabled = p.cloudShadowsEnabled;
    rs.cloud.shadowStrength = p.cloudShadowStrength;
    rs.cloud.shadowScale = p.cloudShadowScale;
    rs.cloud.shadowSpeed = p.cloudShadowSpeed;
    rs.cloud.timeScale = p.cloudTimeScale;
    rs.cloud.sceneCloudCompositeStrength = p.sceneCloudCompositeStrength;

    // Reflection
    rs.reflection.filterEnabled = p.reflectionFilterEnabled;
    rs.reflection.temporalEnabled = p.reflectionTemporalEnabled;
    rs.reflection.historyWeight = p.reflectionHistoryWeight;
    rs.reflection.filterStrength = p.reflectionFilterStrength;
    rs.reflection.sceneReflectionCompositeStrength = p.sceneReflectionCompositeStrength;

    // Transparent / water
    rs.transparent.waterEffectsEnabled = p.waterEffectsEnabled;
    rs.transparent.compositeEnabled = p.transparentCompositeEnabled;

    // TAA
    rs.taa.enabled = p.taaEnabled;
    rs.taa.blendMin = p.taaBlendMin;
    rs.taa.blendMax = p.taaBlendMax;
    rs.taa.forceZeroVelocity = p.forceZeroVelocity;
    rs.taa.freezeJitter = p.freezeTaaJitter;

    // Post-process
    rs.postProcess.bloomEnabled = p.bloomEnabled;
    rs.postProcess.bloomThreshold = p.bloomThreshold;
    rs.postProcess.bloomStrength = p.bloomStrength;
    rs.postProcess.bloomyFogEnabled = p.bloomyFogEnabled;
    rs.postProcess.autoExposureEnabled = p.autoExposureEnabled;
    rs.postProcess.autoExposureMin = p.autoExposureMin;
    rs.postProcess.autoExposureMax = p.autoExposureMax;
    rs.postProcess.autoExposureSpeed = p.autoExposureSpeed;
    rs.postProcess.autoExposureBias = p.autoExposureBias;
    rs.postProcess.exposure = p.exposure;
    rs.postProcess.tonemapMode = p.tonemapMode;
    rs.postProcess.gamma = p.gamma;
    rs.postProcess.saturation = p.saturation;
    rs.postProcess.contrast = p.contrast;
    rs.postProcess.colorTemperature = p.colorTemperature;
    rs.postProcess.vibrance = p.vibrance;
    rs.postProcess.highlightCompression = p.highlightCompression;
    rs.postProcess.filmEmulationStrength = p.filmEmulationStrength;
    rs.postProcess.redModifierStrength = p.redModifierStrength;
    rs.postProcess.colorLumaR = p.colorLumaR;
    rs.postProcess.colorLumaG = p.colorLumaG;
    rs.postProcess.colorLumaB = p.colorLumaB;
    rs.postProcess.albedoDesaturation = p.albedoDesaturation;
    rs.postProcess.splitToneStrength = p.splitToneStrength;
    rs.postProcess.vignetteStrength = p.vignetteStrength;
    rs.postProcess.sunWarmth = p.sunWarmth;
    rs.postProcess.skyCoolness = p.skyCoolness;
    rs.postProcess.shadowDesaturation = p.shadowDesaturation;
    rs.postProcess.shadowTintStrength = p.shadowTintStrength;
    rs.postProcess.directSunStrength = p.directSunStrength;
    rs.postProcess.skyAmbientStrength = p.skyAmbientStrength;
    rs.postProcess.minimumAmbient = p.minimumAmbient;
    rs.postProcess.shadowMinLight = p.shadowMinLight;
    rs.postProcess.shadowContrast = p.shadowContrast;
    rs.postProcess.blockLightStrength = p.blockLightStrength;
    rs.postProcess.fakeBounceStrength = p.fakeBounceStrength;
    rs.postProcess.aerialPerspectiveEnabled = p.aerialPerspectiveEnabled;
    rs.postProcess.aerialStrength = p.aerialStrength;
    rs.postProcess.horizonScatterStrength = p.horizonScatterStrength;
    rs.postProcess.sharpenStrength = p.sharpenStrength;
    rs.postProcess.noiseDitherStrength = p.noiseDitherStrength;
    rs.postProcess.purkinjeShiftEnabled = p.purkinjeShiftEnabled;
    rs.postProcess.sunRaysEnabled = p.sunRaysEnabled;
    rs.postProcess.shaderpackGradingEnabled = p.shaderpackGradingEnabled;
    rs.postProcess.sunRayStrength = p.sunRayStrength;
    rs.postProcess.motionBlurEnabled = p.motionBlurEnabled;
    rs.postProcess.motionBlurStrength = p.motionBlurStrength;
    rs.postProcess.motionBlurSamples = p.motionBlurSamples;
    rs.postProcess.dofEnabled = p.dofEnabled;
    rs.postProcess.dofIntensity = p.dofIntensity;
    rs.postProcess.dofAperture = p.dofAperture;
    rs.postProcess.dofFocusDistance = p.dofFocusDistance;

    // Debug
    rs.debug.viewMode = p.debugViewMode;
    rs.debug.lightDebugMode = 0; // Not in legacy
    rs.debug.deferredLightDebugMode = p.deferredLightDebugMode;
    rs.debug.postprocessDebugMode = p.postprocessDebugMode;
    rs.debug.reflectionDebugMode = p.reflectionDebugMode;
    rs.debug.derivativeStrictMode = p.derivativeStrictMode;
    rs.debug.disableGreedyMeshing = p.debugDisableGreedyMeshing;

    // Fog: managed separately via Renderer::FogSettings, not in RenderPipelineSettings

    // Weather
    rs.weather.skylightScale = p.weatherSkylightScale;
    rs.weather.exposureBias = p.weatherExposureBias;
    rs.weather.postRainFog = p.weatherPostRainFog;
    rs.weather.rainAlphaScale = p.weatherRainAlphaScale;
    rs.weather.rainLinesEnabled = p.weatherRainLinesEnabled;
    rs.weather.particlesEnabled = p.sceneParticlesEnabled;
    rs.weather.wetSurfacesEnabled = p.rainWetSurfacesEnabled;
    rs.weather.surfaceRipplesEnabled = p.rainSurfaceRipplesEnabled;
    rs.weather.directWeatherOcclusion = p.directWeatherOcclusion;

    return rs;
}

/// Convert new RenderSettings to legacy RenderPipelineSettings.
/// Used by RenderScene::setSettings() to sync to legacy Renderer.
inline Renderer::RenderPipelineSettings toLegacySettings(const RenderSettings& s) {
    Renderer::RenderPipelineSettings p;

    p.mode = (s.pipelineMode == PipelineMode::Deferred)
        ? Renderer::RenderPipelineMode::HybridDeferred
        : Renderer::RenderPipelineMode::ForwardLegacy;

    // Shadow
    p.shadowsEnabled = s.shadow.enabled;
    p.softShadowsEnabled = s.shadow.softShadowsEnabled;
    p.pcssShadowsEnabled = s.shadow.pcssShadowsEnabled;
    p.contactShadowsEnabled = s.shadow.contactShadowsEnabled;
    p.cloudShadowsEnabled = s.shadow.cloudShadowsEnabled;
    p.shadowResolution = s.shadow.resolution;
    p.shadowDistance = s.shadow.distance;
    p.shadowSoftness = s.shadow.softness;
    p.shadowPcssStrength = s.shadow.pcssStrength;
    p.shadowConstantBias = s.shadow.constantBias;
    p.shadowSlopeBias = s.shadow.slopeBias;
    p.shadowNormalOffset = s.shadow.normalOffset;
    p.contactShadowStrength = s.shadow.contactShadowStrength;
    p.cloudShadowStrength = s.shadow.cloudShadowStrength;
    p.cloudShadowScale = s.shadow.cloudShadowScale;
    p.cloudShadowSpeed = s.shadow.cloudShadowSpeed;

    // SSAO
    p.ssaoEnabled = s.ssao.enabled;
    p.ssaoFilterEnabled = s.ssao.filterEnabled;
    p.ssaoTemporalEnabled = s.ssao.temporalEnabled;
    p.ssaoHistoryWeight = s.ssao.historyWeight;
    p.ssaoRadius = s.ssao.radius;
    p.ssaoStrength = s.ssao.strength;
    p.ssaoSamples = s.ssao.samples;

    // Volumetric
    p.volumetricLightEnabled = s.volumetric.lightEnabled;
    p.uwVolumetricLightEnabled = s.volumetric.uwLightEnabled;
    p.volumetricFogEnabled = s.volumetric.fogEnabled;
    p.volumetricSkyRayEnabled = s.volumetric.skyRayEnabled;
    p.volumetricTimeFadeEnabled = s.volumetric.timeFadeEnabled;
    p.volumetricTemporalEnabled = s.volumetric.temporalEnabled;
    p.volumetricQualityTier = s.volumetric.qualityTier;
    p.volumetricFogSamples = s.volumetric.fogSamples;
    p.volumetricTemporalWeight = s.volumetric.temporalWeight;
    p.volumetricShadowBiasScale = s.volumetric.shadowBiasScale;
    p.volumetricFogStrength = s.volumetric.fogStrength;
    p.underwaterVolumetricLightStrength = s.volumetric.underwaterLightStrength;
    p.vfogCenterHeight = s.volumetric.fogCenterHeight;
    p.vfogHeightSpread = s.volumetric.fogHeightSpread;
    p.vfogNoiseScale = s.volumetric.fogNoiseScale;
    p.vfogLightStrength = s.volumetric.fogLightStrength;
    p.vfogDensityScale = s.volumetric.fogDensityScale;
    p.freezeR1 = s.volumetric.freezeR1;
    p.freezeBias = s.volumetric.freezeBias;

    // Cloud
    p.cloudShadowsEnabled = s.cloud.shadowsEnabled;
    p.cloudShadowStrength = s.cloud.shadowStrength;
    p.cloudShadowScale = s.cloud.shadowScale;
    p.cloudShadowSpeed = s.cloud.shadowSpeed;
    p.cloudTimeScale = s.cloud.timeScale;
    p.sceneCloudCompositeStrength = s.cloud.sceneCloudCompositeStrength;

    // Reflection
    p.reflectionFilterEnabled = s.reflection.filterEnabled;
    p.reflectionTemporalEnabled = s.reflection.temporalEnabled;
    p.reflectionHistoryWeight = s.reflection.historyWeight;
    p.reflectionFilterStrength = s.reflection.filterStrength;
    p.sceneReflectionCompositeStrength = s.reflection.sceneReflectionCompositeStrength;

    // Transparent / water
    p.waterEffectsEnabled = s.transparent.waterEffectsEnabled;
    p.transparentCompositeEnabled = s.transparent.compositeEnabled;

    // TAA
    p.taaEnabled = s.taa.enabled;
    p.taaBlendMin = s.taa.blendMin;
    p.taaBlendMax = s.taa.blendMax;
    p.forceZeroVelocity = s.taa.forceZeroVelocity;
    p.freezeTaaJitter = s.taa.freezeJitter;

    // Post-process
    p.bloomEnabled = s.postProcess.bloomEnabled;
    p.bloomThreshold = s.postProcess.bloomThreshold;
    p.bloomStrength = s.postProcess.bloomStrength;
    p.bloomyFogEnabled = s.postProcess.bloomyFogEnabled;
    p.autoExposureEnabled = s.postProcess.autoExposureEnabled;
    p.autoExposureMin = s.postProcess.autoExposureMin;
    p.autoExposureMax = s.postProcess.autoExposureMax;
    p.autoExposureSpeed = s.postProcess.autoExposureSpeed;
    p.autoExposureBias = s.postProcess.autoExposureBias;
    p.exposure = s.postProcess.exposure;
    p.tonemapMode = s.postProcess.tonemapMode;
    p.gamma = s.postProcess.gamma;
    p.saturation = s.postProcess.saturation;
    p.contrast = s.postProcess.contrast;
    p.colorTemperature = s.postProcess.colorTemperature;
    p.vibrance = s.postProcess.vibrance;
    p.highlightCompression = s.postProcess.highlightCompression;
    p.filmEmulationStrength = s.postProcess.filmEmulationStrength;
    p.redModifierStrength = s.postProcess.redModifierStrength;
    p.colorLumaR = s.postProcess.colorLumaR;
    p.colorLumaG = s.postProcess.colorLumaG;
    p.colorLumaB = s.postProcess.colorLumaB;
    p.albedoDesaturation = s.postProcess.albedoDesaturation;
    p.splitToneStrength = s.postProcess.splitToneStrength;
    p.vignetteStrength = s.postProcess.vignetteStrength;
    p.sunWarmth = s.postProcess.sunWarmth;
    p.skyCoolness = s.postProcess.skyCoolness;
    p.shadowDesaturation = s.postProcess.shadowDesaturation;
    p.shadowTintStrength = s.postProcess.shadowTintStrength;
    p.directSunStrength = s.postProcess.directSunStrength;
    p.skyAmbientStrength = s.postProcess.skyAmbientStrength;
    p.minimumAmbient = s.postProcess.minimumAmbient;
    p.shadowMinLight = s.postProcess.shadowMinLight;
    p.shadowContrast = s.postProcess.shadowContrast;
    p.blockLightStrength = s.postProcess.blockLightStrength;
    p.fakeBounceStrength = s.postProcess.fakeBounceStrength;
    p.aerialPerspectiveEnabled = s.postProcess.aerialPerspectiveEnabled;
    p.aerialStrength = s.postProcess.aerialStrength;
    p.horizonScatterStrength = s.postProcess.horizonScatterStrength;
    p.sharpenStrength = s.postProcess.sharpenStrength;
    p.noiseDitherStrength = s.postProcess.noiseDitherStrength;
    p.purkinjeShiftEnabled = s.postProcess.purkinjeShiftEnabled;
    p.sunRaysEnabled = s.postProcess.sunRaysEnabled;
    p.shaderpackGradingEnabled = s.postProcess.shaderpackGradingEnabled;
    p.sunRayStrength = s.postProcess.sunRayStrength;
    p.motionBlurEnabled = s.postProcess.motionBlurEnabled;
    p.motionBlurStrength = s.postProcess.motionBlurStrength;
    p.motionBlurSamples = s.postProcess.motionBlurSamples;
    p.dofEnabled = s.postProcess.dofEnabled;
    p.dofIntensity = s.postProcess.dofIntensity;
    p.dofAperture = s.postProcess.dofAperture;
    p.dofFocusDistance = s.postProcess.dofFocusDistance;

    // Debug
    p.debugViewMode = s.debug.viewMode;
    p.deferredLightDebugMode = s.debug.deferredLightDebugMode;
    p.postprocessDebugMode = s.debug.postprocessDebugMode;
    p.reflectionDebugMode = s.debug.reflectionDebugMode;
    p.derivativeStrictMode = s.debug.derivativeStrictMode;
    p.debugDisableGreedyMeshing = s.debug.disableGreedyMeshing;

    // Fog: managed separately via Renderer::FogSettings, not in RenderPipelineSettings

    // Weather
    p.weatherSkylightScale = s.weather.skylightScale;
    p.weatherExposureBias = s.weather.exposureBias;
    p.weatherPostRainFog = s.weather.postRainFog;
    p.weatherRainAlphaScale = s.weather.rainAlphaScale;
    p.weatherRainLinesEnabled = s.weather.rainLinesEnabled;
    p.sceneParticlesEnabled = s.weather.particlesEnabled;
    p.rainWetSurfacesEnabled = s.weather.wetSurfacesEnabled;
    p.rainSurfaceRipplesEnabled = s.weather.surfaceRipplesEnabled;
    p.directWeatherOcclusion = s.weather.directWeatherOcclusion;

    return p;
}

/// Sync fog settings from Renderer::FogSettings to RenderSettings::FogSettings.
/// Fog is managed separately from RenderPipelineSettings, so this is a dedicated sync path.
inline void syncFogToRenderSettings(const Renderer::FogSettings& src, FogSettings& dst) {
    dst.enabled = src.enabled;
    dst.mode = static_cast<int>(src.mode);
    dst.color = src.color;
    dst.startDistance = src.startDistance;
    dst.endDistance = src.endDistance;
    dst.density = src.density;
    dst.autoDistanceByRenderDistance = src.autoDistanceByRenderDistance;
    dst.autoEndOffsetChunks = src.autoEndOffsetChunks;
    dst.autoFadeWidthChunks = src.autoFadeWidthChunks;
}

} // namespace settings_mapper

#endif // MECRAFT_SETTINGS_MAPPER_H
