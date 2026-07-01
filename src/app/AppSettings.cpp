#include "AppSettings.h"

#include "../Paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

constexpr int kDefaultRenderDistance = 16;
constexpr int kMinRenderDistance = 2;
constexpr int kMaxRenderDistance = 32;

json readSettingsFile() {
    std::ifstream file(SETTINGS_PATH);
    if (!file.is_open()) {
        return json::object();
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return json::object();
    }
    return root.is_object() ? root : json::object();
}

bool writeSettingsFile(const json& root) {
    const std::filesystem::path path(SETTINGS_PATH);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << root.dump(2) << '\n';
    return true;
}

void readBool(const json& obj, const char* key, bool& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_boolean()) {
        out = it->get<bool>();
    }
}

void readInt(const json& obj, const char* key, int& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_number()) {
        out = it->get<int>();
    }
}

void readFloat(const json& obj, const char* key, float& out) {
    auto it = obj.find(key);
    if (it != obj.end() && it->is_number()) {
        out = it->get<float>();
    }
}

void readVec3(const json& obj, const char* key, glm::vec3& out) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return;
    }
    if (it->is_array() && it->size() >= 3) {
        const auto& arr = *it;
        if (arr[0].is_number() && arr[1].is_number() && arr[2].is_number()) {
            out = glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
        }
    } else if (it->is_object()) {
        glm::vec3 value = out;
        readFloat(*it, "x", value.x);
        readFloat(*it, "y", value.y);
        readFloat(*it, "z", value.z);
        out = value;
    }
}

json toJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

void applyShadowSettings(const json& j, ShadowSettings& s) {
    readBool(j, "enabled", s.enabled);
    readBool(j, "softShadowsEnabled", s.softShadowsEnabled);
    readBool(j, "pcssShadowsEnabled", s.pcssShadowsEnabled);
    readBool(j, "contactShadowsEnabled", s.contactShadowsEnabled);
    readInt(j, "resolution", s.resolution);
    readFloat(j, "distance", s.distance);
    readFloat(j, "softness", s.softness);
    readFloat(j, "pcssStrength", s.pcssStrength);
    readFloat(j, "constantBias", s.constantBias);
    readFloat(j, "slopeBias", s.slopeBias);
    readFloat(j, "normalOffset", s.normalOffset);
    readFloat(j, "contactShadowStrength", s.contactShadowStrength);
}

json toJson(const ShadowSettings& s) {
    return {
        {"enabled", s.enabled},
        {"softShadowsEnabled", s.softShadowsEnabled},
        {"pcssShadowsEnabled", s.pcssShadowsEnabled},
        {"contactShadowsEnabled", s.contactShadowsEnabled},
        {"resolution", s.resolution},
        {"distance", s.distance},
        {"softness", s.softness},
        {"pcssStrength", s.pcssStrength},
        {"constantBias", s.constantBias},
        {"slopeBias", s.slopeBias},
        {"normalOffset", s.normalOffset},
        {"contactShadowStrength", s.contactShadowStrength},
    };
}

void applySsaoSettings(const json& j, SsaoSettings& s) {
    readBool(j, "enabled", s.enabled);
    readBool(j, "filterEnabled", s.filterEnabled);
    readBool(j, "temporalEnabled", s.temporalEnabled);
    readFloat(j, "historyWeight", s.historyWeight);
    readFloat(j, "radius", s.radius);
    readFloat(j, "strength", s.strength);
    readInt(j, "samples", s.samples);
}

json toJson(const SsaoSettings& s) {
    return {
        {"enabled", s.enabled},
        {"filterEnabled", s.filterEnabled},
        {"temporalEnabled", s.temporalEnabled},
        {"historyWeight", s.historyWeight},
        {"radius", s.radius},
        {"strength", s.strength},
        {"samples", s.samples},
    };
}

void applySsgiSettings(const json& j, SsgiSettings& s) {
    readBool(j, "enabled", s.enabled);
    readBool(j, "temporalEnabled", s.temporalEnabled);
    readBool(j, "denoiseEnabled", s.denoiseEnabled);
    readFloat(j, "historyWeight", s.historyWeight);
    readFloat(j, "radius", s.radius);
    readFloat(j, "strength", s.strength);
    readFloat(j, "maxDistance", s.maxDistance);
    readFloat(j, "thickness", s.thickness);
    readFloat(j, "denoiseStrength", s.denoiseStrength);
    readFloat(j, "radianceFilterStrength", s.radianceFilterStrength);
    readFloat(j, "colorBleedStrength", s.colorBleedStrength);
    readInt(j, "samples", s.samples);
    readInt(j, "denoiseIterations", s.denoiseIterations);
}

json toJson(const SsgiSettings& s) {
    return {
        {"enabled", s.enabled},
        {"temporalEnabled", s.temporalEnabled},
        {"denoiseEnabled", s.denoiseEnabled},
        {"historyWeight", s.historyWeight},
        {"radius", s.radius},
        {"strength", s.strength},
        {"maxDistance", s.maxDistance},
        {"thickness", s.thickness},
        {"denoiseStrength", s.denoiseStrength},
        {"radianceFilterStrength", s.radianceFilterStrength},
        {"colorBleedStrength", s.colorBleedStrength},
        {"samples", s.samples},
        {"denoiseIterations", s.denoiseIterations},
    };
}

void applyVoxelGiSettings(const json& j, VoxelGiSettings& s) {
    readBool(j, "enabled", s.enabled);
    readBool(j, "debugEnabled", s.debugEnabled);
    readInt(j, "resolution", s.resolution);
    readInt(j, "updateInterval", s.updateInterval);
    readInt(j, "coneSteps", s.coneSteps);
    readFloat(j, "voxelSize", s.voxelSize);
    readFloat(j, "strength", s.strength);
    readFloat(j, "normalBias", s.normalBias);
    readFloat(j, "sampleDistance", s.sampleDistance);
    readFloat(j, "traceDistance", s.traceDistance);
    readFloat(j, "coneAperture", s.coneAperture);
}

json toJson(const VoxelGiSettings& s) {
    return {
        {"enabled", s.enabled},
        {"debugEnabled", s.debugEnabled},
        {"resolution", s.resolution},
        {"updateInterval", s.updateInterval},
        {"coneSteps", s.coneSteps},
        {"voxelSize", s.voxelSize},
        {"strength", s.strength},
        {"normalBias", s.normalBias},
        {"sampleDistance", s.sampleDistance},
        {"traceDistance", s.traceDistance},
        {"coneAperture", s.coneAperture},
    };
}

void applyVolumetricSettings(const json& j, VolumetricSettings& s) {
    readBool(j, "lightEnabled", s.lightEnabled);
    readBool(j, "uwLightEnabled", s.uwLightEnabled);
    readBool(j, "fogEnabled", s.fogEnabled);
    readBool(j, "skyRayEnabled", s.skyRayEnabled);
    readBool(j, "timeFadeEnabled", s.timeFadeEnabled);
    readBool(j, "temporalEnabled", s.temporalEnabled);
    readInt(j, "qualityTier", s.qualityTier);
    readInt(j, "fogSamples", s.fogSamples);
    readInt(j, "updateInterval", s.updateInterval);
    readFloat(j, "temporalWeight", s.temporalWeight);
    readFloat(j, "shadowBiasScale", s.shadowBiasScale);
    readFloat(j, "fogStrength", s.fogStrength);
    readFloat(j, "underwaterLightStrength", s.underwaterLightStrength);
    readFloat(j, "fogCenterHeight", s.fogCenterHeight);
    readFloat(j, "fogHeightSpread", s.fogHeightSpread);
    readFloat(j, "fogNoiseScale", s.fogNoiseScale);
    readFloat(j, "fogLightStrength", s.fogLightStrength);
    readFloat(j, "fogDensityScale", s.fogDensityScale);
    readFloat(j, "baseDensity", s.baseDensity);
    readFloat(j, "maxDistance", s.maxDistance);
    readBool(j, "freezeR1", s.freezeR1);
    readBool(j, "freezeBias", s.freezeBias);
}

json toJson(const VolumetricSettings& s) {
    return {
        {"lightEnabled", s.lightEnabled},
        {"uwLightEnabled", s.uwLightEnabled},
        {"fogEnabled", s.fogEnabled},
        {"skyRayEnabled", s.skyRayEnabled},
        {"timeFadeEnabled", s.timeFadeEnabled},
        {"temporalEnabled", s.temporalEnabled},
        {"qualityTier", s.qualityTier},
        {"fogSamples", s.fogSamples},
        {"updateInterval", s.updateInterval},
        {"temporalWeight", s.temporalWeight},
        {"shadowBiasScale", s.shadowBiasScale},
        {"fogStrength", s.fogStrength},
        {"underwaterLightStrength", s.underwaterLightStrength},
        {"fogCenterHeight", s.fogCenterHeight},
        {"fogHeightSpread", s.fogHeightSpread},
        {"fogNoiseScale", s.fogNoiseScale},
        {"fogLightStrength", s.fogLightStrength},
        {"fogDensityScale", s.fogDensityScale},
        {"baseDensity", s.baseDensity},
        {"maxDistance", s.maxDistance},
        {"freezeR1", s.freezeR1},
        {"freezeBias", s.freezeBias},
    };
}

void applyCloudSettings(const json& j, CloudSettings& s) {
    readBool(j, "shadowsEnabled", s.shadowsEnabled);
    readInt(j, "updateInterval", s.updateInterval);
    readFloat(j, "shadowStrength", s.shadowStrength);
    readFloat(j, "shadowScale", s.shadowScale);
    readFloat(j, "shadowSpeed", s.shadowSpeed);
    readFloat(j, "timeScale", s.timeScale);
    readFloat(j, "coverage", s.coverage);
    readFloat(j, "density", s.density);
    readFloat(j, "height", s.height);
    readFloat(j, "thickness", s.thickness);
    readFloat(j, "planarCoverage", s.planarCoverage);
    readFloat(j, "planarDensity", s.planarDensity);
    readFloat(j, "planarAltitude", s.planarAltitude);
    readFloat(j, "sceneCloudCompositeStrength", s.sceneCloudCompositeStrength);
}

json toJson(const CloudSettings& s) {
    return {
        {"shadowsEnabled", s.shadowsEnabled},
        {"updateInterval", s.updateInterval},
        {"shadowStrength", s.shadowStrength},
        {"shadowScale", s.shadowScale},
        {"shadowSpeed", s.shadowSpeed},
        {"timeScale", s.timeScale},
        {"coverage", s.coverage},
        {"density", s.density},
        {"height", s.height},
        {"thickness", s.thickness},
        {"planarCoverage", s.planarCoverage},
        {"planarDensity", s.planarDensity},
        {"planarAltitude", s.planarAltitude},
        {"sceneCloudCompositeStrength", s.sceneCloudCompositeStrength},
    };
}

void applyReflectionSettings(const json& j, ReflectionSettings& s) {
    readBool(j, "filterEnabled", s.filterEnabled);
    readBool(j, "temporalEnabled", s.temporalEnabled);
    readFloat(j, "historyWeight", s.historyWeight);
    readFloat(j, "filterStrength", s.filterStrength);
    readFloat(j, "sceneReflectionCompositeStrength", s.sceneReflectionCompositeStrength);
}

json toJson(const ReflectionSettings& s) {
    return {
        {"filterEnabled", s.filterEnabled},
        {"temporalEnabled", s.temporalEnabled},
        {"historyWeight", s.historyWeight},
        {"filterStrength", s.filterStrength},
        {"sceneReflectionCompositeStrength", s.sceneReflectionCompositeStrength},
    };
}

void applyTransparentSettings(const json& j, TransparentSettings& s) {
    readBool(j, "waterEffectsEnabled", s.waterEffectsEnabled);
    readBool(j, "compositeEnabled", s.compositeEnabled);
}

json toJson(const TransparentSettings& s) {
    return {
        {"waterEffectsEnabled", s.waterEffectsEnabled},
        {"compositeEnabled", s.compositeEnabled},
    };
}

void applyTaaSettings(const json& j, TaaSettings& s) {
    readBool(j, "enabled", s.enabled);
    readFloat(j, "blendMin", s.blendMin);
    readFloat(j, "blendMax", s.blendMax);
    readBool(j, "forceZeroVelocity", s.forceZeroVelocity);
    readBool(j, "freezeJitter", s.freezeJitter);
}

json toJson(const TaaSettings& s) {
    return {
        {"enabled", s.enabled},
        {"blendMin", s.blendMin},
        {"blendMax", s.blendMax},
        {"forceZeroVelocity", s.forceZeroVelocity},
        {"freezeJitter", s.freezeJitter},
    };
}

void applyPostProcessSettings(const json& j, PostProcessSettings& s) {
    readBool(j, "bloomEnabled", s.bloomEnabled);
    readInt(j, "bloomMipCount", s.bloomMipCount);
    readFloat(j, "bloomThreshold", s.bloomThreshold);
    readFloat(j, "bloomStrength", s.bloomStrength);
    readBool(j, "bloomyFogEnabled", s.bloomyFogEnabled);
    readBool(j, "autoExposureEnabled", s.autoExposureEnabled);
    readFloat(j, "autoExposureMin", s.autoExposureMin);
    readFloat(j, "autoExposureMax", s.autoExposureMax);
    readFloat(j, "autoExposureSpeed", s.autoExposureSpeed);
    readFloat(j, "autoExposureBias", s.autoExposureBias);
    readFloat(j, "exposure", s.exposure);
    readInt(j, "tonemapMode", s.tonemapMode);
    readFloat(j, "gamma", s.gamma);
    readFloat(j, "saturation", s.saturation);
    readFloat(j, "contrast", s.contrast);
    readFloat(j, "colorTemperature", s.colorTemperature);
    readFloat(j, "vibrance", s.vibrance);
    readFloat(j, "highlightCompression", s.highlightCompression);
    readFloat(j, "filmEmulationStrength", s.filmEmulationStrength);
    readFloat(j, "redModifierStrength", s.redModifierStrength);
    readFloat(j, "colorLumaR", s.colorLumaR);
    readFloat(j, "colorLumaG", s.colorLumaG);
    readFloat(j, "colorLumaB", s.colorLumaB);
    readFloat(j, "albedoDesaturation", s.albedoDesaturation);
    readFloat(j, "splitToneStrength", s.splitToneStrength);
    readFloat(j, "vignetteStrength", s.vignetteStrength);
    readFloat(j, "sunWarmth", s.sunWarmth);
    readFloat(j, "skyCoolness", s.skyCoolness);
    readFloat(j, "shadowDesaturation", s.shadowDesaturation);
    readFloat(j, "shadowTintStrength", s.shadowTintStrength);
    readFloat(j, "directSunStrength", s.directSunStrength);
    readFloat(j, "skyAmbientStrength", s.skyAmbientStrength);
    readFloat(j, "minimumAmbient", s.minimumAmbient);
    readFloat(j, "shadowMinLight", s.shadowMinLight);
    readFloat(j, "shadowContrast", s.shadowContrast);
    readFloat(j, "blockLightStrength", s.blockLightStrength);
    readFloat(j, "fakeBounceStrength", s.fakeBounceStrength);
    readBool(j, "aerialPerspectiveEnabled", s.aerialPerspectiveEnabled);
    readFloat(j, "aerialStrength", s.aerialStrength);
    readFloat(j, "horizonScatterStrength", s.horizonScatterStrength);
    readFloat(j, "sharpenStrength", s.sharpenStrength);
    readFloat(j, "noiseDitherStrength", s.noiseDitherStrength);
    readBool(j, "purkinjeShiftEnabled", s.purkinjeShiftEnabled);
    readBool(j, "sunRaysEnabled", s.sunRaysEnabled);
    readBool(j, "shaderpackGradingEnabled", s.shaderpackGradingEnabled);
    readFloat(j, "sunRayStrength", s.sunRayStrength);
    readBool(j, "motionBlurEnabled", s.motionBlurEnabled);
    readFloat(j, "motionBlurStrength", s.motionBlurStrength);
    readInt(j, "motionBlurSamples", s.motionBlurSamples);
    readBool(j, "dofEnabled", s.dofEnabled);
    readFloat(j, "dofIntensity", s.dofIntensity);
    readFloat(j, "dofAperture", s.dofAperture);
    readFloat(j, "dofFocusDistance", s.dofFocusDistance);
}

json toJson(const PostProcessSettings& s) {
    return {
        {"bloomEnabled", s.bloomEnabled},
        {"bloomMipCount", s.bloomMipCount},
        {"bloomThreshold", s.bloomThreshold},
        {"bloomStrength", s.bloomStrength},
        {"bloomyFogEnabled", s.bloomyFogEnabled},
        {"autoExposureEnabled", s.autoExposureEnabled},
        {"autoExposureMin", s.autoExposureMin},
        {"autoExposureMax", s.autoExposureMax},
        {"autoExposureSpeed", s.autoExposureSpeed},
        {"autoExposureBias", s.autoExposureBias},
        {"exposure", s.exposure},
        {"tonemapMode", s.tonemapMode},
        {"gamma", s.gamma},
        {"saturation", s.saturation},
        {"contrast", s.contrast},
        {"colorTemperature", s.colorTemperature},
        {"vibrance", s.vibrance},
        {"highlightCompression", s.highlightCompression},
        {"filmEmulationStrength", s.filmEmulationStrength},
        {"redModifierStrength", s.redModifierStrength},
        {"colorLumaR", s.colorLumaR},
        {"colorLumaG", s.colorLumaG},
        {"colorLumaB", s.colorLumaB},
        {"albedoDesaturation", s.albedoDesaturation},
        {"splitToneStrength", s.splitToneStrength},
        {"vignetteStrength", s.vignetteStrength},
        {"sunWarmth", s.sunWarmth},
        {"skyCoolness", s.skyCoolness},
        {"shadowDesaturation", s.shadowDesaturation},
        {"shadowTintStrength", s.shadowTintStrength},
        {"directSunStrength", s.directSunStrength},
        {"skyAmbientStrength", s.skyAmbientStrength},
        {"minimumAmbient", s.minimumAmbient},
        {"shadowMinLight", s.shadowMinLight},
        {"shadowContrast", s.shadowContrast},
        {"blockLightStrength", s.blockLightStrength},
        {"fakeBounceStrength", s.fakeBounceStrength},
        {"aerialPerspectiveEnabled", s.aerialPerspectiveEnabled},
        {"aerialStrength", s.aerialStrength},
        {"horizonScatterStrength", s.horizonScatterStrength},
        {"sharpenStrength", s.sharpenStrength},
        {"noiseDitherStrength", s.noiseDitherStrength},
        {"purkinjeShiftEnabled", s.purkinjeShiftEnabled},
        {"sunRaysEnabled", s.sunRaysEnabled},
        {"shaderpackGradingEnabled", s.shaderpackGradingEnabled},
        {"sunRayStrength", s.sunRayStrength},
        {"motionBlurEnabled", s.motionBlurEnabled},
        {"motionBlurStrength", s.motionBlurStrength},
        {"motionBlurSamples", s.motionBlurSamples},
        {"dofEnabled", s.dofEnabled},
        {"dofIntensity", s.dofIntensity},
        {"dofAperture", s.dofAperture},
        {"dofFocusDistance", s.dofFocusDistance},
    };
}

void applyUpscaleSettings(const json& j, UpscaleSettings& s) {
    readBool(j, "fsr1Enabled", s.fsr1Enabled);
    readFloat(j, "renderScale", s.renderScale);
    readFloat(j, "sharpness", s.sharpness);
}

json toJson(const UpscaleSettings& s) {
    return {
        {"fsr1Enabled", s.fsr1Enabled},
        {"renderScale", s.renderScale},
        {"sharpness", s.sharpness},
    };
}

void applyDebugSettings(const json& j, DebugSettings& s) {
    readInt(j, "viewMode", s.viewMode);
    readInt(j, "lightDebugMode", s.lightDebugMode);
    readInt(j, "deferredLightDebugMode", s.deferredLightDebugMode);
    readInt(j, "postprocessDebugMode", s.postprocessDebugMode);
    readInt(j, "reflectionDebugMode", s.reflectionDebugMode);
    readBool(j, "derivativeStrictMode", s.derivativeStrictMode);
    readBool(j, "disableGreedyMeshing", s.disableGreedyMeshing);
}

json toJson(const DebugSettings& s) {
    return {
        {"viewMode", s.viewMode},
        {"lightDebugMode", s.lightDebugMode},
        {"deferredLightDebugMode", s.deferredLightDebugMode},
        {"postprocessDebugMode", s.postprocessDebugMode},
        {"reflectionDebugMode", s.reflectionDebugMode},
        {"derivativeStrictMode", s.derivativeStrictMode},
        {"disableGreedyMeshing", s.disableGreedyMeshing},
    };
}

void applyFogSettings(const json& j, FogSettings& s) {
    readBool(j, "enabled", s.enabled);
    readInt(j, "mode", s.mode);
    readVec3(j, "color", s.color);
    readFloat(j, "startDistance", s.startDistance);
    readFloat(j, "endDistance", s.endDistance);
    readFloat(j, "density", s.density);
    readBool(j, "autoDistanceByRenderDistance", s.autoDistanceByRenderDistance);
    readFloat(j, "autoEndOffsetChunks", s.autoEndOffsetChunks);
    readFloat(j, "autoFadeWidthChunks", s.autoFadeWidthChunks);
}

json toJson(const FogSettings& s) {
    return {
        {"enabled", s.enabled},
        {"mode", s.mode},
        {"color", toJson(s.color)},
        {"startDistance", s.startDistance},
        {"endDistance", s.endDistance},
        {"density", s.density},
        {"autoDistanceByRenderDistance", s.autoDistanceByRenderDistance},
        {"autoEndOffsetChunks", s.autoEndOffsetChunks},
        {"autoFadeWidthChunks", s.autoFadeWidthChunks},
    };
}

void applyWeatherSettings(const json& j, WeatherRenderSettings& s) {
    readFloat(j, "skylightScale", s.skylightScale);
    readFloat(j, "exposureBias", s.exposureBias);
    readFloat(j, "postRainFog", s.postRainFog);
    readFloat(j, "rainAlphaScale", s.rainAlphaScale);
    readBool(j, "rainLinesEnabled", s.rainLinesEnabled);
    readBool(j, "particlesEnabled", s.particlesEnabled);
    readBool(j, "wetSurfacesEnabled", s.wetSurfacesEnabled);
    readBool(j, "surfaceRipplesEnabled", s.surfaceRipplesEnabled);
    readFloat(j, "directWeatherOcclusion", s.directWeatherOcclusion);
}

json toJson(const WeatherRenderSettings& s) {
    return {
        {"skylightScale", s.skylightScale},
        {"exposureBias", s.exposureBias},
        {"postRainFog", s.postRainFog},
        {"rainAlphaScale", s.rainAlphaScale},
        {"rainLinesEnabled", s.rainLinesEnabled},
        {"particlesEnabled", s.particlesEnabled},
        {"wetSurfacesEnabled", s.wetSurfacesEnabled},
        {"surfaceRipplesEnabled", s.surfaceRipplesEnabled},
        {"directWeatherOcclusion", s.directWeatherOcclusion},
    };
}

void applyRenderSettings(const json& j, RenderSettings& s) {
    int pipelineMode = static_cast<int>(s.pipelineMode);
    readInt(j, "pipelineMode", pipelineMode);
    s.pipelineMode = pipelineMode == static_cast<int>(PipelineMode::Forward)
        ? PipelineMode::Forward
        : PipelineMode::Deferred;

    auto applyObject = [&j](const char* key, auto&& apply) {
        auto it = j.find(key);
        if (it != j.end() && it->is_object()) {
            apply(*it);
        }
    };

    applyObject("shadow", [&s](const json& value) { applyShadowSettings(value, s.shadow); });
    applyObject("ssao", [&s](const json& value) { applySsaoSettings(value, s.ssao); });
    applyObject("ssgi", [&s](const json& value) { applySsgiSettings(value, s.ssgi); });
    applyObject("voxelGi", [&s](const json& value) { applyVoxelGiSettings(value, s.voxelGi); });
    applyObject("volumetric", [&s](const json& value) { applyVolumetricSettings(value, s.volumetric); });
    applyObject("cloud", [&s](const json& value) { applyCloudSettings(value, s.cloud); });
    applyObject("reflection", [&s](const json& value) { applyReflectionSettings(value, s.reflection); });
    applyObject("transparent", [&s](const json& value) { applyTransparentSettings(value, s.transparent); });
    applyObject("taa", [&s](const json& value) { applyTaaSettings(value, s.taa); });
    applyObject("postProcess", [&s](const json& value) { applyPostProcessSettings(value, s.postProcess); });
    applyObject("upscale", [&s](const json& value) { applyUpscaleSettings(value, s.upscale); });
    applyObject("debug", [&s](const json& value) { applyDebugSettings(value, s.debug); });
    applyObject("fog", [&s](const json& value) { applyFogSettings(value, s.fog); });
    applyObject("weather", [&s](const json& value) { applyWeatherSettings(value, s.weather); });
}

json toJson(const RenderSettings& s) {
    return {
        {"pipelineMode", static_cast<int>(s.pipelineMode)},
        {"shadow", toJson(s.shadow)},
        {"ssao", toJson(s.ssao)},
        {"ssgi", toJson(s.ssgi)},
        {"voxelGi", toJson(s.voxelGi)},
        {"volumetric", toJson(s.volumetric)},
        {"cloud", toJson(s.cloud)},
        {"reflection", toJson(s.reflection)},
        {"transparent", toJson(s.transparent)},
        {"taa", toJson(s.taa)},
        {"postProcess", toJson(s.postProcess)},
        {"upscale", toJson(s.upscale)},
        {"debug", toJson(s.debug)},
        {"fog", toJson(s.fog)},
        {"weather", toJson(s.weather)},
    };
}

int readRenderDistance(const json& root) {
    int renderDistance = kDefaultRenderDistance;
    auto gameIt = root.find("game");
    if (gameIt != root.end() && gameIt->is_object()) {
        readInt(*gameIt, "renderDistance", renderDistance);
    }
    return std::clamp(renderDistance, kMinRenderDistance, kMaxRenderDistance);
}

} // namespace

namespace app {

AppSettingsData loadSettings() {
    const json root = readSettingsFile();
    AppSettingsData settings;
    settings.renderDistance = readRenderDistance(root);
    settings.renderSettings = loadRenderSettings(settings.renderSettings);
    return settings;
}

int loadRenderDistance() {
    return readRenderDistance(readSettingsFile());
}

RenderSettings loadRenderSettings(const RenderSettings& fallback) {
    RenderSettings settings = fallback;
    const json root = readSettingsFile();
    auto renderIt = root.find("render");
    if (renderIt != root.end() && renderIt->is_object()) {
        applyRenderSettings(*renderIt, settings);
    }
    return settings;
}

bool saveSettings(const AppSettingsData& settings) {
    json root = readSettingsFile();
    json& game = root["game"];
    if (!game.is_object()) {
        game = json::object();
    }
    game["renderDistance"] = std::clamp(settings.renderDistance, kMinRenderDistance, kMaxRenderDistance);
    root["render"] = toJson(settings.renderSettings);
    return writeSettingsFile(root);
}

bool saveRenderDistance(const int renderDistance) {
    json root = readSettingsFile();
    json& game = root["game"];
    if (!game.is_object()) {
        game = json::object();
    }
    game["renderDistance"] = std::clamp(renderDistance, kMinRenderDistance, kMaxRenderDistance);
    return writeSettingsFile(root);
}

bool saveRenderSettings(const RenderSettings& settings) {
    json root = readSettingsFile();
    root["render"] = toJson(settings);
    return writeSettingsFile(root);
}

} // namespace app
