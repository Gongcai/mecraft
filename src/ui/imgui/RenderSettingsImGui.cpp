#include "RenderSettingsImGui.h"

#include "imgui.h"

#include "renderer/core/RenderSettings.h"

namespace render_settings_imgui {

bool showDeferredDebugView(RenderSettings& settings) {
    static constexpr const char* kDebugViewModes[] = {"0: Off",
                                                      "1: GBuffer Albedo",
                                                      "2: GBuffer Normal",
                                                      "3: GBuffer Vertex AO",
                                                      "4: Voxel Light",
                                                      "5: Material Rough/F0/Emission",
                                                      "6: Material SSS",
                                                      "7: Depth",
                                                      "8: Shadow Depth",
                                                      "9: SSAO",
                                                      "10: Scene Lighting",
                                                      "11: Scene Composite",
                                                      "12: Transparent Composite",
                                                      "13: Transparent Depth",
                                                      "14: Volumetric RGB",
                                                      "15: Volumetric Transmittance",
                                                      "16: Sky Capture",
                                                      "17: Velocity",
                                                      "18: History Scene",
                                                      "19: History Depth",
                                                      "20: Shadow Projection",
                                                      "21: Shadow Visibility",
                                                      "22: Shadow Bias",
                                                      "23: CSM Cascade",
                                                      "24: Reflection Target",
                                                      "25: Cloud Target",
                                                      "26: Material Kind",
                                                      "27: Material Aux",
                                                      "28: Reflection History",
                                                      "29: Cloud History",
                                                      "30: Reflection Filter Metric",
                                                      "31: Scene Resolved",
                                                      "32: Shadow UV",
                                                      "33: Shadow Density",
                                                      "34: Shadow Depth Compare",
                                                      "35: Shadow Hit Caster",
                                                      "36: CSM Depth 0",
                                                      "37: CSM Depth 1",
                                                      "38: CSM Depth 2",
                                                      "39: CSM Depth 3",
                                                      "40: Cascade Info",
                                                      "41: Sky Dir Raw",
                                                      "42: Sky Dir Cloudy",
                                                      "43: Sky Dir Raw x20",
                                                      "44: SkyCapture Atlas + Metadata",
                                                      "45: Lighting Balance",
                                                      "46: VFog Density",
                                                      "47: VFog Transmittance",
                                                      "48: VFog Sky Only",
                                                      "49: VFog Sun Only",
                                                      "50: VFog Sun Gates",
                                                      "51: VFog Integration",
                                                      "52: VFog Sky Ray Coverage",
                                                      "53: VFog March Detail",
                                                      "54: VFog Sun Contrast",
                                                      "55: VFog Sun Only x20",
                                                      "56: VFog Sun Only x100",
                                                      "57: VFog Shadow Visibility",
                                                      "58: VFog Shadow Raw vs PCF",
                                                      "59: VFog Shadow Projection",
                                                      "60: VFog Shadow Compare",
                                                      "61: VFog Bias Compare",
                                                      "62: VFog Cascade Index",
                                                      "63: VFog Receiver Depth",
                                                      "64: VFog Sun/Sky Ratio",
                                                      "65: VFog Beam Modulation",
                                                      "66: VFog Density Field",
                                                      "67: TAA Current Scratch",
                                                      "68: TAA Current-History Delta",
                                                      "69: Velocity Sky Highlight",
                                                      "70: Raw Half VFog",
                                                      "71: Upscaled VFog",
                                                      "72: UW VL Scatter",
                                                      "73: UW VL Shadow",
                                                      "74: UW VL Phase",
                                                      "75: Shadow Depth Gap",
                                                      "76: Shadow Color0",
                                                      "77: Shadow Color1",
                                                      "78: Reflection Composite Delta x32",
                                                      "79: TAA Loss x32",
                                                      "80: TAA Wet Reject Mask",
                                                      "81: SSGI",
                                                      "82: SSGI x8",
                                                      "83: SSGI Confidence",
                                                      "84: Reactive Mask",
                                                      "85: Transparency Mask",
                                                      "86: RGB F0",
                                                      "87: Stable Object ID",
                                                      "88: Stable Material ID"};
    int debugViewMode = settings.debug.viewMode;
    const bool changed =
        ImGui::Combo("Deferred Debug View", &debugViewMode, kDebugViewModes, IM_ARRAYSIZE(kDebugViewModes));
    settings.debug.viewMode = debugViewMode;
    return changed;
}

bool showReflectionSettings(RenderSettings& settings) {
    static constexpr const char* kReflectionDebugModes[] = {"0: Off",
                                                            "1: Pixel Wetness",
                                                            "2: Reflectance",
                                                            "3: SSR Hit Confidence",
                                                            "4: Roughness",
                                                            "5: Specular Weight x8",
                                                            "6: Composite Delta",
                                                            "7: Puddle Mask",
                                                            "8: Rain Splash Mask",
                                                            "9: Rain Ripple Normal",
                                                            "10: Rain Ripple Strength",
                                                            "11: F0 x8",
                                                            "12: Sky Environment",
                                                            "13: Reflection RGB x8",
                                                            "14: Reflective Material Mask",
                                                            "15: Sky Light Raw",
                                                            "16: Voxel Light RG",
                                                            "17: Material Aux",
                                                            "18: Sky Gradient x64",
                                                            "19: Final Contribution",
                                                            "20: Reflection Source",
                                                            "21: Reflectance x32",
                                                            "22: F0 x32",
                                                            "23: Roughness",
                                                            "24: Reflection Source x8",
                                                            "25: Final Contribution x32",
                                                            "26: Reflection / Scene Ratio",
                                                            "27: Scene Luminance",
                                                            "28: Reflection Luminance x64",
                                                            "29: Reflectance x128",
                                                            "30: Source Gradient x128",
                                                            "31: Sky IBL Mip",
                                                            "32: Sky IBL DFG",
                                                            "33: Reflection Probe ID",
                                                            "34: Reflection Probe Weight"};

    bool changed = false;
    changed |= ImGui::Checkbox("Bilateral Filter", &settings.reflection.filterEnabled);
    changed |= ImGui::Checkbox("Temporal Accumulation", &settings.reflection.temporalEnabled);
    changed |= ImGui::SliderFloat("Filter Strength", &settings.reflection.filterStrength, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("History Weight", &settings.reflection.historyWeight, 0.0f, 0.98f, "%.2f");
    changed |= ImGui::SliderFloat("Scene Composite", &settings.reflection.sceneReflectionCompositeStrength, 0.0f, 1.0f,
                                  "%.2f");
    changed |= ImGui::Combo("Reflection Debug", &settings.debug.reflectionDebugMode, kReflectionDebugModes,
                            IM_ARRAYSIZE(kReflectionDebugModes));
    return changed;
}

bool showShadowSettings(RenderSettings& settings) {
    bool changed = false;
    changed |= ImGui::Checkbox("Sun Shadows", &settings.shadow.enabled);
    changed |= ImGui::Checkbox("Soft Shadows", &settings.shadow.softShadowsEnabled);
    changed |= ImGui::Checkbox("PCSS Shadows", &settings.shadow.pcssShadowsEnabled);
    changed |= ImGui::Checkbox("Interleaved Far Cascades", &settings.shadow.farCascadeInterleaved);
    changed |= ImGui::Checkbox("GPU Cascade Culling", &settings.shadow.gpuCascadeCullEnabled);
    changed |= ImGui::Checkbox("Contact Shadows", &settings.shadow.contactShadowsEnabled);
    changed |= ImGui::SliderInt("Shadow Resolution", &settings.shadow.resolution, 512, 4096);
    changed |= ImGui::SliderFloat("Shadow Distance", &settings.shadow.distance, 64.0f, 192.0f, "%.1f");
    changed |= ImGui::SliderFloat("Shadow Softness", &settings.shadow.softness, 0.1f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("PCSS Strength", &settings.shadow.pcssStrength, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Shadow Constant Bias", &settings.shadow.constantBias, 0.0f, 0.004f, "%.4f");
    changed |= ImGui::SliderFloat("Shadow Slope Bias", &settings.shadow.slopeBias, 0.0f, 0.012f, "%.4f");
    changed |= ImGui::SliderFloat("Shadow Normal Offset", &settings.shadow.normalOffset, 0.0f, 0.12f, "%.3f");
    changed |=
        ImGui::SliderFloat("Contact Shadow Strength", &settings.shadow.contactShadowStrength, 0.0f, 0.6f, "%.2f");
    changed |= ImGui::Checkbox("Cloud Shadows", &settings.cloud.shadowsEnabled);
    changed |= ImGui::SliderFloat("Cloud Shadow Strength", &settings.cloud.shadowStrength, 0.0f, 0.8f, "%.2f");
    changed |= ImGui::SliderFloat("Cloud Shadow Scale", &settings.cloud.shadowScale, 0.001f, 0.02f, "%.4f");
    return changed;
}

bool showVolumetricSettings(RenderSettings& settings) {
    static constexpr const char* kQualityTiers[] = {"Low: No Noise", "Medium: Cloudy Fog Lite", "High: Cloudy Fog",
                                                    "Ultra: Cloudy Sea"};

    bool changed = false;
    changed |= ImGui::Checkbox("Volumetric Light", &settings.volumetric.lightEnabled);
    changed |= ImGui::Checkbox("Volumetric Fog", &settings.volumetric.fogEnabled);
    changed |= ImGui::Checkbox("Sky Ray March", &settings.volumetric.skyRayEnabled);
    changed |= ImGui::Checkbox("Time Fade", &settings.volumetric.timeFadeEnabled);
    changed |= ImGui::Checkbox("Temporal Accumulation", &settings.volumetric.temporalEnabled);
    changed |= ImGui::Checkbox("Underwater Volumetric Light", &settings.volumetric.uwLightEnabled);
    changed |=
        ImGui::Combo("Fog Quality", &settings.volumetric.qualityTier, kQualityTiers, IM_ARRAYSIZE(kQualityTiers));
    changed |= ImGui::SliderInt("Fog Samples", &settings.volumetric.fogSamples, 2, 50);
    changed |= ImGui::SliderInt("Update Frames", &settings.volumetric.updateInterval, 1, 4);
    changed |= ImGui::SliderFloat("Temporal Weight", &settings.volumetric.temporalWeight, 0.0f, 0.99f, "%.2f");
    changed |= ImGui::SliderFloat("Fog Strength", &settings.volumetric.fogStrength, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Fog Center Height", &settings.volumetric.fogCenterHeight, 0.0f, 255.0f, "%.0f");
    changed |= ImGui::SliderFloat("Fog Height Spread", &settings.volumetric.fogHeightSpread, 1.0f, 200.0f, "%.0f");
    changed |= ImGui::SliderFloat("Fog Noise Scale", &settings.volumetric.fogNoiseScale, 0.001f, 0.2f, "%.3f");
    changed |= ImGui::SliderFloat("Fog Light Strength", &settings.volumetric.fogLightStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Fog Density Scale", &settings.volumetric.fogDensityScale, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::SliderFloat("Underwater Light Strength", &settings.volumetric.underwaterLightStrength, 0.0f, 2.0f,
                                  "%.2f");
    changed |= ImGui::SliderFloat("Volumetric Shadow Bias", &settings.volumetric.shadowBiasScale, 0.0f, 4.0f, "%.2f");
    return changed;
}

bool showSsaoSettings(RenderSettings& settings) {
    bool changed = false;
    changed |= ImGui::Checkbox("SSAO", &settings.ssao.enabled);
    changed |= ImGui::Checkbox("SSAO Temporal", &settings.ssao.temporalEnabled);
    changed |= ImGui::Checkbox("SSAO Filter", &settings.ssao.filterEnabled);
    changed |= ImGui::Checkbox("SSAO Async Compute", &settings.ssao.asyncComputeEnabled);
    changed |= ImGui::SliderFloat("SSAO Radius", &settings.ssao.radius, 0.25f, 8.0f, "%.2f");
    changed |= ImGui::SliderFloat("SSAO Strength", &settings.ssao.strength, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderInt("SSAO Samples", &settings.ssao.samples, 1, 64);
    changed |= ImGui::SliderFloat("SSAO History Weight", &settings.ssao.historyWeight, 0.0f, 0.98f, "%.2f");
    return changed;
}

bool showSsgiSettings(RenderSettings& settings) {
    bool changed = false;
    changed |= ImGui::Checkbox("SSGI", &settings.ssgi.enabled);
    changed |= ImGui::Checkbox("SSGI Temporal", &settings.ssgi.temporalEnabled);
    changed |= ImGui::Checkbox("SSGI Denoise", &settings.ssgi.denoiseEnabled);
    changed |= ImGui::SliderFloat("SSGI Radius", &settings.ssgi.radius, 0.5f, 24.0f, "%.2f");
    changed |= ImGui::SliderFloat("SSGI Strength", &settings.ssgi.strength, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::SliderInt("SSGI Samples", &settings.ssgi.samples, 1, 32);
    changed |= ImGui::SliderFloat("SSGI Max Distance", &settings.ssgi.maxDistance, 1.0f, 48.0f, "%.1f");
    changed |= ImGui::SliderFloat("SSGI Thickness", &settings.ssgi.thickness, 0.1f, 8.0f, "%.2f");
    changed |= ImGui::SliderInt("SSGI Denoise Passes", &settings.ssgi.denoiseIterations, 0, 4);
    changed |= ImGui::SliderFloat("SSGI Denoise Strength", &settings.ssgi.denoiseStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("SSGI Radiance Filter", &settings.ssgi.radianceFilterStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("SSGI Color Bleed", &settings.ssgi.colorBleedStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("SSGI History Weight", &settings.ssgi.historyWeight, 0.0f, 0.98f, "%.2f");
    return changed;
}

bool showPictureAdjustments(RenderSettings& settings) {
    bool changed = false;
    if (settings.postProcess.autoExposureEnabled) {
        ImGui::BeginDisabled();
    }
    changed |= ImGui::SliderFloat("Manual Exposure Value", &settings.postProcess.exposure, 0.1f, 50.0f, "%.2f",
                                  ImGuiSliderFlags_Logarithmic);
    if (settings.postProcess.autoExposureEnabled) {
        ImGui::EndDisabled();
    }
    changed |= ImGui::SliderFloat("Gamma", &settings.postProcess.gamma, 1.0f, 3.0f, "%.2f");
    changed |= ImGui::SliderFloat("Saturation", &settings.postProcess.saturation, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Contrast", &settings.postProcess.contrast, 0.5f, 2.0f, "%.2f");
    return changed;
}

bool showPostProcessSettings(RenderSettings& settings) {
    static constexpr const char* kTonemapModes[] = {"Reinhard",    "Academy Fit",  "Filmic",
                                                    "AgX Minimal", "Academy Full", "AgX Full"};

    bool changed = false;
    changed |= ImGui::Checkbox("Auto Exposure", &settings.postProcess.autoExposureEnabled);
    changed |= ImGui::SliderFloat("Auto Exposure Minimum", &settings.postProcess.autoExposureMin, 0.001f, 1.0f, "%.3f",
                                  ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Auto Exposure Maximum", &settings.postProcess.autoExposureMax, 1.0f, 64.0f, "%.2f",
                                  ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Auto Exposure Speed", &settings.postProcess.autoExposureSpeed, 0.1f, 6.0f, "%.2f");
    changed |= ImGui::SliderFloat("Auto Exposure Bias", &settings.postProcess.autoExposureBias, -2.0f, 2.0f, "%.2f EV");
    changed |= showPictureAdjustments(settings);
    changed |=
        ImGui::Combo("Tonemap Mode", &settings.postProcess.tonemapMode, kTonemapModes, IM_ARRAYSIZE(kTonemapModes));

    changed |= ImGui::Checkbox("Bloom", &settings.postProcess.bloomEnabled);
    changed |= ImGui::SliderInt("Bloom Mips", &settings.postProcess.bloomMipCount, 1, 7);
    changed |= ImGui::SliderFloat("Bloom Threshold", &settings.postProcess.bloomThreshold, 0.0f, 3.0f, "%.2f");
    changed |= ImGui::SliderFloat("Bloom Strength", &settings.postProcess.bloomStrength, 0.0f, 20.0f, "%.2f");
    changed |= ImGui::Checkbox("Bloomy Fog", &settings.postProcess.bloomyFogEnabled);
    changed |= ImGui::Checkbox("Sun Rays", &settings.postProcess.sunRaysEnabled);
    changed |= ImGui::SliderFloat("Sun Ray Strength", &settings.postProcess.sunRayStrength, 0.0f, 0.6f, "%.2f");
    changed |= ImGui::Checkbox("Aerial Perspective", &settings.postProcess.aerialPerspectiveEnabled);
    changed |= ImGui::Checkbox("Shaderpack Grading", &settings.postProcess.shaderpackGradingEnabled);
    changed |= ImGui::Checkbox("Purkinje Shift", &settings.postProcess.purkinjeShiftEnabled);

    changed |= ImGui::SliderFloat("Color Temperature", &settings.postProcess.colorTemperature, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Vibrance", &settings.postProcess.vibrance, -0.5f, 0.8f, "%.2f");
    changed |=
        ImGui::SliderFloat("Highlight Compression", &settings.postProcess.highlightCompression, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Film Emulation", &settings.postProcess.filmEmulationStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Red Modifier", &settings.postProcess.redModifierStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Channel R", &settings.postProcess.colorLumaR, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Channel G", &settings.postProcess.colorLumaG, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Channel B", &settings.postProcess.colorLumaB, 0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Albedo Desaturation", &settings.postProcess.albedoDesaturation, 0.0f, 0.8f, "%.2f");
    changed |= ImGui::SliderFloat("Split Tone", &settings.postProcess.splitToneStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Vignette", &settings.postProcess.vignetteStrength, 0.0f, 0.5f, "%.2f");

    changed |= ImGui::SliderFloat("Sun Warmth", &settings.postProcess.sunWarmth, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Sky Coolness", &settings.postProcess.skyCoolness, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Shadow Desaturation", &settings.postProcess.shadowDesaturation, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Shadow Tint", &settings.postProcess.shadowTintStrength, 0.0f, 0.8f, "%.2f");
    changed |= ImGui::SliderFloat("Direct Sun", &settings.postProcess.directSunStrength, 0.0f, 3.0f, "%.2f");
    changed |= ImGui::SliderFloat("Sky Ambient", &settings.postProcess.skyAmbientStrength, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Minimum Ambient", &settings.postProcess.minimumAmbient, 0.0f, 0.4f, "%.2f");
    changed |= ImGui::SliderFloat("Shadow Minimum Light", &settings.postProcess.shadowMinLight, 0.0f, 0.5f, "%.2f");
    changed |= ImGui::SliderFloat("Shadow Contrast", &settings.postProcess.shadowContrast, 0.5f, 2.5f, "%.2f");
    changed |= ImGui::SliderFloat("Block Light", &settings.postProcess.blockLightStrength, 0.0f, 2.5f, "%.2f");
    changed |= ImGui::SliderFloat("Fake Bounce", &settings.postProcess.fakeBounceStrength, 0.0f, 0.3f, "%.2f");
    changed |= ImGui::SliderFloat("Aerial Strength", &settings.postProcess.aerialStrength, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Horizon Scatter", &settings.postProcess.horizonScatterStrength, 0.0f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Noise Dither", &settings.postProcess.noiseDitherStrength, 0.0f, 0.05f, "%.3f");
    changed |= ImGui::SliderFloat("CAS Sharpen", &settings.postProcess.sharpenStrength, 0.0f, 0.5f, "%.2f");

    changed |= ImGui::Checkbox("Motion Blur", &settings.postProcess.motionBlurEnabled);
    changed |= ImGui::SliderFloat("Motion Blur Strength", &settings.postProcess.motionBlurStrength, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderInt("Motion Blur Samples", &settings.postProcess.motionBlurSamples, 1, 32);
    changed |= ImGui::Checkbox("Depth of Field", &settings.postProcess.dofEnabled);
    changed |= ImGui::SliderFloat("DoF Focus Distance", &settings.postProcess.dofFocusDistance, 0.5f, 50.0f, "%.1f");
    changed |= ImGui::SliderFloat("DoF Aperture", &settings.postProcess.dofAperture, 0.8f, 22.0f, "%.1f");
    changed |= ImGui::SliderFloat("DoF Intensity", &settings.postProcess.dofIntensity, 0.0f, 1.0f, "%.3f");
    return changed;
}

} // namespace render_settings_imgui
