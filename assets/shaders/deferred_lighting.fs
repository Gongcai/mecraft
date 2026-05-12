#version 450 core
#include "gbuffer_contract.glsl"
#include "render_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uDepthTex;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uShadowMap;
uniform sampler2D uSsaoTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uNoiseTex;

uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform mat4 uProjection;
uniform mat4 uShadowViewProj;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform int uAerialPerspectiveEnabled;
uniform float uShadowTintStrength;
uniform float uDirectSunStrength;
uniform float uSkyAmbientStrength;
uniform float uMinimumAmbient;
uniform float uShadowMinLight;
uniform float uShadowContrast;
uniform float uBlockLightStrength;
uniform float uFakeBounceStrength;
uniform float uAlbedoDesaturation;
uniform float uSunWarmth;
uniform float uSkyCoolness;
uniform float uShadowDesaturation;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uWeatherMist;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uAerialReduction;
uniform int uShadowsEnabled;
uniform int uSoftShadowsEnabled;
uniform int uPcssShadowsEnabled;
uniform int uContactShadowsEnabled;
uniform int uCloudShadowsEnabled;
uniform int uShadowWarpMode;
uniform int uShadowLightMode;
uniform float uShadowDistance;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowSoftness;
uniform float uShadowPcssStrength;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uShadowNormalOffset;
uniform float uContactShadowStrength;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudShadowSpeed;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform float uTime;
uniform int uSsaoEnabled;
uniform int uHeldBlockLightValue;
uniform int uHeldBlockLightValue2;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

// Shadow color/normal textures (DerivativeMain shadowcolor0/1 equivalent)
uniform sampler2D uShadowColorTex;
uniform sampler2D uShadowNormalTex;

// Atmosphere precomputed scattering LUT (256x128x33 RGBA32F)
uniform sampler3D uAtmosphereLut;

const float kTwoPi = 6.28318530718;
const float kPi = 3.14159265359;

float cube(float x) { return x * x * x; }
float sqr(float x) { return x * x; }

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 decodeOctNormal(vec2 encoded) {
    vec2 f = encoded * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

vec3 desaturateLinear(vec3 color, float amount) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(color, vec3(luma), clamp(amount, 0.0, 1.0));
}

// --- DerivativeMain BRDF.glsl faithful port ---

float fresnelSchlick(float cosTheta, float f0) {
    float f = pow(1.0 - cosTheta, 5.0);
    return clamp(f + (1.0 - f) * f0, 0.0, 1.0);
}

float fresnelDielectric(float cosTheta, float f0) {
    f0 = min(sqrt(f0), 0.99999);
    f0 = (1.0 + f0) / (1.0 - f0);
    float cosTheta2 = cosTheta * cosTheta;
    float sinTheta2 = 1.0 - cosTheta2;
    float t1 = sqrt(max(f0 * f0 - sinTheta2, 0.0));
    float t2 = f0 * f0 * cosTheta2;
    float rs = (f0 * cosTheta - t1) / max(f0 * cosTheta + t1, 1e-8);
    float rp = (t2 - f0 * t1) / max(t2 + f0 * t1, 1e-8);
    return clamp((rs * rs + rp * rp) * 0.5, 0.0, 1.0);
}

float v1SmithGGXInverse(float cosTheta, float alpha2) {
    return cosTheta + sqrt(max((cosTheta - alpha2 * cosTheta) * cosTheta + alpha2, 0.0));
}

float v2SmithGGX(float NdotV, float NdotL, float alpha2) {
    float ggxl = NdotL * sqrt(max((NdotV - alpha2 * NdotV) * NdotV + alpha2, 0.0));
    float ggxv = NdotV * sqrt(max((NdotL - alpha2 * NdotL) * NdotL + alpha2, 0.0));
    return 0.5 / max(ggxl + ggxv, 1e-7);
}

float distributionGGX(float NdotH, float alpha2) {
    return alpha2 / max(kPi * sqr(1.0 + (NdotH * alpha2 - NdotH) * NdotH), 1e-7);
}

vec3 diffuseHammon(float LdotV, float NdotV, float NdotL, float NdotH, float roughness, vec3 albedo) {
    if (NdotL < 1e-6) return vec3(0.0);
    float facing = max(LdotV, 0.0) * 0.5 + 0.5;
    float singleSmooth = 1.05 * (1.0 - pow(1.0 - NdotL, 5.0)) * (1.0 - pow(1.0 - NdotV, 5.0));
    float singleRough = facing * (0.45 - 0.2 * facing) * (1.0 / max(NdotH, 1e-4) + 2.0);
    float single = mix(singleSmooth, singleRough, roughness) / kPi;
    float multi = 0.1159 * roughness;
    return (multi * albedo + single) * NdotL;
}

float specularBRDF(float LdotH, float NdotV, float NdotL, float NdotH, float alpha2, float f0) {
    if (NdotL < 1e-5) return 0.0;
    float F = fresnelSchlick(LdotH, f0);
    float D = distributionGGX(NdotH, alpha2);
    float V = v2SmithGGX(max(NdotV, 1e-2), max(NdotL, 1e-2), alpha2);
    return min(NdotL * D * V * F, 4.0);
}

// Planckian locus blackbody — matches DerivativeMain's Common.inc Blackbody().
// Computes CIE xy chromaticity from temperature, converts to sRGB, normalizes.
vec3 blackbodyApprox(float t) {
    t = clamp(t, 1000.0, 15000.0);
    float it = 1.0 / t;
    float it2 = it * it;
    vec4 vx = vec4(-0.2661239e9, -0.2343580e6, 0.8776956e3, 0.179910);
    vec4 vy = vec4(-1.1063814, -1.34811020, 2.18555832, -0.20219683);
    float x = dot(vx, vec4(it * it2, it2, it, 1.0));
    float x2 = x * x;
    float y = dot(vy, vec4(x * x2, x2, x, 1.0));
    mat3 xyzToSrgb = mat3(
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252);
    vec3 srgb = vec3(x / y, 1.0, (1.0 - x - y) / y) * xyzToSrgb;
    srgb = max(srgb, vec3(0.0));
    return srgb / max(min(srgb.r, min(srgb.g, srgb.b)), 0.001);
}

vec3 artisticSunIlluminance(vec3 sunColor, vec3 sunDir) {
    float elevation = clamp(sunDir.y, 0.0, 1.0);
    vec3 noonWarmth = vec3(1.10, 1.00, 0.84);
    vec3 lowSunWarmth = vec3(1.38, 0.82, 0.42);
    vec3 tint = mix(lowSunWarmth, noonWarmth, smoothstep(0.08, 0.62, elevation));
    float energy = mix(1.35, 1.08, smoothstep(0.04, 0.70, elevation));
    return max(sunColor * tint * energy, vec3(0.0));
}

// Sky sampling uses projectSky() from render_contract.glsl (included via atmosphere_lut.glsl)
vec3 sampleSkyCapture(vec3 dir) {
    return sampleSkyRadiance(uSkyCaptureTex, dir);
}

vec3 sampleSkyIrradiance(vec3 normal) {
    normal = normalize(normal);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 north = normalize(vec3(0.0, 0.45, -1.0));
    vec3 south = normalize(vec3(0.0, 0.45, 1.0));
    vec3 east = normalize(vec3(1.0, 0.45, 0.0));
    vec3 west = normalize(vec3(-1.0, 0.45, 0.0));

    float wUp = 0.34 + 0.34 * max(dot(normal, up), 0.0);
    float wNorth = 0.16 + 0.20 * max(dot(normal, north), 0.0);
    float wSouth = 0.16 + 0.20 * max(dot(normal, south), 0.0);
    float wEast = 0.16 + 0.20 * max(dot(normal, east), 0.0);
    float wWest = 0.16 + 0.20 * max(dot(normal, west), 0.0);
    float weightSum = wUp + wNorth + wSouth + wEast + wWest;

    vec3 irradiance = sampleSkyCapture(up) * wUp;
    irradiance += sampleSkyCapture(north) * wNorth;
    irradiance += sampleSkyCapture(south) * wSouth;
    irradiance += sampleSkyCapture(east) * wEast;
    irradiance += sampleSkyCapture(west) * wWest;
    return irradiance / max(weightSum, 0.0001);
}

float computeFogFactor(float fogDistance) {
    if (uFogMode == 1) {
        return clamp(exp(-uFogDensity * fogDistance), 0.0, 1.0);
    }
    if (uFogMode == 2) {
        float d = uFogDensity * fogDistance;
        return clamp(exp(-(d * d)), 0.0, 1.0);
    }
    float linearRange = max(uFogEnd - uFogStart, 0.0001);
    return clamp((uFogEnd - fogDistance) / linearRange, 0.0, 1.0);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

vec2 projectWorldToUv(vec3 worldPos, out float ndcDepth) {
    vec4 clip = uViewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 0.00001);
    ndcDepth = ndc.z * 0.5 + 0.5;
    return ndc.xy * 0.5 + 0.5;
}

float compareShadowTexelAt(vec3 proj, ivec2 texelCoord, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    if (texelCoord.x < 0 || texelCoord.y < 0 || texelCoord.x >= size.x || texelCoord.y >= size.y) {
        return 1.0;
    }
    float closest = texelFetch(uShadowMap, texelCoord, 0).r;
    return (proj.z - bias <= closest) ? 1.0 : 0.0;
}

float sampleShadowDepthAt(vec3 proj, vec2 offsetTexels) {
    ivec2 size = textureSize(uShadowMap, 0);
    vec2 uv = proj.xy + offsetTexels / vec2(size);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        return 1.0;
    }
    return texture(uShadowMap, uv).r;
}

float compareShadowTexel(vec3 proj, ivec2 offset, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    ivec2 texelCoord = ivec2(floor(proj.xy * vec2(size))) + offset;
    return compareShadowTexelAt(proj, texelCoord, bias);
}

float compareShadowBilinear(vec3 proj, vec2 offsetTexels, float bias) {
    ivec2 size = textureSize(uShadowMap, 0);
    vec2 texelPos = proj.xy * vec2(size) - vec2(0.5) + offsetTexels;
    ivec2 base = ivec2(floor(texelPos));
    vec2 f = fract(texelPos);

    float s00 = compareShadowTexelAt(proj, base, bias);
    float s10 = compareShadowTexelAt(proj, base + ivec2(1, 0), bias);
    float s01 = compareShadowTexelAt(proj, base + ivec2(0, 1), bias);
    float s11 = compareShadowTexelAt(proj, base + ivec2(1, 1), bias);

    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

float noise2D(vec2 uv) {
    return texture(uNoiseTex, uv).r;
}

// DerivativeMain-style dither: per-pixel blue noise rotation for shadow sampling.
float shadowDither() {
    return noise2D(gl_FragCoord.xy / 256.0);
}

// DerivativeMain rotating Poisson disk: 45-degree increments via 2x2 rotation matrix.
// cossin(angle) returns vec2(cos, sin).
vec2 cossin(float angle) {
    return vec2(cos(angle), sin(angle));
}

// DerivativeMain quartic shadow distortion (ShadowDistortion.glsl)
float quarticLength(vec2 v) {
    vec2 v2 = v * v;
    return pow(dot(v2, v2), 0.25);
}

float shadowDistortionFactor(vec2 shadowClipPos) {
    const float SHADOW_MAP_BIAS = 0.9;
    return quarticLength(shadowClipPos * 1.165) * SHADOW_MAP_BIAS + 1.0 - SHADOW_MAP_BIAS;
}

float cloudShadowFactor(vec3 worldPos, vec3 lightDir, float outdoorMask) {
    if (uCloudShadowsEnabled == 0 || uCloudShadowStrength <= 0.001 || outdoorMask <= 0.001) {
        return 1.0;
    }

    lightDir = normalize(lightDir);
    float layerHeight = max(uCloudHeight, 1.0);
    float denom = max(abs(lightDir.y), 0.18);
    float t = (layerHeight - worldPos.y) / denom;
    vec2 cloudPos = (worldPos.xz + lightDir.xz * t) * max(uCloudShadowScale, 0.0001);
    vec2 wind = vec2(0.73, 0.31) * uTime * uCloudShadowSpeed;

    float large = noise2D(cloudPos + wind);
    float medium = noise2D(cloudPos * 2.37 - wind * 1.7);
    float coverageThreshold = mix(0.72, 0.42, clamp(uCloudCoverage, 0.0, 1.0));
    float coverage = smoothstep(coverageThreshold, coverageThreshold + 0.24, large * 0.72 + medium * 0.28);
    float weatherBoost = clamp(uWeatherMist * 0.25 + uWeatherWetness * 0.32 + uWeatherStorm * 0.58, 0.0, 0.65);
    float strength = uCloudShadowStrength * outdoorMask * max(uCloudDensity, 0.0) * (1.0 + weatherBoost);
    return 1.0 - coverage * clamp(strength, 0.0, 0.85);
}

vec2 spiralDiskSample(int index, int sampleCount, float jitter) {
    float fi = float(index) + jitter;
    float radius = sqrt((float(index) + 0.5) / max(float(sampleCount), 1.0));
    float angle = fi * 2.39996323 + jitter * kTwoPi;
    return vec2(cos(angle), sin(angle)) * radius;
}

float calculateShadowWarp(vec2 coord) {
    if (uShadowWarpMode == 2) {
        return 1.0;
    }
    if (uShadowWarpMode == 1) {
        return shadowDistortionFactor(coord);
    }
    return length(coord * 1.169) * 0.9 + 0.1;
}

vec3 worldToShadowProj(vec3 worldPos, out float warpDensity) {
    vec4 lightView = uShadowModelView * vec4(worldPos, 1.0);
    vec4 lightClip = uShadowProjection * lightView;
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    if (uShadowWarpMode != 2) {
        warpDensity = calculateShadowWarp(proj.xy);
        proj.xy /= warpDensity;
        proj.z *= 0.2;
    } else {
        warpDensity = 1.0;
    }
    return proj * 0.5 + 0.5;
}

float shapeShadowVisibility(float lit) {
    lit = clamp(lit, 0.0, 1.0);
    float contrasted = pow(lit, max(uShadowContrast, 0.001));
    return mix(clamp(uShadowMinLight, 0.0, 0.8), 1.0, contrasted);
}

float shadowProjectionFade(vec3 proj) {
    vec2 edgeDistance = min(proj.xy, vec2(1.0) - proj.xy);
    float texelUv = 1.0 / max(float(textureSize(uShadowMap, 0).x), 1.0);
    float edgeFade = smoothstep(texelUv * 8.0, texelUv * 36.0, min(edgeDistance.x, edgeDistance.y));
    float nearFade = smoothstep(0.002, 0.016, proj.z);
    float farFade = 1.0 - smoothstep(0.965, 0.998, proj.z);
    return clamp(edgeFade * nearFade * farFade, 0.0, 1.0);
}

float shadowDepthWorldScale() {
    float scale = max(abs(uShadowProjectionInverse[2][2]) * 2.0, 1.0);
    return (uShadowWarpMode != 2) ? scale / 0.2 : scale;
}

float shadowDepthBiasFromWorld(float worldUnits) {
    return worldUnits / shadowDepthWorldScale();
}

float derivativeMinimumShadowBias() {
    return (uShadowWarpMode != 2) ? 1.0e-4 : 6.0e-5;
}

float shadowWorldBias(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float slope = 1.0 - clamp(ndotl, 0.0, 1.0);
    float receiverScale = 1.0 + 0.35 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    return texelWorld * receiverScale * (0.35 + uShadowConstantBias * 18.0 + uShadowSlopeBias * 18.0 * slope);
}

float shadowNormalOffsetWorld(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float grazing = 1.0 - clamp(ndotl, 0.0, 1.0);
    float distanceScale = 1.0 + 0.35 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    float requestedTexels = max(uShadowNormalOffset, 0.0) / 0.09375;
    float texelOffset = texelWorld * requestedTexels * distanceScale * (1.0 + 0.85 * grazing);

    // DerivativeMain offsets receivers by a small world-space amount before shadow projection:
    // normal * (dist^2 * 8e-5 + 3e-2) * (2 - NdotL). Keep the same floor so higher
    // shadow resolutions do not shrink the receiver offset back into shadow acne.
    float derivativeScale = max(uShadowNormalOffset, 0.0) / 0.035;
    float derivativeOffset = (viewDistance * viewDistance * 8e-5 + 3e-2) *
                             (2.0 - clamp(ndotl, 0.0, 1.0)) *
                             derivativeScale;
    return max(texelOffset, derivativeOffset);
}

vec3 sampleShadowColorTint(vec3 worldPos, vec3 normal, vec3 lightDir, float shadowVisibility) {
    if (uShadowsEnabled == 0 || uShadowTintStrength <= 0.001) {
        return vec3(1.0);
    }

    float viewDistanceForBias = length(worldPos - uCameraPos);
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float normalOffset = shadowNormalOffsetWorld(ndotl, viewDistanceForBias);
    float warpDensity = 1.0;
    vec3 proj = worldToShadowProj(worldPos + normal * normalOffset, warpDensity);
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return vec3(1.0);
    }

    vec3 shadowColor = texture(uShadowColorTex, proj.xy).rgb;
    vec3 shadowNormal = decodeOctNormal(texture(uShadowNormalTex, proj.xy).rg);
    float tintStrength = clamp(uShadowTintStrength, 0.0, 1.0) * (1.0 - shadowVisibility);
    float normalMatch = clamp(dot(shadowNormal, normal) * 0.5 + 0.5, 0.0, 1.0);
    vec3 desaturatedShadow = mix(shadowColor, vec3(dot(shadowColor, vec3(0.2126, 0.7152, 0.0722))), 0.35);
    return mix(vec3(1.0), desaturatedShadow, tintStrength * normalMatch * 0.42);
}

// DerivativeMain BlockerSearch (SunLighting.glsl:28-54)
// Uses texelFetch (integer coordinates, no filtering) — matches DerivativeMain exactly.
// Returns: .x = penumbra scale (shadow map space), .y = SSS depth (world space)
vec2 blockerSearch(vec3 shadowProjPos, float dither) {
    float searchDepth = 0.0;
    float sumWeight = 0.0;
    float sssDepth = 0.0;

    float searchRadius = 2.0 * uShadowProjection[0][0];
    ivec2 shadowSize = textureSize(uShadowMap, 0);
    vec2 shadowRes = vec2(shadowSize);

    vec2 rot = cossin(dither * kTwoPi) * searchRadius;
    vec2 angleStep = cossin(kTwoPi * 0.125);
    mat2 rotStep = mat2(angleStep.x, angleStep.y, -angleStep.y, angleStep.x);

    for (int i = 0; i < 8; ++i, rot *= rotStep) {
        float fi = float(i) + dither;
        vec2 sampleCoord = shadowProjPos.xy + rot * sqrt(fi * 0.125);
        if (sampleCoord.x <= 0.0 || sampleCoord.y <= 0.0 || sampleCoord.x >= 1.0 || sampleCoord.y >= 1.0) {
            continue;
        }

        // texelFetch: integer texel coordinates, no filtering (matches DerivativeMain)
        ivec2 texelCoord = clamp(ivec2(sampleCoord * shadowRes), ivec2(0), shadowSize - ivec2(1));
        float depthSample = texelFetch(uShadowMap, texelCoord, 0).r;
        float weight = step(depthSample, shadowProjPos.z) * (1.0 - step(0.999999, depthSample));

        sssDepth += max(shadowProjPos.z - depthSample, 0.0);
        searchDepth += depthSample * weight;
        sumWeight += weight;
    }

    if (sumWeight <= 0.0) {
        return vec2(0.0);
    }

    searchDepth /= sumWeight;
    searchDepth = min(2.0 * (shadowProjPos.z - searchDepth) / max(searchDepth, 0.0001), 1.0);

    float sssWorldDepth = sssDepth * shadowDepthWorldScale();
    return vec2(searchDepth * uShadowProjection[0][0], sssWorldDepth);
}

// DerivativeMain PercentageCloserFilter (SunLighting.glsl:56-84)
// Rotating Poisson disk PCF with 16 samples.
float pcfFilter(vec3 shadowProjPos, float penumbraScale, float dither, int sampleCount) {
    // DerivativeMain bias: constant 1e-4 minus dithered offset
    shadowProjPos.z -= 1e-4 - dither * 5e-5;

    float rSteps = 1.0 / float(sampleCount);
    float shadowRes = float(textureSize(uShadowMap, 0).x);
    float result = 0.0;

    // CRITICAL: initial rot radius = penumbraScale (DerivativeMain line 64)
    // penumbraScale is in UV space; rot samples are in UV space.
    vec2 rot = cossin(dither * kTwoPi) * penumbraScale;
    vec2 angleStep = cossin(kTwoPi * 0.125);
    mat2 rotStep = mat2(angleStep.x, angleStep.y, -angleStep.y, angleStep.x);

    for (int i = 0; i < sampleCount; ++i, rot *= rotStep) {
        float fi = float(i) + dither;
        vec2 offsetUV = rot * sqrt(fi * rSteps);
        vec2 sampleUv = shadowProjPos.xy + offsetUV;
        if (sampleUv.x <= 0.0 || sampleUv.y <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y >= 1.0) {
            result += 1.0;
        } else {
            result += compareShadowBilinear(shadowProjPos, offsetUV * shadowRes, 0.0);
        }
    }

    return result * rSteps;
}

// DerivativeMain ScreenSpaceShadow (SunLighting.glsl:88-125)
// 12-step screen-space ray march along light direction with SSS absorption.
// DerivativeMain ScreenSpaceShadow (SunLighting.glsl:88-125)
// 12-step screen-space ray march along projected light direction.
// worldPos: fragment world position
// screenUv: fragment screen UV (0-1)
// sceneDepth: fragment screen depth (0-1, from depth buffer)
// dither: per-pixel noise for temporal stability
// sssAmount: subsurface scattering amount (0 = opaque, 1 = full SSS)
float screenSpaceShadow(vec3 worldPos, vec2 screenUv, float sceneDepth, float dither, float sssAmount) {
    if (uContactShadowsEnabled == 0) return 1.0;

    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    vec3 ndcPos = clipPos.xyz / max(abs(clipPos.w), 0.0001);
    vec3 shadowLightDir = (uShadowLightMode == 1) ? normalize(uMoonDirection) : normalize(uSunDirection);
    vec3 lightWorldOffset = worldPos + shadowLightDir * abs(clipPos.w) * 0.1;
    vec4 clipOffset = uViewProj * vec4(lightWorldOffset, 1.0);
    vec3 ndcOffset = clipOffset.xyz / max(abs(clipOffset.w), 0.0001);
    vec3 screenDir = normalize(vec3((ndcOffset.xy - ndcPos.xy) * 0.5, ndcOffset.z - ndcPos.z));

    float stepSize = max(0.01, 0.05 - sssAmount * 0.05) * uProjection[1][1] / 12.0;
    vec3 rayStep = screenDir * stepSize;

    vec3 rayPos = vec3(screenUv, sceneDepth) + rayStep * (1.0 - sssAmount + dither);
    float shadow = 1.0;
    const float zTolerance = 0.025;
    float absorption = (sssAmount > 1e-4)
        ? pow(clamp(sssAmount, 0.001, 1.0), sqrt(length(worldPos - uCameraPos)) * 0.5)
        : 0.0;

    for (int i = 0; i < 12; ++i) {
        rayPos += rayStep;
        if (rayPos.x < 0.0 || rayPos.y < 0.0 || rayPos.x > 1.0 || rayPos.y > 1.0 || rayPos.z >= 1.0) break;

        float sampleDepth = texture(uDepthTex, rayPos.xy).r;
        vec3 sampleWorld = reconstructWorldPosition(rayPos.xy, sampleDepth);
        vec3 currentWorld = reconstructWorldPosition(rayPos.xy, rayPos.z);
        float linearSample = length(sampleWorld - uCameraPos);
        float linearCurrent = length(currentWorld - uCameraPos);

        if (abs(linearSample - linearCurrent) / max(linearCurrent, 1e-6) < zTolerance &&
            sampleDepth < rayPos.z) {
            shadow *= absorption;
            if (shadow < 1e-2) break;
        }
    }
    return mix(1.0, shadow, clamp(uContactShadowStrength, 0.0, 1.0));
}

// DerivativeMain shadow pipeline (deferred5.fsh:240-288)
// Returns shadow value, outputs SSS depth for subsurface scattering.
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir, float sssAmount, out float outSssDepth) {
    outSssDepth = 0.0;
    if (uShadowsEnabled == 0) return 1.0;

    lightDir = normalize(lightDir);
    float viewDistanceForBias = length(worldPos - uCameraPos);

    float distance01 = clamp(viewDistanceForBias / max(uShadowDistance, 1.0), 0.0, 1.0);
    float distanceFade = pow(distance01, 16.0);
    if (distanceFade >= 0.999) return 1.0;

    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    if (ndotl <= 1e-4) return 1.0;

    // Normal offset (DerivativeMain: normal * (dist² * 8e-5 + 3e-2) * (2 - NdotL))
    float normalOffset = (viewDistanceForBias * viewDistanceForBias * 8e-5 + 3e-2) *
                         (2.0 - ndotl) * max(uShadowNormalOffset, 0.0) / 0.035;
    float warpDensity = 1.0;
    vec3 proj = worldToShadowProj(worldPos + normal * normalOffset, warpDensity);
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) return 1.0;

    float projectionFade = shadowProjectionFade(proj);
    if (projectionFade <= 0.001) return 1.0;

    float bias = max(
        shadowDepthBiasFromWorld(shadowWorldBias(ndotl, viewDistanceForBias)),
        derivativeMinimumShadowBias()
    );
    proj.z -= bias;

    if (uSoftShadowsEnabled == 0) {
        float lit = (proj.z <= texture(uShadowMap, proj.xy).r) ? 1.0 : 0.0;
        float shaped = shapeShadowVisibility(lit);
        return mix(1.0, shaped, projectionFade * (1.0 - distanceFade));
    }

    float dither = shadowDither();
    vec2 blocker = blockerSearch(proj, dither);
    outSssDepth = blocker.y;

    int samples = uPcssShadowsEnabled != 0 ? 16 : 8;
    float minRadius = 2.0 / max(float(textureSize(uShadowMap, 0).x), 1.0);
    float pcfRadius = minRadius * max(uShadowSoftness, 0.1);
    if (uPcssShadowsEnabled != 0) {
        pcfRadius = max(blocker.x / max(warpDensity, 0.1), minRadius) *
                    max(1.0, uShadowSoftness) *
                    max(uShadowPcssStrength, 0.35);
    }

    float lit = pcfFilter(proj, pcfRadius, dither, samples);
    lit *= screenSpaceShadow(worldPos, vTexCoord, texture(uDepthTex, vTexCoord).r, dither, sssAmount);
    float shaped = shapeShadowVisibility(lit);
    return mix(1.0, shaped, projectionFade * (1.0 - distanceFade));
}

// Overload without SSS depth output for callers that don't need it
float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    float unused;
    return shadowFactor(worldPos, normal, lightDir, 0.0, unused);
}

vec3 aerialFogColor(vec3 baseFogColor, vec3 viewDir, float horizon, vec3 warmSunColor) {
    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
    float nightFactor = 1.0 - dayFactor;
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float moonVisibility = clamp(uMoonVisibility, 0.0, 1.0);

    vec3 captureDir = normalize(vec3(viewDir.x, viewDir.y * 0.32, viewDir.z));
    vec3 capturedFog = sampleSkyCapture(captureDir);
    vec3 skyFog = mix(capturedFog, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0));
    vec3 fogColor = mix(baseFogColor, skyFog, 0.34 + 0.18 * nightFactor);

    float sunForwardWide = pow(max(dot(viewDir, sunDir), 0.0), 5.0);
    float sunForwardCore = pow(max(dot(viewDir, sunDir), 0.0), 36.0);
    float moonForward = pow(max(dot(viewDir, moonDir), 0.0), 8.0);
    fogColor += warmSunColor * (sunForwardWide * 0.14 + sunForwardCore * 0.22) *
                sunVisibility * clamp(uHorizonScatterStrength, 0.0, 2.0);
    fogColor += uMoonLightColor * moonForward * moonVisibility * nightFactor *
                (0.10 + 0.10 * clamp(uHorizonScatterStrength, 0.0, 2.0));
    return max(fogColor, vec3(0.0));
}

vec3 applyAerialPerspective(vec3 sceneColor,
                            vec3 worldPos,
                            float fogDistance,
                            float outdoorSkyMask,
                            vec3 warmSunColor) {
    vec3 viewDir = normalize(worldPos - uCameraPos);
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.55);
    float distanceTransmittance = computeFogFactor(fogDistance);
    float distanceFogOpacity = 1.0 - distanceTransmittance;

    vec3 baseFogColor = srgbToLinear(uFogColor);
    if (uAerialPerspectiveEnabled == 0) {
        return mix(baseFogColor, sceneColor, distanceTransmittance);
    }

    float outdoorMask = smoothstep(0.05, 0.65, outdoorSkyMask);
    float heightDensity = (1.0 - smoothstep(96.0, 220.0, worldPos.y)) * (0.68 + 0.42 * horizon);
    float weatherHaze = 0.55 * uWeatherMist + 0.35 * uWeatherWetness + 0.65 * uWeatherStorm;
    float clearAirScale = mix(clamp(uAerialReduction, 0.0, 1.0), 0.82, clamp(weatherHaze, 0.0, 1.0));
    float airDensity = (0.00048 + 0.00105 * horizon) *
                       clamp(uAerialStrength, 0.0, 2.0) *
                       clearAirScale *
                       (1.0 + weatherHaze * 0.85);
    float aerialOpacity = (1.0 - exp(-fogDistance * airDensity)) * outdoorMask * heightDensity;
    float fogOpacity = clamp(max(distanceFogOpacity * mix(0.55, 0.95, clamp(weatherHaze, 0.0, 1.0)),
                                 aerialOpacity * 0.48),
                             0.0,
                             0.88);

    vec3 fogColor = aerialFogColor(baseFogColor, viewDir, horizon, warmSunColor);
    return mix(sceneColor, fogColor, fogOpacity);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 1.0) {
        discard;
    }

    GBufferSurface surface = unpackGBufferSurface(texture(uAlbedoTex, vTexCoord),
                                                  texture(uNormalAoTex, vTexCoord),
                                                  texture(uVoxelLightTex, vTexCoord),
                                                  texture(uMaterialTex, vTexCoord),
                                                  texture(uMaterialAuxTex, vTexCoord));
    vec3 albedo = surface.albedo;
    albedo = desaturateLinear(albedo, uAlbedoDesaturation);
    float emissiveHint = surface.emissiveHint;
    vec3 normal = surface.normal;
    float vertexAo = surface.vertexAo;
    vec2 voxelLight = surface.voxelLight;
    float roughness = surface.material.roughness;
    float f0Scalar = surface.material.f0;
    float materialEmission = surface.material.emission;
    float sss = surface.material.sss;
    int materialKind = materialKindId(surface.aux.materialKind);
    TranslucentMask transMask = decodeTranslucentMask(surface.aux.materialKind);

    // Wetness/porosity only apply to opaque surfaces.
    float wetness = clamp(uWeatherWetness * surface.aux.wetnessMask * voxelLight.r, 0.0, 1.0);
    float wetPorosity = wetness * clamp(surface.aux.porosity, 0.0, 1.0);
    if (!transMask.isTranslucent) {
        albedo *= 1.0 - wetPorosity * 0.22;
        roughness = mix(roughness, max(0.08, roughness * 0.36), wetness * (0.72 + surface.aux.metalness * 0.18));
        f0Scalar = mix(f0Scalar, max(f0Scalar, 0.055), wetness * (0.35 + surface.aux.metalness * 0.20));
    }
    bool hasDerivativeSpecular = (max(0.625 - roughness, 0.0) + surface.aux.metalness > 0.005) ||
                                 transMask.isTranslucent;
    float derivativeSpecularMask = hasDerivativeSpecular ? 1.0 : 0.0;
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);

    vec2 lightmapUV = vec2(voxelLight.g, 1.0 - voxelLight.r);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 vanillaLight = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
    // DerivativeMain treats the sky lightmap channel as sky visibility. Day/night
    // energy comes from directIlluminance/skyIlluminance in the sky cache.
    float skyLightMask = clamp(voxelLight.r, 0.0, 1.0);
    float nightSkyMask = clamp(voxelLight.r * uMoonVisibility, 0.0, 1.0);
    float outdoorSkyMask = max(skyLightMask, nightSkyMask);
    float blockLightMask = clamp(voxelLight.g, 0.0, 1.0);

    // ===== DerivativeMain-aligned lighting flow =====
    // Reference: deferred5.fsh main() + SunLighting.glsl + BlockLighting.glsl + BRDF.glsl

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    vec3 viewDir = normalize(uCameraPos - worldPos);

    // Dot products using fast halfway-vector trick (DerivativeMain deferred5.fsh:213-227)
    float rawNdotL = dot(normal, sunDir);
    float rawNdotM = dot(normal, moonDir);
    float LdotV = dot(sunDir, viewDir);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(rawNdotL, 0.0);
    float NdotM = max(rawNdotM, 0.0);
    // Fast halfway vector: avoids normalize(sunDir + viewDir) per-pixel
    float halfwayNorm = inversesqrt(2.0 * LdotV + 2.0);
    float NdotH = max((rawNdotL + dot(normal, viewDir)) * halfwayNorm, 0.0);
    float LdotH = max((LdotV + 1.0) * halfwayNorm, 0.0);

    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    vec3 shadowLightDir = (uShadowLightMode == 1) ? moonDir : sunDir;
    float shadowSssDepth = 0.0;
    float shadow = shadowFactor(worldPos, normal, shadowLightDir, sss, shadowSssDepth);
    float cloudShadow = cloudShadowFactor(worldPos, shadowLightDir, outdoorSkyMask);
    float sunShadow = (uShadowLightMode == 0) ? shadow : 1.0;
    float moonShadow = (uShadowLightMode == 1) ? mix(1.0, shadow, 0.82) : 1.0;

    // --- Illuminance from sky capture (DerivativeMain directIlluminance/skyIlluminance) ---
    vec3 cacheDirectLux = getDirectIlluminance(uSkyCaptureTex);
    vec3 cacheSkyLux = getSkyIlluminance(uSkyCaptureTex);
    vec3 directIlluminance = max(cacheDirectLux, vec3(0.0));
    vec3 skyIlluminance = max(cacheSkyLux, vec3(0.0));

    vec3 warmSunColor = artisticSunIlluminance(uSunLightColor, sunDir);
    warmSunColor = mix(warmSunColor, warmSunColor * vec3(1.16, 1.03, 0.78), clamp(uSunWarmth, 0.0, 1.5) * 0.65);
    vec3 shadowTint = sampleShadowColorTint(worldPos, normal, shadowLightDir, shadow);

    // --- BRDF (DerivativeMain BRDF.glsl) ---
    float alpha = max(roughness * roughness, 0.002);
    float alpha2 = alpha * alpha;
    float f0ScalarClamped = max(f0Scalar, 0.005);
    // Diffuse Hammon (derivativeMain's full implementation)
    vec3 diffuse = diffuseHammon(LdotV, NdotV, NdotL, NdotH, roughness, albedo);
    // Specular BRDF: GGX NDF + height-correlated Smith G2 + Schlick Fresnel, clamped to 4.0
    float specRaw = specularBRDF(LdotH, NdotV, NdotL, NdotH, alpha2, f0ScalarClamped);
    vec3 specular = vec3(specRaw) * mix(vec3(1.0), albedo, surface.aux.metalness);

    // === DerivativeMain lighting order ===
    // 1. Sunlight setup: 64 * waterTint * SUNLIGHT_INTENSITY * directIlluminance * cloudShadow
    vec3 sunlightMult = directIlluminance * 64.0 * uDirectSunStrength * cloudShadow * shadowTint;
    // 2. SSS (DerivativeMain SunLighting.glsl:176-188)
    float sssSunShadowFill = 0.0;
    if (sss > 1e-4) {
        float sssDepth = max(shadowSssDepth, (1.0 - sunShadow) * 0.35);
        float coeff = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
        coeff = inversesqrt(coeff + 1e-6);
        vec3 sssCoeff = albedo * coeff * (1.0 - 0.75 * clamp(albedo * coeff, 0.0, 1.0));
        float hg1 = (1.0 - 0.5 * 0.6 * 0.6) / (4.0 * kPi * pow(1.0 + 0.6 * 0.6 - 2.0 * 0.6 * (-LdotV), 1.5));
        float hg2 = (1.0 - 0.5 * 0.35 * 0.35) / (4.0 * kPi * pow(1.0 + 0.35 * 0.35 - 2.0 * 0.35 * (-LdotV), 1.5));
        vec3 sssContrib = albedo * inversesqrt(max(dot(albedo, vec3(0.2126, 0.7152, 0.0722)), 1e-6));
        sssContrib = sssContrib * (1.0 - 0.75 * clamp(sssContrib, 0.0, 1.0)) * (28.0 / max(sss, 1e-4));
        sssContrib = exp(-sssContrib * sssDepth * 0.375) * hg1
                   + exp(-sssContrib * sssDepth * 0.125) * (0.33 * hg2 + 0.17 / kPi);
        sssContrib *= sss * kPi;
        sssSunShadowFill = sss * smoothstep(-0.35, 0.18, -rawNdotL) * mix(0.18, 0.46, 1.0 - sunShadow) * cloudShadow;
    }

    // 3. sceneData accumulation (DerivativeMain deferred5.fsh:290-357)
    // Start with basic + night vision
    vec3 sceneData = vec3(0.018) + vec3(0.1) * 0.0; // BASIC_BRIGHTNESS (nightVision=0 for now)

    // Apply shadow to diffuse, add SSS
    sceneData += sunShadow * diffuse * sunlightMult + sssSunShadowFill * sunlightMult;

    // Skylight (DerivativeMain deferred5.fsh:305-323)
    vec3 coolSkyColor = mix(uSkyAmbientColor, uSkyAmbientColor * vec3(0.78, 0.92, 1.18), clamp(uSkyCoolness, 0.0, 1.0));
    vec3 capturedZenith = sampleSkyCapture(vec3(0.0, 1.0, 0.0));
    vec3 capturedNormalSky = sampleSkyIrradiance(normal);
    float skyCaptureInfluence = mix(0.18, 0.46, 1.0 - clamp(uSkyIntensity, 0.0, 1.0));
    coolSkyColor = mix(coolSkyColor, mix(capturedZenith, capturedNormalSky, 0.55), skyCaptureInfluence);

    float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skylight = coolSkyColor * skyIlluminance * (0.026 + 0.54 * outdoorSkyMask) *
                    uSkyAmbientStrength * mix(0.48, 1.0, upward) * shadowTint;
    float moonMask = nightSkyMask;
    skylight += uMoonLightColor * moonMask * (0.030 + 0.060 * uSkyAmbientStrength);
    skylight *= mix(vec3(1.0), uShadowTintColor, (1.0 - shadow) * clamp(uShadowTintStrength, 0.0, 1.0) * 0.72);
    sceneData += skylight * voxelLight.r * 1.5; // SKYLIGHT_INTENSITY

    // Minimum ambient + fake bounce
    float minimumAmbientMask = mix(0.35, 1.0, outdoorSkyMask);
    sceneData += uShadowTintColor * uMinimumAmbient * minimumAmbientMask * 0.62;
    float groundFacing = clamp(dot(normal, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
    sceneData += directIlluminance * 64.0 * uFakeBounceStrength * pow(skyLightMask, 4.0) *
                 (0.28 + 0.58 * groundFacing) * (0.35 + 0.65 * (1.0 - sunShadow));

    // GI / AO (DerivativeMain deferred5.fsh:329-347)
    // AO multiplies accumulated diffuse+skylight (before blocklight)
    sceneData *= ssao * vertexAo;

    // === Block lighting (DerivativeMain BlockLighting.glsl inline) ===
    vec3 blocklightColor = blackbodyApprox(3000.0);
    float albedoLuminance = length(albedo);
    vec3 albedoRawApprox = pow(max(albedo, vec3(0.0)), vec3(1.0 / 2.2));
    float lightSourceMask = 1.0;

    // Per-materialID emission (BlockLighting.glsl:15-89)
    vec3 emissionColor = vec3(0.0);
    if (materialKind == MATERIAL_TOTAL_GLOWING || materialKind == MATERIAL_TEXTURED_EMISSIVE) {
        emissionColor = vec3(albedoLuminance);
        lightSourceMask = 0.1;
    } else if (materialKind == MATERIAL_TORCH_LIKE) {
        emissionColor = 4.0 * blocklightColor * float(albedoRawApprox.r > 0.8 || albedoRawApprox.r > albedoRawApprox.g * 1.4);
        lightSourceMask = 0.15;
    } else if (materialKind == MATERIAL_FIRE || materialKind == MATERIAL_LAVA) {
        emissionColor = 6.0 * blocklightColor * cube(albedoLuminance);
        lightSourceMask = 0.1;
    } else if (materialKind == MATERIAL_GLOWSTONE_LIKE) {
        emissionColor = 2.5 * blocklightColor * cube(albedoLuminance);
        lightSourceMask = 0.15;
    } else if (materialKind == MATERIAL_SEA_LANTERN_LIKE) {
        emissionColor = vec3(2.0 * cube(albedoLuminance));
        lightSourceMask = 0.0;
    } else if (materialKind == MATERIAL_REDSTONE) {
        emissionColor = vec3(2.1, 0.9, 0.9) * step(0.65, albedoRawApprox.r);
    } else if (materialKind == MATERIAL_SOUL_FIRE) {
        emissionColor = vec3(albedoLuminance + 0.6) * step(0.53, albedoRawApprox.b);
        lightSourceMask = 0.5;
    } else if (materialKind == MATERIAL_AMETHYST) {
        emissionColor = vec3(min(blockLightMask * 200.0 + 0.05, 2.0) *
                             pow(albedoLuminance, min(blockLightMask * 100.0, 2.5)));
    } else if (materialKind == MATERIAL_GLOWBERRY) {
        emissionColor = clamp(dot(clamp(albedo - 0.1, 0.0, 1.0), vec3(1.0, -0.6, -0.99)), 0.0, 1.0) *
                        vec3(28.0, 25.0, 21.0);
        lightSourceMask = 0.4;
    } else if (materialKind == MATERIAL_RAILS) {
        emissionColor = vec3(2.1, 0.9, 0.9) * albedoLuminance *
                        step(albedoRawApprox.g * 2.0 + albedoRawApprox.b, albedoRawApprox.r);
    } else if (materialKind == MATERIAL_BEACON_CORE) {
        vec3 midBlockPos = abs(fract(worldPos) - 0.5);
        emissionColor = vec3(step(max(max(midBlockPos.x, midBlockPos.y), midBlockPos.z), 0.4) *
                             step(0.5, albedo.b) * 6.0 * albedoLuminance);
        lightSourceMask = 0.2;
    } else if (materialKind == MATERIAL_SCULK) {
        emissionColor = vec3(0.04 * sqr(albedoLuminance) *
                             float((albedoRawApprox.b * 2.0 > albedoRawApprox.r + albedoRawApprox.g) &&
                                   albedoRawApprox.b > 0.55));
    } else if (materialKind == MATERIAL_GLOW_LICHEN) {
        emissionColor = vec3(albedoRawApprox.r > albedoRawApprox.b * 1.2 ? 3.0 : albedoLuminance * 0.1);
    } else if (materialKind == MATERIAL_PARTIAL_GLOWING) {
        vec3 partialGlow = clamp(albedo - 0.5, 0.0, 1.0);
        emissionColor = 30.0 * albedoLuminance * partialGlow * partialGlow * partialGlow;
        lightSourceMask = 0.5;
    } else if (materialKind == MATERIAL_MIDDLE_GLOWING) {
        vec2 midBlockPosXZ = abs(fract(worldPos.xz) - 0.5);
        emissionColor = vec3(step(max(midBlockPosXZ.x, midBlockPosXZ.y), 0.063) * albedoLuminance);
    }
    // Emissive ores (BlockLighting.glsl:100-109)
    if (materialKind == MATERIAL_ORE) {
        float isOre = clamp((max(max(dot(albedoRawApprox, vec3(2.0, -1.0, -1.0)),
                                     dot(albedoRawApprox, vec3(-1.0, 2.0, -1.0))),
                                 dot(albedoRawApprox, vec3(-1.0, -1.0, 2.0))) - 0.1) / 0.3, 0.0, 1.0);
        emissionColor += pow(max(albedoRawApprox - vec3(0.1), vec3(0.0)), vec3(5.0)) * isOre * 2.0;
    }
    if (materialKind == MATERIAL_NETHER_ORE) {
        float isNetherOre = clamp(dot(albedoRawApprox, vec3(-20.0, 30.0, 10.0)), 0.0, 1.0);
        vec3 netherOreGlow = max(albedoRawApprox - vec3(0.1), vec3(0.0));
        emissionColor += netherOreGlow * netherOreGlow * netherOreGlow * isNetherOre * 2.0;
    }
    // Emission from material system
    if (materialEmission > 0.01) {
        emissionColor += albedo * materialEmission * 1.5;
    }
    // Add emission to sceneData
    if (max(max(emissionColor.r, emissionColor.g), emissionColor.b) > 0.0) {
        sceneData += emissionColor * uBlockLightStrength;
    }

    // Blocklight falloff (BlockLighting.glsl:111-115, Overworld)
    sceneData += blockLightMask * (ssao * (1.0 - blockLightMask) + blockLightMask) *
                 2.0 * blocklightColor * uBlockLightStrength * lightSourceMask;

    // Held torchlight (BlockLighting.glsl:117-128, Overworld)
    int heldLightMax = max(uHeldBlockLightValue, uHeldBlockLightValue2);
    if (heldLightMax > 0) {
        vec3 heldPos = worldPos - uCameraPos;
        float heldFalloff = 1.0 / (dot(heldPos, heldPos) + 1.0);
        heldFalloff *= NdotV * 0.8 + 0.2;
        sceneData += heldFalloff * 0.2 * float(heldLightMax) * blocklightColor * uBlockLightStrength;
    }

    // Hardcoded material-specific additions (BlockLighting.glsl:130)
    sceneData += float(materialKind == 12) * 12.0 +
                 float(materialKind == 36) * 2.0 +
                 float(materialKind == 19) * albedoLuminance * 200.0;

    // === DerivativeMain compositing (deferred5.fsh:352-357) ===
    // Multiply by albedo AFTER all diffuse/ambient/emission accumulation
    vec3 color = sceneData * albedo;
    // Metal mask: reduce diffuse for metals in skylight
    float isMetal = surface.aux.metalness * (0.2 * smoothstep(0.3, 0.8, voxelLight.r) + 0.8);
    color *= 1.0 - isMetal;
    // Additive specular on top (not multiplied by albedo)
    vec3 specularTint = specular * mix(vec3(1.0), albedo, surface.aux.metalness);
    color += shadow * specularTint * sunlightMult * (0.5 + 0.5 * derivativeSpecularMask);

    // Sky specular (environment reflection)
    color += coolSkyColor * fresnelSchlick(max(dot(normal, viewDir), 0.0), f0ScalarClamped) *
             pow(1.0 - roughness, 1.65) * (0.018 + 0.105 * outdoorSkyMask) * derivativeSpecularMask;

    // Shadow desaturation
    float shadowMask = (1.0 - shadow) * outdoorSkyMask;
    color = desaturateLinear(color, shadowMask * uShadowDesaturation);

    // Back-scatter SSS for sun and moon (kept from current implementation)
    float backScatter = pow(max(dot(-normal, sunDir), 0.0), 1.35) * skyLightMask * mix(0.35, 1.0, sunShadow);
    color += albedo * directIlluminance * 64.0 * backScatter * sss * cloudShadow * (0.46 + 0.20 * uDirectSunStrength);
    float moonBackScatter = pow(max(dot(-normal, moonDir), 0.0), 1.45) * moonMask;
    color += albedo * uMoonLightColor * moonBackScatter * sss * 0.12;

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        color = applyAerialPerspective(color, worldPos, fogDistance, outdoorSkyMask, warmSunColor);
    }

    // Alpha encodes translucency: 0 = opaque, 1 = translucent (water/glass/ice/stained glass).
    // Downstream composite passes use this to apply refraction/tinting selectively.
    float translucency = transMask.isTranslucent ? 1.0 : 0.0;
    FragColor = vec4(max(color, vec3(0.0)), translucency);
}
