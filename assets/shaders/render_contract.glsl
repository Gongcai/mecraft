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
// Texture is 256 x 514 (GL_CLAMP_TO_EDGE, GL_LINEAR):
//   Rows 0..257  (258 rows): raw atmospheric sky radiance.
//   Rows 258..513 (256 rows): cloudy skybox.
// UV mapping uses texel-center alignment to avoid boundary bleed:
//   raw sky v:    [0.5/514,  257.5/514]  (first to last texel center)
//   cloudy sky v: [258.5/514, 513.5/514]
// projectSky()       -> v in [rawSkyVMin, rawSkyVMax]
// projectSkyCloudy() -> v in [cloudySkyVMin, cloudySkyVMax]
//----------------------------------------------------------------------------//

const float skyTexHeight = float(skyCaptureRes.y * 2 + 2); // 514
const float halfTexel    = 0.5 / skyTexHeight;             // 0.5/514
const float rawSkyRows   = float(skyCaptureRes.y + 1);     // 258
const float rawSkyVMin   = halfTexel;                       // 0.5/514
const float rawSkyVMax   = (rawSkyRows - 0.5) / skyTexHeight; // 257.5/514
const float cloudySkyVMin = (rawSkyRows + 0.5) / skyTexHeight; // 258.5/514
const float cloudySkyVMax = (skyTexHeight - 0.5) / skyTexHeight; // 513.5/514

vec2 projectSky(vec3 direction) {
    direction = normalize(direction);
    float u = atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5;
    float v = acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359);
    u = u * (1.0 - 4.0 / float(skyCaptureRes.x)) + 2.0 / float(skyCaptureRes.x);
    v = mix(rawSkyVMin, rawSkyVMax, v);
    return clamp(vec2(u, v), vec2(0.0), vec2(1.0));
}

// Project sky direction into the cloudy sky region (rows 258..513).
vec2 projectSkyCloudy(vec3 direction) {
    direction = normalize(direction);
    float u = atan(-direction.x, -direction.z) * (1.0 / 6.28318530718) + 0.5;
    float v = acos(clamp(direction.y, -1.0, 1.0)) * (1.0 / 3.14159265359);
    u = u * (1.0 - 4.0 / float(skyCaptureRes.x)) + 2.0 / float(skyCaptureRes.x);
    v = mix(cloudySkyVMin, cloudySkyVMax, v);
    return clamp(vec2(u, v), vec2(0.0), vec2(1.0));
}

vec3 unprojectSky(vec2 uv) {
    float u = fract((uv.x - 2.0 / float(skyCaptureRes.x)) / (1.0 - 4.0 / float(skyCaptureRes.x)));
    float phi = u * 6.28318530718;
    float vNorm = clamp((uv.y - rawSkyVMin) / (rawSkyVMax - rawSkyVMin), 0.0, 1.0);
    float theta = vNorm * 3.14159265359;
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
