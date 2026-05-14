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
    // SkyCapture metadata (GPU texel values, physically scaled)
    vec3 directIlluminance;  // sun+moon irradiance on horizontal ground (lux)
    vec3 skyIlluminance;     // hemisphere-integrated sky irradiance (lux)
    vec3 sunIlluminance;     // solar disk luminous radiance
    vec3 moonIlluminance;    // lunar disk luminous radiance

    // Sky radiance samples (raw atmospheric, no clouds)
    vec3 skyZenith;          // sampleSkyRadiance(vec3(0,1,0))
    vec3 skyHorizonAvg;      // average of 4 horizontal sky samples

    // Cloudy sky radiance samples (sky + baked clouds)
    vec3 cloudySkyZenith;    // sampleSkyRadianceCloudy(vec3(0,1,0))
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
