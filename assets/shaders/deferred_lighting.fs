#version 450 core
#include "gbuffer_contract.glsl"

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

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 desaturateLinear(vec3 color, float amount) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(color, vec3(luma), clamp(amount, 0.0, 1.0));
}

float ggxDistribution(float ndoth, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 0.00001);
}

float smithG1(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.00001);
}

float hammonDiffuseApprox(float ndotl, float ndotv, float roughness) {
    float lit = max(ndotl, 0.0);
    float viewWrap = ndotv * 0.5 + 0.5;
    float roughBoost = mix(0.0, 0.18, clamp(roughness, 0.0, 1.0));
    return pow(lit, mix(1.18, 0.78, roughness)) *
           mix(0.92, 1.08, viewWrap) *
           (1.0 + roughBoost * (1.0 - lit));
}

float roughTerminatorFill(float ndotl, float roughness) {
    float terminator = smoothstep(-0.18, 0.12, ndotl) * (1.0 - smoothstep(0.10, 0.55, ndotl));
    return terminator * roughness * 0.16;
}

vec3 fresnelSchlick(float vdoth, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
}

vec3 blackbodyApprox(float temperatureKelvin) {
    float t = clamp(temperatureKelvin, 1000.0, 12000.0) / 1000.0;
    vec3 color;
    if (t <= 6.6) {
        color.r = 1.0;
        color.g = clamp(0.39008158 * log(t) - 0.63184144, 0.0, 1.0);
    } else {
        color.r = clamp(1.29293619 * pow(t - 6.0, -0.13320476), 0.0, 1.0);
        color.g = clamp(1.12989086 * pow(t - 6.0, -0.07551485), 0.0, 1.0);
    }
    color.b = t >= 6.6 ? 1.0 : (t <= 1.9 ? 0.0 : clamp(0.54320679 * log(t - 1.0) - 1.19625409, 0.0, 1.0));
    return color;
}

vec3 artisticSunIlluminance(vec3 sunColor, vec3 sunDir) {
    float elevation = clamp(sunDir.y, 0.0, 1.0);
    vec3 noonWarmth = vec3(1.10, 1.00, 0.84);
    vec3 lowSunWarmth = vec3(1.38, 0.82, 0.42);
    vec3 tint = mix(lowSunWarmth, noonWarmth, smoothstep(0.08, 0.62, elevation));
    float energy = mix(1.35, 1.08, smoothstep(0.04, 0.70, elevation));
    return max(sunColor * tint * energy, vec3(0.0));
}

vec2 directionToSkyCaptureUv(vec3 dir) {
    dir = normalize(dir);
    float phi = atan(dir.x, -dir.z);
    float u = phi / kTwoPi + 0.5;
    float v = dir.y * 0.5 + 0.5;
    return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 sampleSkyCapture(vec3 dir) {
    return texture(uSkyCaptureTex, directionToSkyCaptureUv(dir)).rgb;
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

float stableShadowJitter() {
    // Keep the soft-shadow kernel fixed for now. A per-shadow-texel hash rotates the PCF/PCSS
    // pattern whenever the sun or snapped projection crosses texel boundaries, which shows up as
    // moving striped ghosting even with a stationary camera.
    return 0.5;
}

float noise2D(vec2 uv) {
    return texture(uNoiseTex, uv).r;
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
        vec2 scaled = coord * 1.165;
        float quarticLength = pow(dot(scaled * scaled, scaled * scaled), 0.25);
        return quarticLength * 0.85 + 0.15;
    }
    return length(coord * 1.169) * 0.85 + 0.15;
}

vec3 worldToShadowProj(vec3 worldPos, out float warpDensity) {
    vec4 lightView = uShadowModelView * vec4(worldPos, 1.0);
    vec4 lightClip = uShadowProjection * lightView;
    vec3 proj = lightClip.xyz / max(lightClip.w, 0.00001);
    warpDensity = calculateShadowWarp(proj.xy);
    proj.xy /= warpDensity;
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
    return max(abs(uShadowProjectionInverse[2][2]) * 2.0, 1.0);
}

float shadowDepthBiasFromWorld(float worldUnits) {
    return worldUnits / shadowDepthWorldScale();
}

float shadowWorldBias(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float slope = 1.0 - clamp(ndotl, 0.0, 1.0);
    float receiverScale = 1.0 + 0.25 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    return texelWorld * receiverScale *
           (uShadowConstantBias * 48.0 + uShadowSlopeBias * 64.0 * slope);
}

float shadowNormalOffsetWorld(float ndotl, float viewDistance) {
    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    float grazing = 1.0 - clamp(ndotl, 0.0, 1.0);
    float distanceScale = 1.0 + 0.35 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    float requestedTexels = max(uShadowNormalOffset, 0.0) / 0.09375;
    return texelWorld * requestedTexels * distanceScale * (1.0 + 0.85 * grazing);
}

vec2 pcssBlockerSearch(vec3 proj, float bias, float searchRadius, float jitter) {
    float blockerDepthSum = 0.0;
    float blockerCount = 0.0;
    for (int i = 0; i < 12; ++i) {
        vec2 offset = spiralDiskSample(i, 12, jitter) * searchRadius;
        float blockerDepth = sampleShadowDepthAt(proj, offset);
        float isBlocker = step(blockerDepth, proj.z - bias) * (1.0 - step(0.9999, blockerDepth));
        blockerDepthSum += blockerDepth * isBlocker;
        blockerCount += isBlocker;
    }

    float averageBlockerDepth = blockerDepthSum / max(blockerCount, 0.0001);
    return vec2(averageBlockerDepth, blockerCount);
}

float pcssFilterRadius(vec3 proj, float baseRadius, float bias, float warpDensity, float jitter) {
    if (uPcssShadowsEnabled == 0 || uShadowPcssStrength <= 0.001) {
        return baseRadius;
    }

    float texelRadiusFromSoftness = max(uShadowSoftness, 0.1);
    float searchRadius = clamp(baseRadius * (0.62 + texelRadiusFromSoftness * 0.16) / max(warpDensity, 0.25),
                               1.0,
                               8.0);
    vec2 blockerSearch = pcssBlockerSearch(proj, bias, searchRadius, jitter);
    float averageBlockerDepth = blockerSearch.x;
    float blockerCount = blockerSearch.y;

    if (blockerCount < 0.5) {
        return min(baseRadius, 1.15);
    }

    float receiverToBlocker = max(proj.z - averageBlockerDepth, 0.0);
    float receiverToBlockerWorld = receiverToBlocker * shadowDepthWorldScale();
    float penumbraTexels = receiverToBlockerWorld / max(uShadowTexelWorldSize, 0.0001);
    penumbraTexels *= 0.10 * uShadowPcssStrength / max(warpDensity, 0.25);

    float blockerConfidence = smoothstep(1.5, 5.5, blockerCount);
    float adaptiveRadius = clamp(0.85 + penumbraTexels, 0.85, max(baseRadius, 1.0));
    return mix(min(baseRadius, 1.15), adaptiveRadius, blockerConfidence);
}

float shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }
    lightDir = normalize(lightDir);
    float viewDistanceForBias = length(worldPos - uCameraPos);
    float distanceFade = 1.0 - smoothstep(uShadowDistance * 0.58, uShadowDistance * 0.82, viewDistanceForBias);
    if (distanceFade <= 0.001) {
        return 1.0;
    }
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float normalOffset = shadowNormalOffsetWorld(ndotl, viewDistanceForBias);
    float warpDensity = 1.0;
    vec3 proj = worldToShadowProj(worldPos + normal * normalOffset, warpDensity);
    if (proj.x < 0.0 || proj.y < 0.0 || proj.x > 1.0 || proj.y > 1.0 || proj.z > 1.0) {
        return 1.0;
    }
    float bias = shadowDepthBiasFromWorld(shadowWorldBias(ndotl, viewDistanceForBias));

    float projectionFade = shadowProjectionFade(proj);
    if (projectionFade <= 0.001) {
        return 1.0;
    }

    if (uSoftShadowsEnabled == 0) {
        float hardShadow = shapeShadowVisibility(compareShadowTexel(proj, ivec2(0), bias));
        return mix(1.0, hardShadow, projectionFade * distanceFade);
    }

    float viewDistance = viewDistanceForBias;
    float distanceSoftness = smoothstep(18.0, 96.0, viewDistance);
    float grazingSoftness = 1.0 - ndotl;
    vec2 centeredShadow = proj.xy * 2.0 - 1.0;
    float filterWarpDensity = calculateShadowWarp(centeredShadow);
    float radius = clamp(max(uShadowSoftness, 0.1) * (1.05 + 0.34 * distanceSoftness + 0.20 * grazingSoftness) * filterWarpDensity,
                         1.25, 7.5);
    float jitter = stableShadowJitter();
    radius = pcssFilterRadius(proj, radius, bias, warpDensity, jitter);
    float lit = 0.0;
    for (int i = 0; i < 24; ++i) {
        lit += compareShadowBilinear(proj, spiralDiskSample(i, 24, jitter) * radius, bias);
    }
    return mix(1.0, shapeShadowVisibility(lit / 24.0), projectionFade * distanceFade);
}

float contactShadow(vec3 worldPos,
                    vec3 normal,
                    vec3 lightDir,
                    float lightMask,
                    float shadowVisibility) {
    if (uShadowsEnabled == 0 || uContactShadowsEnabled == 0 || uContactShadowStrength <= 0.001) {
        return 1.0;
    }
    lightDir = normalize(lightDir);
    float lightTerm = max(dot(normal, lightDir), 0.0);
    lightMask = clamp(lightMask, 0.0, 1.0);
    float litGate = smoothstep(0.55, 0.92, shadowVisibility);
    if (lightTerm * lightMask * litGate <= 0.02) {
        return 1.0;
    }

    float occlusion = 0.0;
    float weightSum = 0.0;
    vec3 rayOrigin = worldPos + normal * 0.035 + lightDir * 0.045;
    const float maxDistance = 1.65;
    for (int i = 0; i < 8; ++i) {
        float step01 = (float(i) + 0.72) / 8.0;
        float rayDistance = step01 * step01 * maxDistance + 0.045;
        vec3 sampleWorld = rayOrigin + lightDir * rayDistance;
        float sampleProjectedDepth = 1.0;
        vec2 sampleUv = projectWorldToUv(sampleWorld, sampleProjectedDepth);
        if (sampleUv.x <= 0.001 || sampleUv.y <= 0.001 || sampleUv.x >= 0.999 || sampleUv.y >= 0.999) {
            continue;
        }

        float sceneDepth = texture(uDepthTex, sampleUv).r;
        if (sceneDepth >= 1.0 || sceneDepth >= sampleProjectedDepth - 0.00008) {
            continue;
        }

        vec3 sceneWorld = reconstructWorldPosition(sampleUv, sceneDepth);
        float worldSeparation = length(sceneWorld - sampleWorld);
        float thickness = mix(0.12, 0.72, step01);
        float hit = 1.0 - smoothstep(thickness * 0.45, thickness, worldSeparation);
        float forwardWeight = 1.0 - step01 * 0.55;
        occlusion += hit * forwardWeight;
        weightSum += forwardWeight;
    }
    occlusion = clamp(occlusion / max(weightSum, 0.0001), 0.0, 1.0);
    float grazingBoost = 0.55 + 0.45 * pow(1.0 - lightTerm, 0.65);
    return 1.0 - occlusion * uContactShadowStrength * lightTerm * lightMask * litGate * grazingBoost;
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
    float wetness = clamp(uWeatherWetness * surface.aux.wetnessMask * voxelLight.r, 0.0, 1.0);
    float wetPorosity = wetness * clamp(surface.aux.porosity, 0.0, 1.0);
    bool waterLike = materialKind == MATERIAL_WATER || materialKind == MATERIAL_GLASS;
    if (!waterLike) {
        albedo *= 1.0 - wetPorosity * 0.22;
        roughness = mix(roughness, max(0.08, roughness * 0.36), wetness * (0.72 + surface.aux.metalness * 0.18));
        f0Scalar = mix(f0Scalar, max(f0Scalar, 0.055), wetness * (0.35 + surface.aux.metalness * 0.20));
    }
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);

    vec2 lightmapUV = vec2(voxelLight.g, 1.0 - voxelLight.r);
    vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
    vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
    vec3 vanillaLight = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
    float skyLightMask = clamp(voxelLight.r * uSkyIntensity, 0.0, 1.0);
    float nightSkyMask = clamp(voxelLight.r * uMoonVisibility, 0.0, 1.0);
    float outdoorSkyMask = max(skyLightMask, nightSkyMask);
    float blockLightMask = clamp(voxelLight.g, 0.0, 1.0);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    vec3 viewDir = normalize(uCameraPos - worldPos);
    vec3 halfDir = normalize(sunDir + viewDir);
    float rawNdotL = dot(normal, sunDir);
    float rawNdotM = dot(normal, moonDir);
    float ndotl = max(rawNdotL, 0.0);
    float ndotm = max(rawNdotM, 0.0);
    float ndotv = max(dot(normal, viewDir), 0.04);
    float ndoth = max(dot(normal, halfDir), 0.0);
    float vdoth = max(dot(viewDir, halfDir), 0.0);
    float diffuse = hammonDiffuseApprox(rawNdotL, ndotv, roughness);
    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    vec3 shadowLightDir = (uShadowLightMode == 1) ? moonDir : sunDir;
    float shadow = shadowFactor(worldPos, normal, shadowLightDir);
    float activeShadowLightMask = (uShadowLightMode == 1) ? nightSkyMask : skyLightMask;
    shadow *= contactShadow(worldPos, normal, shadowLightDir, activeShadowLightMask, shadow);
    float cloudShadow = cloudShadowFactor(worldPos, shadowLightDir, outdoorSkyMask);
    float sunShadow = (uShadowLightMode == 0) ? shadow : 1.0;
    float moonShadow = (uShadowLightMode == 1) ? mix(1.0, shadow, 0.82) : 1.0;

    vec3 warmSunColor = artisticSunIlluminance(uSunLightColor, sunDir);
    warmSunColor = mix(warmSunColor, warmSunColor * vec3(1.16, 1.03, 0.78), clamp(uSunWarmth, 0.0, 1.5) * 0.65);
    vec3 coolSkyColor = mix(uSkyAmbientColor, uSkyAmbientColor * vec3(0.78, 0.92, 1.18), clamp(uSkyCoolness, 0.0, 1.0));
    vec3 capturedZenith = sampleSkyCapture(vec3(0.0, 1.0, 0.0));
    vec3 capturedNormalSky = sampleSkyIrradiance(normal);
    float skyCaptureInfluence = mix(0.18, 0.46, 1.0 - clamp(uSkyIntensity, 0.0, 1.0));
    coolSkyColor = mix(coolSkyColor, mix(capturedZenith, capturedNormalSky, 0.55), skyCaptureInfluence);
    float directEnergy = 1.56 + 0.28 * (1.0 - roughness);
    float terminatorFill = roughTerminatorFill(rawNdotL, roughness) * sunShadow;
    float sssSunTransmittance = sss * smoothstep(-0.35, 0.18, -rawNdotL);
    float sssSunShadowFill = sssSunTransmittance * mix(0.18, 0.46, 1.0 - sunShadow) * cloudShadow;
    vec3 directSun = warmSunColor *
                     ((diffuse * sunShadow + terminatorFill) * cloudShadow + sssSunShadowFill) *
                     skyLightMask * uDirectSunStrength * directEnergy;
    float moonMask = nightSkyMask;
    vec3 moonFill = uMoonLightColor * moonMask * (0.030 + 0.060 * uSkyAmbientStrength);
    float sssMoonTransmittance = sss * smoothstep(-0.30, 0.16, -rawNdotM);
    vec3 directMoon = uMoonLightColor *
                      (pow(ndotm * 0.5 + 0.5, 1.15) * moonShadow * cloudShadow + sssMoonTransmittance * 0.16) *
                      moonMask * (0.38 + 0.18 * uSkyAmbientStrength);
    vec3 f0 = vec3(f0Scalar);
    vec3 specF = fresnelSchlick(vdoth, f0);
    float specD = ggxDistribution(ndoth, roughness);
    float specG = smithG1(ndotl, roughness) * smithG1(ndotv, roughness);
    vec3 directSpecular = warmSunColor * specF * (specD * specG / max(4.0 * ndotl * ndotv, 0.0001));
    directSpecular *= ndotl * sunShadow * cloudShadow * skyLightMask * uDirectSunStrength;
    directSpecular *= mix(1.18, 0.18, roughness);
    float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyAmbient = coolSkyColor * (0.026 + 0.54 * outdoorSkyMask) *
                      uSkyAmbientStrength *
                      mix(0.48, 1.0, upward) +
                      moonFill;
    skyAmbient *= mix(vec3(1.0), uShadowTintColor, (1.0 - shadow) * clamp(uShadowTintStrength, 0.0, 1.0) * 0.72);
    vec3 skySpecular = coolSkyColor * specF * pow(1.0 - roughness, 1.65) * (0.018 + 0.105 * outdoorSkyMask);

    float minimumAmbientMask = mix(0.35, 1.0, outdoorSkyMask);
    vec3 minimumAmbient = uShadowTintColor * uMinimumAmbient * minimumAmbientMask * 0.62;

    float groundFacing = clamp(dot(normal, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 fakeBounce = warmSunColor * uFakeBounceStrength * pow(skyLightMask, 4.0) * (0.28 + 0.58 * groundFacing) * (0.35 + 0.65 * (1.0 - sunShadow));

    float blockLightCurve = pow(blockLightMask, 2.05);
    vec3 warmBlockLight = vec3(1.0, 0.84, 0.58);
    vec3 blockLightColor = mix(warmBlockLight, vanillaLight, 0.18);
    vec3 blockLight = blockLightColor * blockLightCurve * uBlockLightStrength;

    vec3 totalLight = directSun + directMoon + skyAmbient + minimumAmbient + fakeBounce + blockLight;
    totalLight = mix(totalLight, vanillaLight, 0.025);

    vec3 color = albedo * totalLight * vertexAo * mix(1.0, ssao, 0.65);
    float backScatter = pow(max(dot(-normal, sunDir), 0.0), 1.35) * skyLightMask * mix(0.35, 1.0, sunShadow);
    color += albedo * warmSunColor * backScatter * sss * cloudShadow * (0.46 + 0.20 * uDirectSunStrength);
    float moonBackScatter = pow(max(dot(-normal, moonDir), 0.0), 1.45) * moonMask;
    color += albedo * uMoonLightColor * moonBackScatter * sss * 0.12;
    float specularSurfaceMask = smoothstep(0.025, 0.14, f0Scalar) * (1.0 - roughness * 0.45);
    color += (directSpecular + skySpecular) * vertexAo * mix(1.0, ssao, 0.35) * (0.72 + 0.58 * specularSurfaceMask);
    float shadowMask = (1.0 - shadow) * outdoorSkyMask;
    color = desaturateLinear(color, shadowMask * uShadowDesaturation);
    float emissionStrength = max(emissiveHint * emissiveHint, materialEmission);
    float emissionLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3 emissionTint = vec3(1.0, 0.88, 0.64);
    vec3 emissionColor = mix(albedo, emissionTint * max(emissionLuma, 0.45), 0.42);
    color += emissionColor * emissionStrength * (0.55 + 0.82 * uBlockLightStrength);

    if (uFogEnabled != 0) {
        float fogDistance = length(worldPos - uCameraPos);
        color = applyAerialPerspective(color, worldPos, fogDistance, outdoorSkyMask, warmSunColor);
    }

    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
