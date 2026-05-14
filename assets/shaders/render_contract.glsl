// Render target contract for the built-in deferred pipeline.
// Provides sky cache metadata access and equirectangular sky projection.
//
// Include this header in any shader that reads from the sky capture texture
// or needs sky illuminance data.

#ifndef MECRAFT_RENDER_CONTRACT_GLSL
#define MECRAFT_RENDER_CONTRACT_GLSL

// Sky capture resolution — matches DerivativeMain Settings.glsl skyCaptureRes.
// Texture is 256 wide x 514 tall (skyCaptureRes.x + 1 metadata column, skyCaptureRes.y * 2 + 2 rows).
// Rows 0..skyCaptureRes.y+1 (0..257):  raw atmospheric sky radiance (equirectangular).
// Rows skyCaptureRes.y+2.. (258..513): cloudy skybox (sky + clouds composited).
// Column skyCaptureRes.x, rows 0-5: metadata texels.
const ivec2 skyCaptureRes = ivec2(255, 256);

//----------------------------------------------------------------------------//
// Sky cache metadata texel access
//----------------------------------------------------------------------------//

vec3 sampleSkyMetadata(sampler2D skyCapture, int row) {
    return texelFetch(skyCapture, ivec2(skyCaptureRes.x, row), 0).rgb;
}

// Sun irradiance on horizontal ground plane (lux, physically scaled).
vec3 getDirectIlluminance(sampler2D skyCapture) { return sampleSkyMetadata(skyCapture, 0); }

// Hemisphere-integrated sky irradiance (lux).
vec3 getSkyIlluminance(sampler2D skyCapture)    { return sampleSkyMetadata(skyCapture, 1); }

// Solar disk luminous radiance (cd/m^2).
vec3 getSunIlluminance(sampler2D skyCapture)    { return sampleSkyMetadata(skyCapture, 2); }

// Lunar disk luminous radiance (cd/m^2).
vec3 getMoonIlluminance(sampler2D skyCapture)   { return sampleSkyMetadata(skyCapture, 3); }

// Cloud dynamic weather (cirrocumulus, cirrus, storm factors).
vec3 getCloudDynamicWeather(sampler2D skyCapture) { return sampleSkyMetadata(skyCapture, 5); }

//----------------------------------------------------------------------------//
// Equirectangular sky projection.
// Matches DerivativeMain lib/Atmosphere/Atmosphere.glsl ProjectSky/UnprojectSky:
// longitude in x with a 2px border, polar angle acos(y) in y.
// Texture is 256 x 514 (GL_CLAMP_TO_EDGE):
//   Rows 0..257  (258 rows): raw atmospheric sky radiance.
//   Rows 258..513 (256 rows): cloudy skybox.
// projectSky()       -> v in [0, rawSkyVMax]   maps to raw sky rows.
// projectSkyCloudy() -> v in [rawSkyVMax, 1.0] maps to cloudy sky rows.
//----------------------------------------------------------------------------//

const float rawSkyVMax = float(skyCaptureRes.y + 1) / float(skyCaptureRes.y * 2 + 2); // 258/514

vec2 projectSky(vec3 direction) {
    direction = normalize(direction);
    float u = atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5;
    float v = acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359);
    u = u * (1.0 - 4.0 / float(skyCaptureRes.x)) + 2.0 / float(skyCaptureRes.x);
    v = v * rawSkyVMax;
    return clamp(vec2(u, v), vec2(0.0), vec2(1.0));
}

// Project sky direction into the cloudy sky region (rows 258..513).
// Maps equirectangular v [0,1] into [rawSkyVMax, 1.0].
vec2 projectSkyCloudy(vec3 direction) {
    direction = normalize(direction);
    float u = atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5;
    float v = acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359);
    u = u * (1.0 - 4.0 / float(skyCaptureRes.x)) + 2.0 / float(skyCaptureRes.x);
    v = rawSkyVMax + v * (1.0 - rawSkyVMax);
    return clamp(vec2(u, v), vec2(0.0), vec2(1.0));
}

vec3 unprojectSky(vec2 uv) {
    float u = fract((uv.x - 2.0 / float(skyCaptureRes.x)) / (1.0 - 4.0 / float(skyCaptureRes.x)));
    float phi = u * 6.28318530718;
    float theta = clamp(uv.y / rawSkyVMax, 0.0, 1.0) * 3.14159265359;
    float sinTheta = sin(theta);
    return normalize(vec3(sin(phi) * sinTheta, cos(theta), cos(phi) * sinTheta));
}

// Convenience: sample raw sky radiance from a world direction.
vec3 sampleSkyRadiance(sampler2D skyCapture, vec3 worldDir) {
    return texture(skyCapture, projectSky(worldDir)).rgb;
}

// Convenience: sample cloudy sky radiance (sky + baked clouds) from a world direction.
vec3 sampleSkyRadianceCloudy(sampler2D skyCapture, vec3 worldDir) {
    return texture(skyCapture, projectSkyCloudy(worldDir)).rgb;
}

#endif
