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
uniform int uVolumetricDebugMode;
uniform int uVolumetricSkyRayEnabled;
uniform int uVolumetricQualityTier; // 0=Low, 1=Medium, 2=High, 3=Ultra
uniform int uVolumetricStaticJitter; // 1 = freeze jitter for stable debug
uniform float uVolumetricShadowBiasScale; // bias multiplier for A/B testing (default 1.0)

// Cloud shadow uniforms (shared with deferred_lighting)
uniform int uCloudShadowsEnabled;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudShadowSpeed;

#include "lighting_environment.glsl"
#include "atmosphere_lut.glsl"
#include "mecraft_shadow.glsl"

// Dynamic step count per quality tier (DerivativeMain VOLUMETRIC_FOG_SAMPLES)
int getFogSteps() {
    if (uVolumetricQualityTier <= 1) return 8;   // Low/Medium
    if (uVolumetricQualityTier <= 2) return 16;  // High
    return 20;                                      // Ultra
}

// DerivativeMain-aligned volumetric fog constants
// VFOG_SUN_INTENSITY: per-sample sun illuminance scale. DerivativeMain uses SUNLIGHT_INTENSITY.
const float VFOG_SUN_INTENSITY = 1.0;
// VFOG_FINAL_SUN_MULTIPLIER: final sun scattering multiplier (DerivativeMain fogSunColor * 20.0)
// Only applied to High/Ultra shadowed sun path.
const float VFOG_FINAL_SUN_MULTIPLIER = 20.0;
// VFOG_AIR_DENSITY: Rayleigh-phase air scatter strength (DerivativeMain VOLUMETRIC_LIGHT_STRENGTH = 0.2)
const float VFOG_AIR_DENSITY = 0.2;

// Quality tier density multiplier (DerivativeMain FOG_TYPE)
float getQualityDensityMultiplier() {
    if (uVolumetricQualityTier <= 0) return 0.5;   // Low: no noise
    if (uVolumetricQualityTier <= 1) return 1.4;   // Medium: cloudy fog lite
    if (uVolumetricQualityTier <= 2) return 9.0;   // High: cloudy fog
    return 48.0;                                     // Ultra: cloudy sea
}

// Multi-lobe HG phase for High/Ultra (DerivativeMain FOG_TYPE > 1)
// 4 angular scales with forward (g=0.6) + backward (g=-0.3) lobes
float multiLobePhase(float LdotV) {
    float p1 = (atmHenyeyGreensteinPhase(LdotV, 0.6)        + atmHenyeyGreensteinPhase(LdotV, -0.3))        * 0.5;
    float p2 = (atmHenyeyGreensteinPhase(LdotV * 0.5, 0.6)  + atmHenyeyGreensteinPhase(LdotV * 0.5, -0.3))  * 0.25;
    float p3 = (atmHenyeyGreensteinPhase(LdotV * 0.25, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.25, -0.3)) * 0.125;
    float p4 = (atmHenyeyGreensteinPhase(LdotV * 0.125, 0.6)+ atmHenyeyGreensteinPhase(LdotV * 0.125, -0.3))* 0.0625;
    return p1 + p2 + p3 + p4;
}

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 tonemapPreview(vec3 color) {
    color = max(color, vec3(0.0));
    return color / (color + vec3(1.0));
}

vec3 heatmap(float v) {
    v = clamp(v, 0.0, 1.0);
    vec3 a = mix(vec3(0.02, 0.04, 0.18), vec3(0.05, 0.35, 0.95), smoothstep(0.0, 0.35, v));
    vec3 b = mix(vec3(0.05, 0.35, 0.95), vec3(0.95, 0.86, 0.18), smoothstep(0.35, 0.72, v));
    vec3 c = mix(vec3(0.95, 0.86, 0.18), vec3(1.0, 0.08, 0.02), smoothstep(0.72, 1.0, v));
    return v < 0.35 ? a : (v < 0.72 ? b : c);
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

// Shadow setup result for volumetric fog (avoids recomputation in debug modes)
struct VFogShadowData {
    vec3 proj;
    int cascadeIndex;
    vec2 texel;
    float bias;
    float projectionFade;
    float distanceFade;
    float cascadeTexelWorld;  // for debug: texel world size of selected cascade
    bool valid;
};

VFogShadowData computeVolumetricShadowSetup(vec3 worldPos, vec3 lightDir) {
    VFogShadowData data;
    data.valid = false;
    data.proj = vec3(0.0);
    data.cascadeIndex = 0;
    data.texel = vec2(0.0);
    data.bias = 0.0;
    data.projectionFade = 0.0;
    data.distanceFade = 0.0;
    data.cascadeTexelWorld = 0.0;

    if (uShadowsEnabled == 0) return data;

    float viewDistance = length(worldPos - uCameraPos);
    data.distanceFade = 1.0 - smoothstep(uShadowDistance * 0.58, uShadowDistance * 0.92, viewDistance);
    if (data.distanceFade <= 0.001) return data;

    float texelWorld = max(uShadowTexelWorldSize, 0.0001);
    data.cascadeIndex = selectCsmCascade(viewDistance);
    texelWorld = max(uCsmCascades[data.cascadeIndex].texelWorldSize, 0.0001);
    data.cascadeTexelWorld = texelWorld;
    data.proj = csmProjectWorld(worldPos + normalize(lightDir) * texelWorld * 0.5, data.cascadeIndex);
    if (shadowProjOutOfBounds(data.proj)) return data;

    ivec3 size = textureSize(uCsmShadowMap, 0);
    data.texel = 1.0 / vec2(max(size.x, 1), max(size.y, 1));
    float distanceScale = 1.0 + 0.25 * clamp(viewDistance / max(uShadowDistance, 1.0), 0.0, 1.0);
    float biasWorld = texelWorld * distanceScale * (0.5 + uShadowConstantBias * 18.0 + uShadowSlopeBias * 16.0);
    float radiusWorld = texelWorld * float(max(size.x, 1)) * 0.5;
    float depthExtent = max(uShadowDistance + radiusWorld * 3.0, 1.0);
    data.bias = max(biasWorld / (2.0 * depthExtent), 4.0e-5) * uVolumetricShadowBiasScale;
    data.projectionFade = csmProjectionFade(data.proj, data.texel);
    if (data.projectionFade <= 0.001) return data;

    data.valid = true;
    return data;
}

// Stable 3x3 PCF shadow from pre-computed data (reuses setup)
float sampleVolumetricShadowFiltered(VFogShadowData data) {
    if (!data.valid) return 1.0;

    float refZ = data.proj.z - data.bias;
    float lit = 0.0;
    // 3x3 PCF kernel (fixed, no dither — stable for volumetric fog)
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * data.texel;
            lit += sampleCsmDepthCompare(data.proj.xy + offset, data.cascadeIndex, refZ);
        }
    }
    lit *= (1.0 / 9.0);

    float visibility = mix(1.0, lit, data.projectionFade * data.distanceFade);
    return clamp(visibility, 0.0, 1.0);
}

// Convenience: compute setup + filtered in one call
float sampleVolumetricShadow(vec3 worldPos, vec3 lightDir) {
    VFogShadowData data = computeVolumetricShadowSetup(worldPos, lightDir);
    return sampleVolumetricShadowFiltered(data);
}

// Cloud shadow for volumetric fog (simplified from deferred_lighting.fs cloudShadowFactor)
// Projects fog sample to cloud layer height and samples noise
float vfogCloudShadow(vec3 worldPos, vec3 lightDir) {
    if (uCloudShadowsEnabled == 0 || uCloudShadowStrength <= 0.001) return 1.0;

    lightDir = normalize(lightDir);
    float layerHeight = max(uCloudHeight, 1.0);
    float denom = max(abs(lightDir.y), 0.18);
    float t = (layerHeight - worldPos.y) / denom;
    vec2 cloudPos = (worldPos.xz + lightDir.xz * t) * max(uCloudShadowScale, 0.0001);
    vec2 wind = vec2(0.73, 0.31) * uTime * uCloudShadowSpeed;

    float large = pseudo3DNoise(vec3(cloudPos + wind, 0.0), 0.05, vec2(0.0));
    float medium = pseudo3DNoise(vec3(cloudPos * 2.37 - wind * 1.7, 0.0), 0.05, vec2(0.0));
    float coverageThreshold = mix(0.72, 0.42, clamp(uCloudCoverage, 0.0, 1.0));
    float coverage = smoothstep(coverageThreshold, coverageThreshold + 0.24, large * 0.72 + medium * 0.28);
    // Debug weather presets are not a real cloud-shadow/precipitation system yet.
    // Keep procedural cloud shadows conservative to avoid roaming black fog blobs.
    float strength = uCloudShadowStrength * max(uCloudDensity, 0.0);
    return 1.0 - coverage * clamp(strength, 0.0, 0.45);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;

    vec3 viewDir;
    float marchDistance;
    if (depth >= 1.0) {
        if (uVolumetricSkyRayEnabled == 0) {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        // Sky pixel: reconstruct view direction and march to max distance
        vec4 farPoint = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0, 1.0, 1.0);
        viewDir = normalize(farPoint.xyz / max(farPoint.w, 0.0001) - uCameraPos);
        marchDistance = max(uVolumetricMaxDistance, 1.0);
    } else {
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 ray = worldPos - uCameraPos;
        float distance = length(ray);
        viewDir = ray / max(distance, 0.0001);
        marchDistance = min(distance, max(uVolumetricMaxDistance, 1.0));
    }

    // --- Lighting environment: physical sky data from SkyCapture ---
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
    float nightFactor = 1.0 - dayFactor;
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.45);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float LdotV = dot(viewDir, sunDir);
    float sunDot = max(LdotV, 0.0);
    float moonDot = max(dot(viewDir, moonDir), 0.0);
    float sunForward = pow(sunDot, 18.0);
    float sunWide = pow(sunDot, 4.0);
    float moonForward = pow(moonDot, 10.0) * clamp(uMoonVisibility, 0.0, 1.0);
    float phaseG = clamp(uVolumetricPhaseG, -0.2, 0.85);
    float sunPhase = atmRayleighPhase(dot(viewDir, sunDir)) * 0.35 + atmHenyeyGreensteinPhase(dot(viewDir, sunDir), phaseG) * 0.65;
    float moonPhase = atmRayleighPhase(dot(viewDir, moonDir)) * 0.55 + atmHenyeyGreensteinPhase(dot(viewDir, moonDir), 0.36) * 0.45;

    // Sky radiance for fog base color (from SkyCapture, not CPU constant)
    vec3 captureDir = normalize(vec3(viewDir.x, viewDir.y * 0.30, viewDir.z));
    vec3 skyColor = sampleEnvironmentSky(uSkyCaptureTex, captureDir);
    vec3 skyFogColor = skyColor;
    skyFogColor = mix(skyFogColor, uHorizonScatterColor, horizon * clamp(uHorizonScatterStrength, 0.0, 2.0) * 0.28);

    // Sun/moon scatter: use directIlluminance (sun+moon irradiance) per DerivativeMain.
    // directIlluminance = sunIlluminance + moonIlluminance from SkyCapture metadata.
    float phaseTerm = sunWide * 0.10 + sunForward * 0.36 + sunPhase * 0.11;
    vec3 sunScatterColor = env.directIlluminance * VFOG_SUN_INTENSITY * phaseTerm *
                           sunVisibility;
    vec3 moonScatterColor = env.moonIlluminance * VFOG_SUN_INTENSITY * 0.3 *
                            (moonForward * 0.16 + moonPhase * 0.05) * nightFactor;
    vec3 directFogColor = sunScatterColor + moonScatterColor;

    // Independent air density: Rayleigh-phase scatter (DerivativeMain VOLUMETRIC_LIGHT)
    // Uses maxDistance (not per-pixel marchDistance) to match DerivativeMain's 3.0/far
    float airDensity = VFOG_AIR_DENSITY;
    airDensity *= atmRayleighPhase(LdotV) * (3.0 / max(uVolumetricMaxDistance, 1.0));

    // Weather haze modulation applied to both components separately
    float weatherHaze = 0.55 * uWeatherMist + 0.35 * uWeatherWetness + 0.65 * uWeatherStorm;
    vec3 weatherTint = mix(vec3(1.0), vec3(0.82, 0.88, 0.94), clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0) * 0.28);
    skyFogColor *= weatherTint;
    directFogColor *= weatherTint;
    vec3 shadowLightDir = normalize(uShadowLightDirection);
    float directLightWeight = clamp(sunVisibility + clamp(uMoonVisibility, 0.0, 1.0) * nightFactor, 0.0, 1.0);

    float strength = clamp(uAerialStrength, 0.0, 2.0) * clamp(uVolumetricFogStrength, 0.0, 2.0);
    strength *= (uVolumetricFogEnabled != 0) ? 1.0 : 0.0;
    // Apply quality tier density multiplier (DerivativeMain FOG_TYPE)
    float densityMultiplier = getQualityDensityMultiplier();
    float baseDensity = (0.00012 + 0.00030 * horizon) *
                        strength *
                        max(uVolumetricBaseDensity, 0.0) *
                        (0.64 + weatherHaze * 1.55) *
                        densityMultiplier;
    // Jitter: dynamic for normal rendering, screen-only hash for stable debug
    float jitter;
    if (uVolumetricStaticJitter != 0) {
        // Fixed per-pixel jitter (no camera/time dependence) for stable debug
        jitter = fract(dot(vTexCoord, vec2(12.9898, 78.233)) + 0.5);
    } else {
        jitter = pseudo3DNoise(vec3(uCameraPos.xz * 0.17, uTime * 7.0).xzy + vec3(vTexCoord, 0.0) * 17.0, 1.0, vec2(0.0));
    }
    int fogSteps = getFogSteps();
    float stepLength = marchDistance / float(fogSteps);
    vec3 scattering = vec3(0.0);
    vec3 skyScattering = vec3(0.0);
    vec3 sunScattering = vec3(0.0);
    vec3 unshadowedSunAccum = vec3(0.0);
    vec3 shadowedSunAccum = vec3(0.0);
    float transmittance = 1.0;
    float maxDensitySeen = 0.0;
    float avgShadowVisibility = 0.0;
    float minShadowVisibility = 1.0;
    int shadowSampleCount = 0;
    int shadowedSampleCount = 0;      // samples where shadow < 0.99 (actually occluded)
    int shadowSetupValidCount = 0;    // samples where shadow setup was valid
    float avgProjectionFade = 0.0;
    float avgDistanceFade = 0.0;
    float avgProjectionFadeValid = 0.0;  // only for valid samples
    float avgDistanceFadeValid = 0.0;     // only for valid samples
    float avgRawCompare = 0.0;           // pure depth compare (before fade)
    float avgAfterFade = 0.0;            // after fade, single sample
    float avgNoBiasCompare = 0.0;        // depth compare with zero bias
    float avgBiasMagnitude = 0.0;        // bias value
    float avgCascadeIndex = 0.0;         // cascade index
    float avgCascadeTexelWorld = 0.0;    // texel world size
    float avgProjZ = 0.0;               // receiver depth in shadow space

    for (int i = 0; i < fogSteps; ++i) {
        float t = (float(i) + jitter) / float(fogSteps);
        vec3 samplePos = uCameraPos + viewDir * (t * marchDistance);
        float heightDensity = exp2(min((92.0 - samplePos.y) * max(uVolumetricHeightFalloff, 0.0001), 0.35));
        heightDensity *= 1.0 - smoothstep(180.0, 260.0, samplePos.y);
        heightDensity = clamp(heightDensity, 0.035, 1.45);

        float coverage = max(uCloudCoverage, 0.08 + uWeatherMist * 0.72 + uWeatherWetness * 0.32 + uWeatherStorm * 0.82);
        float structure = structuredFogDensity(samplePos, heightDensity, coverage);
        float clearAir = (0.06 + weatherHaze * 0.18) * max(uCloudDensity, 0.0);
        structure += clearAir;
        float nearFade = smoothstep(5.0, 32.0, t * marchDistance);
        float sampleDensity = baseDensity * heightDensity * structure * nearFade + airDensity;
        maxDensitySeen = max(maxDensitySeen, sampleDensity);
        float opticalStep = sampleDensity * stepLength;
        float stepTransmittance = exp(-opticalStep);
        float stepOpacity = clamp(1.0 - stepTransmittance, 0.0, 0.18);
        float powder = 1.0 - exp(-structure * heightDensity * 0.85);
        powder = powder * (1.0 - clamp(dot(viewDir, shadowLightDir) * 0.5 + 0.5, 0.0, 1.0) * 0.35) +
                 clamp(dot(viewDir, shadowLightDir) * 0.5 + 0.5, 0.0, 1.0) * 0.25;
        // Shadow: compute setup once, sample raw compare + filtered
        VFogShadowData shadowData = computeVolumetricShadowSetup(samplePos, shadowLightDir);
        float rawCompare = 1.0;
        float afterFade = 1.0;
        float noBiasCompare = 1.0;
        if (shadowData.valid) {
            float refZ = shadowData.proj.z - shadowData.bias;
            rawCompare = sampleCsmDepthCompare(shadowData.proj.xy, shadowData.cascadeIndex, refZ);
            // No-bias compare: refZ = proj.z (zero bias)
            noBiasCompare = sampleCsmDepthCompare(shadowData.proj.xy, shadowData.cascadeIndex, shadowData.proj.z);
            afterFade = mix(1.0, rawCompare, shadowData.projectionFade * shadowData.distanceFade);
            avgBiasMagnitude += shadowData.bias;
            avgProjZ += shadowData.proj.z;
            avgCascadeIndex += float(shadowData.cascadeIndex);
            avgCascadeTexelWorld += shadowData.cascadeTexelWorld;
        }
        float shadowVisibility = sampleVolumetricShadowFiltered(shadowData);
        avgRawCompare += rawCompare;
        avgAfterFade += afterFade;
        avgNoBiasCompare += noBiasCompare;
        avgShadowVisibility += shadowVisibility;
        minShadowVisibility = min(minShadowVisibility, shadowVisibility);
        shadowSampleCount++;
        if (shadowVisibility < 0.99) shadowedSampleCount++;
        if (shadowData.valid) {
            shadowSetupValidCount++;
            avgProjectionFadeValid += shadowData.projectionFade;
            avgDistanceFadeValid += shadowData.distanceFade;
        }
        avgProjectionFade += shadowData.projectionFade;
        avgDistanceFade += shadowData.distanceFade;

        // Cloud shadow per step (DerivativeMain CLOUDS_SHADOW equivalent)
        float cloudShadow = vfogCloudShadow(samplePos, shadowLightDir);
        shadowVisibility *= cloudShadow;

        vec3 altitudeTransmittance = atmGetTransmittanceToTopAtmosphereBoundary(
            atmPlanetRadius + clamp(samplePos.y + 100.0, 0.0, 90000.0),
            clamp(dot(vec3(0.0, 1.0, 0.0), shadowLightDir), -1.0, 1.0));

        // Sun contribution: High/Ultra uses optical depth + multi-lobe phase
        vec3 sunStep;
        if (uVolumetricQualityTier >= 2 && sampleDensity > 1e-5) {
            // 4-step sun optical depth (DerivativeMain FOG_TYPE > 1)
            // Recalculate height/density at each checkPos
            float odStepSize = 5.0;
            float sunlightOD = 0.0;
            vec3 checkPos = samplePos;
            float LdotV01 = LdotV * 0.5 + 0.5;
            // Accumulate 4 separate phase*OD terms (DerivativeMain exact)
            float phaseOD1 = 0.0, phaseOD2 = 0.0, phaseOD3 = 0.0, phaseOD4 = 0.0;
            for (int j = 0; j < 4; ++j) {
                // Recalculate density at checkPos (not reusing current step's heightDensity)
                float checkHeight = exp2(min((92.0 - checkPos.y) * max(uVolumetricHeightFalloff, 0.0001), 0.35));
                checkHeight *= 1.0 - smoothstep(180.0, 260.0, checkPos.y);
                checkHeight = clamp(checkHeight, 0.035, 1.45);
                float d = baseDensity * checkHeight * structuredFogDensity(checkPos, checkHeight, coverage);
                if (d > 1e-5) {
                    float stepOD = d * odStepSize;
                    sunlightOD += stepOD;
                    phaseOD1 += stepOD;
                    phaseOD2 += stepOD;
                    phaseOD3 += stepOD;
                    phaseOD4 += stepOD;
                }
                checkPos += shadowLightDir * odStepSize;
                odStepSize *= 1.5;
            }
            // Powder effect
            float powderSun = (1.0 - exp(-sunlightOD * 2.0)) * (1.0 - LdotV01) + LdotV01;
            // 4-lobe scattering with separate OD attenuation per lobe
            float scatteringSun =
                exp(-sunlightOD * 2.4) * (atmHenyeyGreensteinPhase(LdotV, 0.6) + atmHenyeyGreensteinPhase(LdotV, -0.3)) * 0.5 +
                exp(-sunlightOD * 1.2) * (atmHenyeyGreensteinPhase(LdotV * 0.5, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.5, -0.3)) * 0.25 +
                exp(-sunlightOD * 0.6) * (atmHenyeyGreensteinPhase(LdotV * 0.25, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.25, -0.3)) * 0.125 +
                exp(-sunlightOD * 0.3) * (atmHenyeyGreensteinPhase(LdotV * 0.125, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.125, -0.3)) * 0.0625;
            scatteringSun *= powderSun;
            float tierScale = float(uVolumetricQualityTier) * float(uVolumetricQualityTier);
            vec3 shadowedDirect = directFogColor * mix(0.28, 1.0, shadowVisibility);
            sunStep = shadowedDirect * altitudeTransmittance * (scatteringSun + airDensity) * tierScale *
                      clamp(uVolumetricLightStrength, 0.0, 2.0) * directLightWeight *
                      VFOG_FINAL_SUN_MULTIPLIER;
        } else {
            // Low/Medium: simpler path with unshadowed base + shadowed direct
            vec3 shadowedDirect = directFogColor * mix(0.28, 1.0, shadowVisibility);
            sunStep = directFogColor * (0.76 + powder * 0.22) * 0.3 +
                      shadowedDirect * altitudeTransmittance * (0.55 + powder * 0.75) *
                      clamp(uVolumetricLightStrength, 0.0, 2.0) * directLightWeight;
        }

        vec3 skyStep = skyFogColor * (0.76 + powder * 0.22);
        vec3 stepColor = skyStep + sunStep;
        scattering += transmittance * stepColor * stepOpacity;
        skyScattering += transmittance * skyStep * stepOpacity;
        sunScattering += transmittance * sunStep * stepOpacity;
        // Track unshadowed vs shadowed for debug
        unshadowedSunAccum += transmittance * directFogColor * stepOpacity;
        shadowedSunAccum += transmittance * sunStep * stepOpacity;
        transmittance *= stepTransmittance;
    }

    // DerivativeMain VolumetricFog.glsl: return real vec4(fogColor, transmittance).
    // No fixed opacity cap — fog density is controlled by the march parameters alone.
    float opacity = max(1.0 - transmittance, 0.0);

    // Debug output modes
    if (uVolumetricDebugMode == 1) {
        // Density heatmap
        FragColor = vec4(heatmap(clamp(maxDensitySeen * 40.0, 0.0, 1.0)), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 2) {
        // Transmittance (white = clear, dark = dense fog)
        FragColor = vec4(vec3(transmittance), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 3) {
        // Sky in-scattering only
        FragColor = vec4(max(skyScattering, vec3(0.0)), 1.0 - opacity);
        return;
    }
    if (uVolumetricDebugMode == 4) {
        // Sun/volume light contribution only
        FragColor = vec4(max(sunScattering, vec3(0.0)), 1.0 - opacity);
        return;
    }
    if (uVolumetricDebugMode == 5) {
        // Sun gate diagnostics: why is VFog Sun Only black?
        // R = sunVisibility (0-1)
        // G = phaseTerm * 10 (amplified to reveal small values)
        // B = avgShadowVisibility (0-1, averaged across march steps)
        float avgShadow = shadowSampleCount > 0 ? avgShadowVisibility / float(shadowSampleCount) : 1.0;
        FragColor = vec4(
            clamp(sunVisibility, 0.0, 1.0),
            clamp(phaseTerm * 10.0, 0.0, 1.0),
            clamp(avgShadow, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 6) {
        // Integration diagnostic: what survives the full pipeline?
        // R = phaseTerm * 10 (is phase contributing?)
        // G = maxDensitySeen * 40 (is density high enough?)
        // B = dot(sunScattering, luma) * 200 (final sun energy, heavily amplified)
        float sunLum = dot(sunScattering, vec3(0.2126, 0.7152, 0.0722));
        FragColor = vec4(
            clamp(phaseTerm * 10.0, 0.0, 1.0),
            clamp(maxDensitySeen * 40.0, 0.0, 1.0),
            clamp(sunLum * 200.0, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 7) {
        // Sky ray coverage (pure): which pixels participate in fog march?
        // Warm white = sky ray (marching to maxDistance)
        // Gray = geometry ray (marching to surface distance)
        // Black = not participating
        float isSky = (depth >= 1.0) ? 1.0 : 0.0;
        FragColor = vec4(mix(vec3(0.25), vec3(1.0, 0.92, 0.78), isSky), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 8) {
        // March detail: coverage + distance + accumulation
        // R = isSkyRay (1.0 for sky, 0.0 for geometry)
        // G = marchDistance / maxDistance (how far we march, 0-1)
        // B = opacity * 10 (actual fog accumulated, amplified)
        float isSky = (depth >= 1.0) ? 1.0 : 0.0;
        float distNorm = clamp(marchDistance / max(uVolumetricMaxDistance, 1.0), 0.0, 1.0);
        FragColor = vec4(
            isSky,
            distNorm,
            clamp(opacity * 10.0, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 9) {
        // Sun contrast: unshadowed vs shadowed/OD contribution
        // R = unshadowed direct (before shadow/OD)
        // G = shadowed/OD direct (after shadow/OD)
        // B = final sunStep luminance
        float unshLum = dot(unshadowedSunAccum, vec3(0.2126, 0.7152, 0.0722));
        float shLum = dot(shadowedSunAccum, vec3(0.2126, 0.7152, 0.0722));
        float sunLum = dot(sunScattering, vec3(0.2126, 0.7152, 0.0722));
        FragColor = vec4(
            clamp(unshLum * 50.0, 0.0, 1.0),
            clamp(shLum * 50.0, 0.0, 1.0),
            clamp(sunLum * 100.0, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 10) {
        // Sun Only x20: amplified sun scattering for structure diagnosis
        FragColor = vec4(tonemapPreview(max(sunScattering * 20.0, vec3(0.0))), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 11) {
        // Sun Only x100: heavily amplified sun scattering
        FragColor = vec4(tonemapPreview(max(sunScattering * 100.0, vec3(0.0))), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 12) {
        // Shadow visibility diagnostics
        // R = average shadowVisibility across march steps
        // G = minimum shadowVisibility (darkest point)
        // B = ratio of samples actually occluded (shadow < 0.99)
        float avgShadow = shadowSampleCount > 0 ? avgShadowVisibility / float(shadowSampleCount) : 1.0;
        float occludedRatio = shadowSampleCount > 0 ? float(shadowedSampleCount) / float(shadowSampleCount) : 0.0;
        FragColor = vec4(
            clamp(avgShadow, 0.0, 1.0),
            clamp(minShadowVisibility, 0.0, 1.0),
            clamp(occludedRatio, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 13) {
        // Shadow raw vs filtered comparison
        // R = average after-fade single sample (noisy, but fade applied)
        // G = average filtered shadow (3x3 PCF, stable)
        // B = abs difference * 5 (shows where PCF changes shadow)
        float avgSingle = shadowSampleCount > 0 ? avgAfterFade / float(shadowSampleCount) : 1.0;
        float avgFilt = shadowSampleCount > 0 ? avgShadowVisibility / float(shadowSampleCount) : 1.0;
        float diff = abs(avgSingle - avgFilt);
        FragColor = vec4(
            clamp(avgSingle, 0.0, 1.0),
            clamp(avgFilt, 0.0, 1.0),
            clamp(diff * 5.0, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 14) {
        // Shadow projection diagnostics: why is shadow mostly 1?
        // R = ratio of samples where shadow setup was valid
        // G = valid-only avg projectionFade (CSM edge falloff for valid samples)
        // B = valid-only avg distanceFade (distance fadeout for valid samples)
        float validRatio = shadowSampleCount > 0 ? float(shadowSetupValidCount) / float(shadowSampleCount) : 0.0;
        float avgPFadeV = shadowSetupValidCount > 0 ? avgProjectionFadeValid / float(shadowSetupValidCount) : 0.0;
        float avgDFadeV = shadowSetupValidCount > 0 ? avgDistanceFadeValid / float(shadowSetupValidCount) : 0.0;
        FragColor = vec4(
            clamp(validRatio, 0.0, 1.0),
            clamp(avgPFadeV, 0.0, 1.0),
            clamp(avgDFadeV, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 15) {
        // Shadow compare: three independent diagnostics
        // R = raw depth compare (no fade, no PCF — pure shadow map comparison)
        // G = after fade (single sample, fade applied — shows fade dilution)
        // B = after PCF (filtered — shows final volumetric shadow result)
        float avgRaw = shadowSampleCount > 0 ? avgRawCompare / float(shadowSampleCount) : 1.0;
        float avgFade = shadowSampleCount > 0 ? avgAfterFade / float(shadowSampleCount) : 1.0;
        float avgFilt = shadowSampleCount > 0 ? avgShadowVisibility / float(shadowSampleCount) : 1.0;
        FragColor = vec4(
            clamp(avgRaw, 0.0, 1.0),
            clamp(avgFade, 0.0, 1.0),
            clamp(avgFilt, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 16) {
        // Bias compare: does bias push occluded samples to lit?
        // R = no-bias raw compare (proj.z, zero bias)
        // G = current-bias raw compare (proj.z - bias)
        // B = difference (shows where bias changes result)
        float avgNB = shadowSampleCount > 0 ? avgNoBiasCompare / float(shadowSampleCount) : 1.0;
        float avgCB = shadowSampleCount > 0 ? avgRawCompare / float(shadowSampleCount) : 1.0;
        FragColor = vec4(
            clamp(avgNB, 0.0, 1.0),
            clamp(avgCB, 0.0, 1.0),
            clamp(abs(avgNB - avgCB) * 10.0, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 17) {
        // Cascade index map: explicit color per cascade
        // cascade 0 = red, cascade 1 = green, cascade 2 = blue, cascade 3 = white
        float avgCI = shadowSampleCount > 0 ? avgCascadeIndex / float(shadowSampleCount) : 0.0;
        int ci = int(avgCI + 0.5);
        vec3 cascadeColor = vec3(0.3);
        if (ci == 0) cascadeColor = vec3(1.0, 0.2, 0.2);
        else if (ci == 1) cascadeColor = vec3(0.2, 1.0, 0.2);
        else if (ci == 2) cascadeColor = vec3(0.2, 0.4, 1.0);
        else if (ci == 3) cascadeColor = vec3(1.0, 1.0, 1.0);
        FragColor = vec4(cascadeColor, 1.0);
        return;
    }
    if (uVolumetricDebugMode == 18) {
        // Shadow receiver depth: proj.z distribution
        // R = average proj.z (receiver depth in shadow space, normalized to 0-1)
        // G = average bias * 1000 (amplified)
        // B = raw compare result (lit=1, occluded=0)
        float avgPZ = shadowSetupValidCount > 0 ? avgProjZ / float(shadowSetupValidCount) : 0.5;
        float avgBI = shadowSampleCount > 0 ? avgBiasMagnitude / float(shadowSampleCount) : 0.0;
        float avgRC = shadowSampleCount > 0 ? avgRawCompare / float(shadowSampleCount) : 1.0;
        FragColor = vec4(
            clamp(avgPZ, 0.0, 1.0),
            clamp(avgBI * 1000.0, 0.0, 1.0),
            clamp(avgRC, 0.0, 1.0),
            1.0
        );
        return;
    }

    FragColor = vec4(max(scattering, vec3(0.0)), 1.0 - opacity);
}
