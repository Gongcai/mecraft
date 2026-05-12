// Render target contract for the built-in deferred pipeline.
// Provides sky cache metadata access and equirectangular sky projection
// matching DerivativeMain's Atmosphere.glsl coordinate mapping.
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
// Matches DerivativeMain lib/Atmosphere/Atmosphere.glsl ProjectSky/UnprojectSky.
//----------------------------------------------------------------------------//

vec2 projectSky(vec3 direction) {
    float skyCols = float(skyCaptureRes.x - 1);  // 255
    vec2 coord = vec2(atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5,
                      acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359));
    // 2-pixel border offset to avoid edge artifacts
    coord.x = coord.x * (1.0 - 4.0 / skyCols) + 2.0 / skyCols;
    return clamp(coord * vec2(skyCols, float(skyCaptureRes.y)) / vec2(skyCaptureRes),
                 vec2(0.0), vec2(1.0));
}

vec3 unprojectSky(vec2 uv) {
    float skyCols = float(skyCaptureRes.x - 1);
    vec2 coord = uv * vec2(skyCaptureRes) / vec2(skyCols, float(skyCaptureRes.y));
    coord.x *= skyCols / float(skyCaptureRes.x);
    coord.x = fract((coord.x - 2.0 / skyCols) / (1.0 - 4.0 / skyCols));
    coord *= vec2(6.28318530718, 3.14159265359);
    return vec3(sin(coord.y) * vec2(cos(coord.x), sin(coord.x)), cos(coord.y)).xzy;
}

// Convenience: sample sky radiance from a world direction.
vec3 sampleSkyRadiance(sampler2D skyCapture, vec3 worldDir) {
    return texture(skyCapture, projectSky(worldDir)).rgb;
}

#endif
