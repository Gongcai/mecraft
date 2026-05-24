// derivative_weather.glsl — Unified DerivativeMain weather contract for Mecraft.
// Single source of truth for all weather-dependent rendering parameters.
// Source: DerivativeMain/lib/Atmosphere/, world0/deferred5.fsh
//
// Prerequisite: consumer must provide saturate() and oneMinus() before including.
//   - deferred_lighting.fs gets them via derivative_shadow.glsl include chain
//   - Other consumers define locally or include derivative_shadow.glsl

#ifndef DERIVATIVE_WEATHER_GLSL
#define DERIVATIVE_WEATHER_GLSL

// ============================================================
// DerivativeMain cloud constants (VolumetricClouds.glsl:2-25)
// ============================================================

// Clear-sky cloud properties
const float CLOUD_CUMULUS_CLEAR_ALTITUDE    = 1000.0;
const float CLOUD_CUMULUS_CLEAR_THICKNESS   = 1400.0;
const float CLOUD_CUMULUS_CLEAR_COVERAGE    = 1.0;
const float CLOUD_CUMULUS_CLEAR_DENSITY     = 1.0;
const float CLOUD_CUMULUS_CLEAR_SUNLIGHTING = 1.0;
const float CLOUD_CUMULUS_CLEAR_SKYLIGHTING = 1.0;

// Rain cloud properties
const float CLOUD_CUMULUS_RAIN_ALTITUDE     = 800.0;
const float CLOUD_CUMULUS_RAIN_THICKNESS    = 3000.0;
const float CLOUD_CUMULUS_RAIN_COVERAGE     = 1.2;
const float CLOUD_CUMULUS_RAIN_DENSITY      = 1.0;
const float CLOUD_CUMULUS_RAIN_SUNLIGHTING  = 0.3;
const float CLOUD_CUMULUS_RAIN_SKYLIGHTING  = 0.3;

// ============================================================
// Cloud properties struct and computation
// DerivativeMain VolumetricClouds.glsl:53-77 GetGlobalCloudProperties()
// ============================================================

struct CloudWeatherProperties {
    float altitude;
    float thickness;
    float coverage;
    float density;
    float sunlighting;
    float skylighting;
    float noiseScale;
    float cloudPeakWeight;
};

CloudWeatherProperties ComputeCloudProperties(float wetness, float stormIntensity) {
    CloudWeatherProperties p;
    p.altitude    = mix(CLOUD_CUMULUS_CLEAR_ALTITUDE,    CLOUD_CUMULUS_RAIN_ALTITUDE,    wetness);
    p.thickness   = mix(CLOUD_CUMULUS_CLEAR_THICKNESS,   CLOUD_CUMULUS_RAIN_THICKNESS,   wetness);
    p.coverage    = mix(CLOUD_CUMULUS_CLEAR_COVERAGE,    CLOUD_CUMULUS_RAIN_COVERAGE,    wetness);
    p.density     = mix(CLOUD_CUMULUS_CLEAR_DENSITY,     CLOUD_CUMULUS_RAIN_DENSITY,     wetness);
    p.sunlighting = mix(CLOUD_CUMULUS_CLEAR_SUNLIGHTING, CLOUD_CUMULUS_RAIN_SUNLIGHTING, wetness);
    p.skylighting = mix(CLOUD_CUMULUS_CLEAR_SKYLIGHTING, CLOUD_CUMULUS_RAIN_SKYLIGHTING, wetness);
    p.noiseScale  = 4e-4 + 6e-5 * wetness;
    p.cloudPeakWeight = 0.1 + 0.7 * wetness;

    // DerivativeMain VolumetricClouds.glsl:57-61: storm intensity non-linear correction.
    // cloudDynamicWeather.z modifies altitude (rises), density (thins), and lighting (slight boost).
    // Thickness is NOT scaled — altitude-only scaling stretches the cloud layer naturally.
    if (stormIntensity > 5e-3) {
        p.altitude    *= 1.0 + stormIntensity * 2.0;
        p.density     *= 1.0 - stormIntensity * 0.3;
        p.sunlighting *= 1.0 + stormIntensity * 0.2;
        p.skylighting *= 1.0 + stormIntensity * 0.2;
    }

    return p;
}

// Planar cloud rain dimming (PlanarClouds.glsl:63,117,247)
// Cirrus and cirrocumulus: *= oneMinus(0.8 * wetness)
float PlanarCloudWetnessFactor(float wetness) {
    return 1.0 - 0.8 * wetness;
}

// Cirrocumulus scattering: *= oneMinus(0.7 * wetness)
float CirrocumulusWetnessFactor(float wetness) {
    return 1.0 - 0.7 * wetness;
}

// ============================================================
// Volumetric fog wetness attenuation
// DerivativeMain VolumetricFog.glsl:310
// ============================================================

// Fog color darkening: at full wetness, fog color is reduced to 20%.
vec3 ApplyFogWetnessDimming(vec3 fogColor, float wetness) {
    return fogColor * (1.0 - 0.8 * wetness);
}

// Fog density boost via TIME_FADE wetness override (VolumetricFog.glsl:211-213)
// At night or during rain, wetness becomes the dominant multiplier.
float ApplyFogDensityWetness(float baseDensity, float timeMidnight, float wetness) {
    return baseDensity * max(timeMidnight * 2.0, wetness);
}

// Air density wetness override (VolumetricFog.glsl:211)
float ApplyAirDensityWetness(float baseDensity, float timeMidnight, float wetness) {
    return baseDensity * max(timeMidnight * 4.0, wetness);
}

// Fog ray length extension with wetness (VolumetricFog.glsl:162,166)
float ExtendFogRayLength(float baseFar, vec3 worldPos, float wetness) {
    return baseFar + wetness * 3e-5 * dot(worldPos.xz, worldPos.xz);
}

// ============================================================
// Skylight weather blend
// DerivativeMain deferred5.fsh:313-314
// ============================================================

// Skylight wet blend: shift toward direct sun light during rain, then dim.
vec3 ApplySkylightWetness(vec3 skylight, vec3 skySunLight, float wetness) {
    // Blend toward direct sun light (DerivativeMain deferred5.fsh:313)
    skylight = mix(skylight, skySunLight, wetness * 0.7);
    // Dim overall skylight (DerivativeMain deferred5.fsh:314)
    skylight *= 0.8 - wetness * 0.2;
    return skylight;
}

// ============================================================
// Direct light occlusion
// DerivativeMain deferred5.fsh:232
// ============================================================

// Default cloud shadow with wetness: at full wetness, only 3% direct light passes.
float ComputeDirectOcclusion(float wetness) {
    return mix(1.0, 0.03, wetness);
}

// ============================================================
// Post-process rain mask
// DerivativeMain Grade.glsl:148-153
// ============================================================

// Rain fog bloom blend: scene darkening + fog bloom addition.
// rain: weather particle alpha * 0.35
// fogBloom: wide atmospheric bloom
// Returns: blended color
vec3 ApplyRainFogBlend(vec3 color, vec3 fogBloom, float rain, float exposure) {
    float fogAmount = clamp(exposure, 0.6, 2.0) * 0.15 + 0.3;
    return color * (1.0 - rain) + fogBloom * fogAmount * rain;
}

#endif // DERIVATIVE_WEATHER_GLSL
