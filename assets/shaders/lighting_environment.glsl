// Unified lighting environment access for all render passes.
//
// All passes should include this header and use getLightingEnvironment()
// instead of directly calling getDirectIlluminance / sampleSkyRadiance etc.
// This ensures every pass reads from the same SkyCapture metadata and sky
// sampling functions, preventing multi-source drift.
//
// Art-directed tint (sunWarmth, skyCoolness, strength sliders) remains on
// the call side — this header only provides the physical/sky data layer.

#ifndef MECRAFT_LIGHTING_ENVIRONMENT_GLSL
#define MECRAFT_LIGHTING_ENVIRONMENT_GLSL

#include "render_contract.glsl"

struct LightingEnvironment {
    // --- SkyCapture metadata (GPU texel values) ---
    // These are NOT 0..1 colors. They are physically-scaled irradiance/radiance
    // values that must be multiplied by large scale factors before use.
    // See GameplaySkyRenderer::computeSkyIlluminance() for the CPU-side source.

    vec3 directIlluminance;  // sun+moon irradiance on horizontal ground (DerivativeMain units).
                             // Typical day: ~(1.0-2.0, 1.3-2.5, 1.3-2.6). Night: near zero.
                             // Usage: multiply by 64 * strength for direct light energy.

    vec3 skyIlluminance;     // hemisphere-integrated sky irradiance (DerivativeMain units).
                             // Typical day: ~(0.1-0.5). Night: ~(0.01-0.05).
                             // Usage: multiply by directionalBoost * strength for skylight.

    vec3 sunIlluminance;     // solar disk luminous radiance (DerivativeMain units).
                             // NOT a 0..1 color. Typical: ~(1.0-2.0). Night: zero.
                             // Usage: multiply by phase * scale for cloud/fog sun scatter.
                             // WARNING: do NOT use as a direct color replacement for
                             // uSunLightColor without matching the energy scale.

    vec3 moonIlluminance;    // lunar disk luminous radiance. Similar scale to sunIlluminance
                             // but ~0.35x weaker.

    // --- Sky radiance samples (raw atmospheric, no clouds) ---
    // These are sky radiance in scene-linear space, suitable for fog/aerial color.

    vec3 skyZenith;          // raw sky radiance looking straight up.
                             // Usage: fog base color, skylight zenith reference.

    vec3 skyHorizonAvg;      // average of 4 horizontal raw sky samples (N/S/E/W).
                             // Usage: ambient estimation, fog fallback when no SH available.

    // --- Cloudy sky radiance (sky + baked clouds) ---
    vec3 cloudySkyZenith;    // cloudy sky radiance looking straight up.
                             // Usage: sky background, water reflection fallback.
};

// Build a LightingEnvironment from the SkyCapture texture.
// All values come from the GPU sky capture — no CPU art constants.
LightingEnvironment getLightingEnvironment(sampler2D skyCapture) {
    LightingEnvironment env;

    // Metadata from sky capture texels
    env.directIlluminance = max(getDirectIlluminance(skyCapture), vec3(0.0));
    env.skyIlluminance    = max(getSkyIlluminance(skyCapture),    vec3(0.0));
    env.sunIlluminance    = max(getSunIlluminance(skyCapture),    vec3(0.0));
    env.moonIlluminance   = max(getMoonIlluminance(skyCapture),   vec3(0.0));

    // Raw sky radiance samples
    env.skyZenith = sampleSkyRadiance(skyCapture, vec3(0.0, 1.0, 0.0));

    // 4-direction horizontal average for ambient estimation
    vec3 n = sampleSkyRadiance(skyCapture, vec3( 0.0, 0.0, 1.0));
    vec3 s = sampleSkyRadiance(skyCapture, vec3( 0.0, 0.0,-1.0));
    vec3 e = sampleSkyRadiance(skyCapture, vec3( 1.0, 0.0, 0.0));
    vec3 w = sampleSkyRadiance(skyCapture, vec3(-1.0, 0.0, 0.0));
    env.skyHorizonAvg = (n + s + e + w) * 0.25;

    // Cloudy sky samples
    env.cloudySkyZenith = sampleSkyRadianceCloudy(skyCapture, vec3(0.0, 1.0, 0.0));

    return env;
}

// Convenience: sample raw sky radiance for fog/aerial perspective.
vec3 sampleEnvironmentSky(sampler2D skyCapture, vec3 dir) {
    return sampleSkyRadiance(skyCapture, dir);
}

// Convenience: sample cloudy sky for reflections / sky background.
vec3 sampleEnvironmentCloudySky(sampler2D skyCapture, vec3 dir) {
    return sampleSkyRadianceCloudy(skyCapture, dir);
}

#endif
