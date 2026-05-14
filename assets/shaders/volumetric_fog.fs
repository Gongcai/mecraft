#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uNoiseTex;
uniform sampler2D uShadowMapRaw;    // Raw depth for texelFetch/textureSize
uniform sampler2D uShadowColorTex;
uniform sampler3D uAtmosphereLut;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uShadowLightDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uVolumetricFogStrength;
uniform float uVolumetricPhaseG;
uniform float uVolumetricBaseDensity;
uniform float uVolumetricHeightFalloff;
uniform float uVolumetricMaxDistance;
uniform float uWeatherMist;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uShadowDistance;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uVolumetricLightStrength;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform int uShadowsEnabled;
uniform int uVolumetricFogEnabled;
uniform int uShadowLightMode;
uniform float uTime;
uniform bool uNoiseEnabled;

#include "atmosphere_lut.glsl"
#include "mecraft_shadow.glsl"

const int kFogSteps = 8;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 sampleSkyCapture(vec3 dir) {
    return sampleSkyRadiance(uSkyCaptureTex, dir);
}

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float sampleNoise2D(vec2 uv, float slice, int channel) {
    vec4 n = texture(uNoiseTex, uv + vec2(slice * 0.071, slice * 0.113));
    if (channel == 0) {
        return n.r;
    }
    if (channel == 1) {
        return n.g;
    }
    return n.b;
}

float pseudo3DNoise(vec3 p, float scale, vec2 wind) {
    if (!uNoiseEnabled) {
        return hash13(p * scale);
    }

    vec3 q = p * scale;
    float slice = q.y * 7.0 + q.z * 1.7;
    float slice0 = floor(slice);
    float blend = smoothstep(0.0, 1.0, fract(slice));
    vec2 uv = q.xz + wind;
    float n0 = sampleNoise2D(uv, slice0, 0);
    float n1 = sampleNoise2D(uv, slice0 + 1.0, 1);
    return mix(n0, n1, blend);
}

float structuredFogDensity(vec3 worldPos, float heightDensity, float weatherCoverage) {
    vec2 wind = vec2(uTime * 0.004, uTime * 0.002);
    vec3 p = worldPos * 0.070 + vec3(wind.x, 0.0, wind.y);
    float base = pseudo3DNoise(p, 1.0, vec2(0.0)) * 4.0;
    float detail = pseudo3DNoise(p * 4.0 + vec3(wind.x, 0.0, wind.y), 1.0, vec2(0.0));
    float threshold = mix(5.25, 3.55, clamp(weatherCoverage, 0.0, 1.0));
    float cloudy = clamp((base - detail) * 4.0 * heightDensity - threshold, 0.0, 1.0);
    float fineShape = smoothstep(0.05, 0.85, base * 0.22 + detail * 0.35);
    return cloudy * mix(0.85, 1.45, fineShape);
}

float sampleVolumetricShadow(vec3 worldPos, vec3 lightDir) {
    if (uShadowsEnabled == 0) {
        return 1.0;
    }

    float viewDistance = length(worldPos - uCameraPos);
    float distanceFade = 1.0 - smoothstep(uShadowDistance * 0.58, uShadowDistance * 0.92, viewDistance);
    if (distanceFade <= 0.001) {
        return 1.0;
    }

    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    int cascadeIndex = selectCsmCascade(viewDistance);

    texelWorld = max(uCsmCascades[cascadeIndex].texelWorldSize, 0.0001);
    vec3 proj = csmProjectWorld(worldPos + normalize(lightDir) * texelWorld * 0.5,
                                cascadeIndex);
    if (shadowProjOutOfBounds(proj)) {
        return 1.0;
    }

    ivec3 size = textureSize(uCsmShadowMap, 0);
    vec2 texel = 1.0 / vec2(max(size.x, 1), max(size.y, 1));
    float distanceScale = 1.0 + 0.25 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    float biasWorld = texelWorld * distanceScale * (0.5 + uShadowConstantBias * 18.0 + uShadowSlopeBias * 16.0);
    float radiusWorld = texelWorld * float(max(size.x, 1)) * 0.5;
    float depthExtent = max(uShadowDistance + radiusWorld * 3.0, 1.0);
    float bias = max(biasWorld / (2.0 * depthExtent), 4.0e-5);
    float projectionFade = csmProjectionFade(proj, texel);
    if (projectionFade <= 0.001) {
        return 1.0;
    }

    float lit = 0.0;
    float refZ = proj.z - bias;
    lit += sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
    lit += sampleCsmDepthCompare(proj.xy + vec2( texel.x, 0.0), cascadeIndex, refZ);
    lit += sampleCsmDepthCompare(proj.xy + vec2(-texel.x, 0.0), cascadeIndex, refZ);
    lit += sampleCsmDepthCompare(proj.xy + vec2(0.0,  texel.y), cascadeIndex, refZ);
    lit *= 0.25;

    float visibility = mix(1.0, lit, projectionFade * distanceFade);
    return clamp(visibility, 0.0, 1.0);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 ray = worldPos - uCameraPos;
    float distance = length(ray);
    vec3 viewDir = ray / max(distance, 0.0001);
    float marchDistance = min(distance, 260.0);

    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
    float nightFactor = 1.0 - dayFactor;
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.45);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float sunDot = max(dot(viewDir, sunDir), 0.0);
    float moonDot = max(dot(viewDir, moonDir), 0.0);
    float sunForward = pow(sunDot, 18.0);
    float sunWide = pow(sunDot, 4.0);
    float moonForward = pow(moonDot, 10.0) * clamp(uMoonVisibility, 0.0, 1.0);
    float phaseG = clamp(uVolumetricPhaseG, -0.2, 0.85);
    float sunPhase = atmRayleighPhase(dot(viewDir, sunDir)) * 0.35 + atmHenyeyGreensteinPhase(dot(viewDir, sunDir), phaseG) * 0.65;
    float moonPhase = atmRayleighPhase(dot(viewDir, moonDir)) * 0.55 + atmHenyeyGreensteinPhase(dot(viewDir, moonDir), 0.36) * 0.45;
    vec3 captureDir = normalize(vec3(viewDir.x, viewDir.y * 0.30, viewDir.z));
    vec3 skyColor = sampleSkyCapture(captureDir);
    vec3 fogColor = skyColor;
    fogColor = mix(fogColor, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0) * 0.28);
    vec3 sunScatterColor = uSunLightColor * (sunWide * 0.10 + sunForward * 0.36 + sunPhase * 0.11) *
                           sunVisibility * clamp(uHorizonScatterStrength, 0.0, 2.0);
    vec3 moonScatterColor = uMoonLightColor * (moonForward * 0.16 + moonPhase * 0.05) * nightFactor;
    fogColor += sunScatterColor + moonScatterColor;
    float weatherHaze = 0.55 * uWeatherMist + 0.35 * uWeatherWetness + 0.65 * uWeatherStorm;
    fogColor = mix(fogColor, fogColor * vec3(0.82, 0.88, 0.94), clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0) * 0.28);
    vec3 shadowLightDir = normalize(uShadowLightDirection);
    float directLightWeight = clamp(sunVisibility + clamp(uMoonVisibility, 0.0, 1.0) * nightFactor, 0.0, 1.0);
    vec3 directFogColor = sunScatterColor + moonScatterColor;

    float strength = clamp(uAerialStrength, 0.0, 2.0) * clamp(uVolumetricFogStrength, 0.0, 2.0);
    strength *= (uVolumetricFogEnabled != 0) ? 1.0 : 0.0;
    float baseDensity = (0.00012 + 0.00030 * horizon) *
                        strength *
                        max(uVolumetricBaseDensity, 0.0) *
                        (0.64 + weatherHaze * 1.55);
    float jitter = pseudo3DNoise(vec3(uCameraPos.xz * 0.17, uTime * 7.0).xzy + vec3(vTexCoord, 0.0) * 17.0, 1.0, vec2(0.0));
    marchDistance = min(marchDistance, max(uVolumetricMaxDistance, 1.0));
    float stepLength = marchDistance / float(kFogSteps);
    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < kFogSteps; ++i) {
        float t = (float(i) + jitter) / float(kFogSteps);
        vec3 samplePos = uCameraPos + viewDir * (t * marchDistance);
        float heightDensity = exp2(min((92.0 - samplePos.y) * max(uVolumetricHeightFalloff, 0.0001), 0.35));
        heightDensity *= 1.0 - smoothstep(180.0, 260.0, samplePos.y);
        heightDensity = clamp(heightDensity, 0.035, 1.45);

        float coverage = max(uCloudCoverage, 0.08 + uWeatherMist * 0.72 + uWeatherWetness * 0.32 + uWeatherStorm * 0.82);
        float structure = structuredFogDensity(samplePos, heightDensity, coverage);
        float clearAir = (0.06 + weatherHaze * 0.18) * max(uCloudDensity, 0.0);
        structure += clearAir;
        float nearFade = smoothstep(5.0, 32.0, t * marchDistance);
        float sampleDensity = baseDensity * heightDensity * structure * nearFade;
        float opticalStep = sampleDensity * stepLength;
        float stepTransmittance = exp(-opticalStep);
        float stepOpacity = clamp(1.0 - stepTransmittance, 0.0, 0.18);
        float powder = 1.0 - exp(-structure * heightDensity * 0.85);
        powder = powder * (1.0 - clamp(dot(viewDir, shadowLightDir) * 0.5 + 0.5, 0.0, 1.0) * 0.35) +
                 clamp(dot(viewDir, shadowLightDir) * 0.5 + 0.5, 0.0, 1.0) * 0.25;
        float shadowVisibility = sampleVolumetricShadow(samplePos, shadowLightDir);
        vec3 shadowedDirect = directFogColor * mix(0.28, 1.0, shadowVisibility);
        vec3 altitudeTransmittance = atmGetTransmittanceToTopAtmosphereBoundary(
            atmPlanetRadius + clamp(samplePos.y + 100.0, 0.0, 90000.0),
            clamp(dot(vec3(0.0, 1.0, 0.0), shadowLightDir), -1.0, 1.0));
        vec3 stepColor = fogColor * (0.76 + powder * 0.22);
        stepColor += shadowedDirect * altitudeTransmittance * (0.55 + powder * 0.75) *
                     clamp(uVolumetricLightStrength, 0.0, 2.0) *
                     directLightWeight;
        scattering += transmittance * stepColor * stepOpacity;
        transmittance *= stepTransmittance;
    }

    float opacity = clamp(1.0 - transmittance, 0.0, 0.34);
    float rawOpacity = max(1.0 - transmittance, 0.0001);
    scattering *= opacity / rawOpacity;
    FragColor = vec4(max(scattering, vec3(0.0)), 1.0 - opacity);
}
