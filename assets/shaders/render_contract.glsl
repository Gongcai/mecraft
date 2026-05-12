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
// Equirectangular sky projection.
// Matches DerivativeMain lib/Atmosphere/Atmosphere.glsl ProjectSky/UnprojectSky:
// longitude in x with a 2px border, polar angle acos(y) in y.
//----------------------------------------------------------------------------//

vec2 projectSky(vec3 direction) {
    direction = normalize(direction);
    float u = atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5;
    float v = acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359);
    u = u * (1.0 - 4.0 / float(skyCaptureRes.x)) + 2.0 / float(skyCaptureRes.x);
    return clamp(vec2(u, v), vec2(0.0), vec2(1.0));
}

vec3 unprojectSky(vec2 uv) {
    float u = fract((uv.x - 2.0 / float(skyCaptureRes.x)) / (1.0 - 4.0 / float(skyCaptureRes.x)));
    float phi = u * 6.28318530718;
    float theta = clamp(uv.y, 0.0, 1.0) * 3.14159265359;
    float sinTheta = sin(theta);
    return normalize(vec3(sin(phi) * sinTheta, cos(theta), cos(phi) * sinTheta));
}

// Convenience: sample sky radiance from a world direction.
vec3 sampleSkyRadiance(sampler2D skyCapture, vec3 worldDir) {
    return texture(skyCapture, projectSky(worldDir)).rgb;
}

#endif
