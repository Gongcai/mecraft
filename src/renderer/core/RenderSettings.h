#ifndef MECRAFT_RENDER_SETTINGS_H
#define MECRAFT_RENDER_SETTINGS_H

#include <glm/glm.hpp>

/// Pipeline mode selection
enum class PipelineMode {
    Forward = 0,
    Deferred = 1
};

/// Shadow subsystem settings
struct ShadowSettings {
    bool enabled = true;
    bool softShadowsEnabled = true;
    bool pcssShadowsEnabled = true;
    bool contactShadowsEnabled = false;
    int resolution = 2048;
    float distance = 192.0f;
    float softness = 1.0f;
    float pcssStrength = 0.72f;
    float constantBias = 0.0007f;
    float slopeBias = 0.0022f;
    float normalOffset = 0.035f;
    float contactShadowStrength = 0.12f;
    // R8: Cloud shadow settings moved to CloudSettings to avoid duplication
};

/// SSAO subsystem settings
struct SsaoSettings {
    bool enabled = true;
    bool filterEnabled = true;
    bool temporalEnabled = true;
    float historyWeight = 0.85f;
    float radius = 0.6f;
    float strength = 0.75f;
    int samples = 8;
};

/// Screen-space global illumination settings.
struct SsgiSettings {
    bool enabled = true;
    bool temporalEnabled = true;
    bool denoiseEnabled = true;
    float historyWeight = 0.82f;
    float radius = 5.5f;
    float strength = 1.2f;
    float maxDistance = 16.0f;
    float thickness = 1.5f;
    float denoiseStrength = 0.85f;
    float radianceFilterStrength = 0.55f;
    float colorBleedStrength = 0.35f;
    int samples = 12;
    int denoiseIterations = 2;
};

/// Voxel clipmap global illumination settings.
struct VoxelGiSettings {
    bool enabled = false;
    bool debugEnabled = false;
    int resolution = 64;
    int updateInterval = 4;
    int coneSteps = 6;
    int originSnap = 8;
    float voxelSize = 1.0f;
    float strength = 0.35f;
    float normalBias = 0.45f;
    float sampleDistance = 1.5f;
    float traceDistance = 18.0f;
    float coneAperture = 0.55f;
    float occupancyScale = 0.55f;
    float occlusionStrength = 1.55f;
    float skyBounceStrength = 0.70f;
    float sunBounceStrength = 1.15f;
    float receiverShadowBoost = 0.85f;
};

/// Volumetric fog/light settings
struct VolumetricSettings {
    bool lightEnabled = true;
    bool uwLightEnabled = true;
    bool fogEnabled = true;
    bool skyRayEnabled = true;
    bool timeFadeEnabled = true;
    bool temporalEnabled = true;
    int qualityTier = 1; // 0=Low, 1=Medium, 2=High, 3=Ultra
    int fogSamples = 8;
    int updateInterval = 1; // 1=every frame, 2=half rate, stationary camera only
    float temporalWeight = 0.90f;
    float shadowBiasScale = 1.0f;
    float fogStrength = 1.0f;
    float underwaterLightStrength = 0.1f;
    float fogCenterHeight = 63.0f;
    float fogHeightSpread = 100.0f;
    float fogNoiseScale = 0.04f;
    float fogLightStrength = 0.2f;
    float fogDensityScale = 1.0f;
    float baseDensity = 1.0f;
    float maxDistance = 260.0f;
    // A/B testing
    bool freezeR1 = false;
    bool freezeBias = false;
};

/// Cloud settings
struct CloudSettings {
    bool shadowsEnabled = true;
    int updateInterval = 2; // 1=every frame, 2=half rate, 3=third rate
    float shadowStrength = 0.0f;
    float shadowScale = 0.0045f;
    float shadowSpeed = 0.018f;
    float timeScale = 0.35f;
    float coverage = 0.35f;
    float density = 1.0f;
    float height = 1000.0f;
    float thickness = 1400.0f;
    float planarCoverage = 0.5f;
    float planarDensity = 1.0f;
    float planarAltitude = 7000.0f;
    float sceneCloudCompositeStrength = 0.85f;
};

/// Reflection/SSR settings
struct ReflectionSettings {
    bool filterEnabled = true;
    bool temporalEnabled = true;
    float historyWeight = 0.90f;
    float filterStrength = 1.0f;
    float sceneReflectionCompositeStrength = 1.0f;
};

/// Transparent and water composition settings
struct TransparentSettings {
    bool waterEffectsEnabled = true;
    bool compositeEnabled = true;
};

/// Block material sidecar texture settings.
struct BlockMaterialMapSettings {
    bool enabled = true;
    bool normalMapsEnabled = true;
    bool specularMapsEnabled = true;
    bool parallaxMapsEnabled = true;
    float parallaxDepth = 0.075f;
};

/// TAA settings
struct TaaSettings {
    bool enabled = true;
    float blendMin = 0.08f;
    float blendMax = 0.26f;
    // A/B testing
    bool forceZeroVelocity = false;
    bool freezeJitter = false;
};

/// Post-process settings
struct PostProcessSettings {
    // Bloom
    bool bloomEnabled = true;
    int bloomMipCount = 5;
    float bloomThreshold = 0.0f;
    float bloomStrength = 1.0f;
    bool bloomyFogEnabled = true;

    // Exposure
    bool autoExposureEnabled = true;
    float autoExposureMin = 0.001f;
    float autoExposureMax = 64.0f;
    float autoExposureSpeed = 1.0f;
    float autoExposureBias = 0.0f;
    float exposure = 12.0f;

    // Tonemap
    int tonemapMode = 1; // 0=Reinhard, 1=AcademyFit, 2=Filmic, 3=AgX_Minimal, 4=AcademyFull, 5=AgX_Full

    // Color grading
    float gamma = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float colorTemperature = 1.0f;
    float vibrance = 0.0f;
    float highlightCompression = 0.0f;
    float filmEmulationStrength = 0.0f;
    float redModifierStrength = 0.35f;
    float colorLumaR = 1.02f;
    float colorLumaG = 1.00f;
    float colorLumaB = 0.96f;
    float albedoDesaturation = 0.0f;
    float splitToneStrength = 0.0f;
    float vignetteStrength = 0.0f;

    // Lighting grading
    float sunWarmth = 0.34f;
    float skyCoolness = 0.18f;
    float shadowDesaturation = 0.22f;
    float shadowTintStrength = 0.40f;
    float directSunStrength = 1.0f;
    float skyAmbientStrength = 0.36f;
    float minimumAmbient = 0.055f;
    float shadowMinLight = 0.0f;
    float shadowContrast = 1.0f;
    float blockLightStrength = 1.00f;
    float fakeBounceStrength = 0.06f;

    // Atmosphere
    bool aerialPerspectiveEnabled = true;
    float aerialStrength = 0.65f;
    float horizonScatterStrength = 0.78f;

    // Sharpen
    float sharpenStrength = 0.3f;

    // Dither
    float noiseDitherStrength = 0.015f;

    // Effects
    bool purkinjeShiftEnabled = false;
    bool sunRaysEnabled = false;
    bool shaderpackGradingEnabled = true;
    float sunRayStrength = 0.18f;

    // Motion blur
    bool motionBlurEnabled = false;
    float motionBlurStrength = 1.0f;
    int motionBlurSamples = 8;

    // Depth of field
    bool dofEnabled = false;
    float dofIntensity = 0.15f;
    float dofAperture = 2.8f;
    float dofFocusDistance = 5.0f;
};

/// Spatial upscaling settings.
struct UpscaleSettings {
    bool fsr1Enabled = false;
    float renderScale = 0.77f;
    float sharpness = 0.2f;
};

/// Debug visualization settings
struct DebugSettings {
    int viewMode = 0;
    int lightDebugMode = 0;
    int deferredLightDebugMode = 0;
    int postprocessDebugMode = 0;
    int reflectionDebugMode = 0;
    bool derivativeStrictMode = false;
    bool disableGreedyMeshing = false;
};

/// Fog settings
struct FogSettings {
    bool enabled = true;
    int mode = 0; // 0=linear, 1=exp, 2=exp2 (matches GL fog modes)
    glm::vec3 color = glm::vec3(0.67f, 0.84f, 1.0f);
    float startDistance = 140.0f;
    float endDistance = 260.0f;
    float density = 0.01f;
    bool autoDistanceByRenderDistance = true;
    float autoEndOffsetChunks = -0.5f;
    float autoFadeWidthChunks = 2.0f;
};

/// Weather render settings
struct WeatherRenderSettings {
    float skylightScale = 1.0f;
    float exposureBias = 0.0f;
    float postRainFog = 1.0f;
    float rainAlphaScale = 2.5f;
    bool rainLinesEnabled = true;
    bool particlesEnabled = true;
    bool wetSurfacesEnabled = true;
    bool surfaceRipplesEnabled = true;
    float directWeatherOcclusion = -1.0f;
};

/// Complete render settings — single source of truth for the renderer configuration.
struct RenderSettings {
    PipelineMode pipelineMode = PipelineMode::Deferred;

    // Subsystem settings
    ShadowSettings shadow;
    SsaoSettings ssao;
    SsgiSettings ssgi;
    VoxelGiSettings voxelGi;
    VolumetricSettings volumetric;
    CloudSettings cloud;
    ReflectionSettings reflection;
    TransparentSettings transparent;
    BlockMaterialMapSettings blockMaterialMaps;
    TaaSettings taa;
    PostProcessSettings postProcess;
    UpscaleSettings upscale;
    DebugSettings debug;
    FogSettings fog;
    WeatherRenderSettings weather;
};

#endif // MECRAFT_RENDER_SETTINGS_H
