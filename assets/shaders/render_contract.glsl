// Render target contract for the built-in deferred pipeline.
// Provides sky cache metadata access and equirectangular sky projection.
//
// Include this header in any shader that reads from the sky capture texture
// or needs sky illuminance data.

#ifndef MECRAFT_RENDER_CONTRACT_GLSL
#define MECRAFT_RENDER_CONTRACT_GLSL

// Sky capture resolution — must match DeferredRenderTargets.h kSkyCaptureWidth/Height.
// Columns 0..skyCaptureRes.x-2 store equirectangular sky radiance.
// Column skyCaptureRes.x-1 stores metadata texels (illuminance, weather).
const ivec2 skyCaptureRes = ivec2(256, 256);

//----------------------------------------------------------------------------//
// Sky cache metadata texel access
//----------------------------------------------------------------------------//

vec3 sampleSkyMetadata(sampler2D skyCapture, int row) {
    return texelFetch(skyCapture, ivec2(skyCaptureRes.x - 1, row), 0).rgb;
}

// Sun irradiance on horizontal ground plane (lux, physically scaled).
vec3 getDirectIlluminance(sampler2D skyCapture) { return sampleSkyMetadata(skyCapture, 0); }

// Hemisphere-integrated sky irradiance (lux).
vec3 getSkyIlluminance(sampler2D skyCapture)    { return sampleSkyMetadata(skyCapture, 1); }

// Solar disk luminous radiance (cd/m^2).
vec3 getSunIlluminance(sampler2D skyCapture)    { return sampleSkyMetadata(skyCapture, 2); }

// Lunar disk luminous radiance (cd/m^2).
vec3 getMoonIlluminance(sampler2D skyCapture)   { return sampleSkyMetadata(skyCapture, 3); }

//----------------------------------------------------------------------------//
// Equirectangular sky projection
// Simple mapping matching the sky capture shader's UV-to-direction conversion.
// NOTE: DerivativeMain uses a 2-pixel border offset; we skip it here because
// the sky capture and all consumers use the same direct mapping.
//----------------------------------------------------------------------------//

vec2 projectSky(vec3 direction) {
    float phi = atan(direction.x, -direction.z);
    float u = phi * (1.0 / 6.28318530718) + 0.5;
    float v = direction.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 unprojectSky(vec2 uv) {
    float phi = (uv.x - 0.5) * 6.28318530718;
    float cosTheta = uv.y * 2.0 - 1.0;
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return normalize(vec3(sin(phi) * sinTheta, cosTheta, -cos(phi) * sinTheta));
}

// Convenience: sample sky radiance from a world direction.
vec3 sampleSkyRadiance(sampler2D skyCapture, vec3 worldDir) {
    return texture(skyCapture, projectSky(worldDir)).rgb;
}

#endif
