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
uniform vec3 uShadowLightDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uVolumetricFogStrength;
uniform float uVolumetricBaseDensity;
uniform float uVolumetricHeightFalloff;
uniform float uVolumetricMaxDistance;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uSkyWetness;
uniform float uSurfaceWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uPrecipitation;
uniform float uLightningFlash;
uniform float uShadowDistance;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
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
uniform int uVolumetricTimeFadeEnabled; // DerivativeMain TIME_FADE toggle
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
// DerivativeMain CornetteShanksPhase (cloud_target.fs:109)
// More accurate than HG for forward-peaked fog scattering.
float cornetteShanksPhase(float cosTheta, float g) {
    float gg = g * g;
    float mu2 = cosTheta * cosTheta;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (3.0 * (1.0 - gg) * (1.0 + mu2)) / (8.0 * 3.14159265 * (2.0 + gg) * denom * sqrt(denom));
}

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
    float horizon = pow(1.0 - clamp(abs(viewDir.y), 0.0, 1.0), 1.45);

    vec3 sunDir = normalize(uSunDirection);
    float sunVisibility = smoothstep(-0.08, 0.18, sunDir.y) * dayFactor;
    float LdotV = dot(viewDir, sunDir);
    float LdotV01 = LdotV * 0.5 + 0.5;

    // DerivativeMain VolumetricFog.glsl: fogColor = fogSunColor * 20.0 + fogSkyColor * 2.0
    // directFogColor and skyFogColor are irradiance from SkyCapture metadata.
    // Phase, shadow, and powder are accumulated per-step; radiance is applied after.
    vec3 directFogColor = env.directIlluminance;  // sun+moon irradiance (DerivativeMain directIlluminance)
    vec3 skyFogColor = env.skyIlluminance;         // sky hemisphere irradiance (DerivativeMain skyIlluminance)

    // DerivativeMain VolumetricFog.glsl:310 — fog color darkening with wetness.
    // At full wetness, fog color is reduced to 20% (oneMinus(0.8 * wetness)).
    float wetness = clamp(uSkyWetness, 0.0, 1.0);
    float weatherHaze = clamp(uFogWetness, 0.0, 1.0);
    skyFogColor *= 1.0 - 0.8 * wetness;
    directFogColor *= 1.0 - 0.8 * wetness;

    // Lightning flash: boost fog scattering so flash lights up the atmosphere.
    skyFogColor *= 1.0 + uLightningFlash * 4.0;
    directFogColor *= 1.0 + uLightningFlash * 4.0;

    // DerivativeMain VOLUMETRIC_LIGHT: airDensity includes RayleighPhase(LdotV),
    // enters fogDensity directly (both extinction and in-scattering are phase-modulated).
    float airDensity = VFOG_AIR_DENSITY * atmRayleighPhase(LdotV) * (3.0 / max(uVolumetricMaxDistance, 1.0));

    vec3 shadowLightDir = normalize(uShadowLightDirection);

    // Debug: sun phase term for debug views (not used in main integration)
    float phaseTerm = pow(max(LdotV, 0.0), 4.0) * 0.10 + pow(max(LdotV, 0.0), 18.0) * 0.36 +
                      atmRayleighPhase(LdotV) * 0.35 * 0.11 + atmHenyeyGreensteinPhase(LdotV, 0.6) * 0.65 * 0.11;

    float strength = clamp(uAerialStrength, 0.0, 2.0) * clamp(uVolumetricFogStrength, 0.0, 2.0);
    strength *= (uVolumetricFogEnabled != 0) ? 1.0 : 0.0;
    // Apply quality tier density multiplier (DerivativeMain FOG_TYPE)
    float densityMultiplier = getQualityDensityMultiplier();
    float baseDensity = (0.00012 + 0.00030 * horizon) *
                        strength *
                        max(uVolumetricBaseDensity, 0.0) *
                        (0.64 + weatherHaze * 1.55) *
                        densityMultiplier;

    // DerivativeMain VolumetricFog.glsl:191: Low/Medium phase applied to mistDensity.
    // For FOG_TYPE <= 1, phase modifies density before the march loop.
    // High/Ultra (FOG_TYPE > 1) applies multi-lobe phase per-step instead.
    float mistDensity = baseDensity;
    if (uVolumetricQualityTier < 2) {
        // DerivativeMain VolumetricFog.glsl:191
        float csPhase = cornetteShanksPhase(LdotV, 0.7 - uSkyWetness * 0.3) * 0.45 +
                        atmHenyeyGreensteinPhase(LdotV, -0.3) * 0.15 + 0.1;
        mistDensity *= csPhase;
    }

    // DerivativeMain TIME_FADE: modulate airDensity and mistDensity by time of day.
    // Peaks at sunrise/sunset (meWeight) and midnight; stronger under wetness.
    // DerivativeMain VolumetricFog.glsl:210-213
    if (uVolumetricTimeFadeEnabled != 0) {
        float sunY = uSunDirection.y;
        float meFade = (sunY < 0.18) ? 0.37 + 1.2 * max(0.0, -sunY) : 1.7;
        float meWeight = pow(clamp(1.0 - meFade * abs(sunY - 0.18), 0.0, 1.0), 2.0);
        float timeMidnight = (sunY < 0.0 ? 1.0 : 0.0) * (1.0 - meWeight);
        float wetness = clamp(uSkyWetness, 0.0, 1.0);
        airDensity *= max(clamp(meWeight + 0.25, 0.0, 1.0) + timeMidnight * 4.0, wetness);
        mistDensity *= max(meWeight * meWeight + timeMidnight * 2.0, wetness);
    }

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
    // DerivativeMain integration form: accumulate dimensionless samples, apply radiance after.
    vec3 sunlightSample = vec3(0.0);   // shadow * phase * fogSample (per step)
    float skylightSample = 0.0;        // fogSample (per step)
    vec3 unshadowedSunSample = vec3(0.0); // debug: same units as sunlightSample without shadowing
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

        float coverage = max(uCloudCoverage, 0.08 + uCloudWetness * 0.82);
        float structure = structuredFogDensity(samplePos, heightDensity, coverage);
        float clearAir = (0.06 + weatherHaze * 0.18) * max(uCloudDensity, 0.0);
        structure += clearAir;
        float nearFade = smoothstep(5.0, 32.0, t * marchDistance);
        // DerivativeMain: fogDensity = CalculateFogDensity(rayPosition) * mistDensity + airDensity.
        // For Low/Medium, mistDensity already includes phase. For High/Ultra, phase is applied per-step below.
        float density = mistDensity * heightDensity * structure * nearFade;
        float sampleDensity = density + airDensity;
        maxDensitySeen = max(maxDensitySeen, sampleDensity);
        float fogDensity = sampleDensity * stepLength;
        float stepTransmittance = exp(-fogDensity);
        float stepOpacity = 1.0 - stepTransmittance;
        // DerivativeMain powder effect: oneMinus(exp(-fogDensity * 3.0)) blended by LdotV01.
        float powder = 1.0 - exp(-fogDensity * 3.0);
        powder = powder * (1.0 - LdotV01) + LdotV01;
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

        // DerivativeMain VolumetricFog.glsl: fogSample = powder * transmittance * oneMinus(stepTransmittance)
        // Dimensionless: represents how much light scatters toward camera from this step.
        float fogSample = powder * transmittance * stepOpacity;

        // DerivativeMain integration: shadow * phase * fogSample for sun, fogSample for sky.
        // Phase is applied differently per tier (DerivativeMain FOG_TYPE).
        float shadow = shadowVisibility;  // DerivativeMain: shadow = shadow map sample

        vec3 unshadowedStep = vec3(fogSample);
        if (uVolumetricQualityTier >= 2 && density > 1e-5) {
            // High/Ultra (FOG_TYPE > 1): multi-lobe phase + optical depth + airDensity
            // DerivativeMain VolumetricFog.glsl:254-272
            float odStepSize = 5.0;
            float sunlightOD = 0.0;
            vec3 checkPos = samplePos;
            for (int j = 0; j < 4; ++j) {
                float checkHeight = exp2(min((92.0 - checkPos.y) * max(uVolumetricHeightFalloff, 0.0001), 0.35));
                checkHeight *= 1.0 - smoothstep(180.0, 260.0, checkPos.y);
                checkHeight = clamp(checkHeight, 0.035, 1.45);
                float d = mistDensity * checkHeight * structuredFogDensity(checkPos, checkHeight, coverage);
                if (d > 1e-5) {
                    sunlightOD += d * odStepSize;
                }
                checkPos += shadowLightDir * odStepSize;
                odStepSize *= 1.5;
            }
            // Powder effect (DerivativeMain line 266)
            float powderSun = (1.0 - exp(-sunlightOD * 2.0)) * (1.0 - LdotV01) + LdotV01;
            // 4-lobe scattering (DerivativeMain lines 267-270)
            float scatteringSun =
                exp(-sunlightOD * 2.4) * (atmHenyeyGreensteinPhase(LdotV, 0.6) + atmHenyeyGreensteinPhase(LdotV, -0.3)) * 0.5 +
                exp(-sunlightOD * 1.2) * (atmHenyeyGreensteinPhase(LdotV * 0.5, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.5, -0.3)) * 0.25 +
                exp(-sunlightOD * 0.6) * (atmHenyeyGreensteinPhase(LdotV * 0.25, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.25, -0.3)) * 0.125 +
                exp(-sunlightOD * 0.3) * (atmHenyeyGreensteinPhase(LdotV * 0.125, 0.6) + atmHenyeyGreensteinPhase(LdotV * 0.125, -0.3)) * 0.0625;
            scatteringSun *= powderSun;
            // DerivativeMain line 271: shadow *= (scatteringSun + airDensity) * FOG_TYPE^2
            float tierScale = float(uVolumetricQualityTier) * float(uVolumetricQualityTier);
            float phaseScale = (scatteringSun + airDensity) * tierScale;
            shadow *= phaseScale;
            sunlightSample += shadow * fogSample;
            unshadowedStep = vec3(phaseScale * fogSample);
        } else {
            // Low/Medium (FOG_TYPE <= 1): phase applied to mistDensity before fogDensity
            // DerivativeMain VolumetricFog.glsl:191
            sunlightSample += shadow * fogSample;
        }
        unshadowedSunSample += unshadowedStep;

        // DerivativeMain line 291: skylightSample += fogSample (no phase, no powder)
        skylightSample += fogSample;

        transmittance *= stepTransmittance;
        if (transmittance < 1e-3) break;
    }

    // DerivativeMain VolumetricFog.glsl:298-301
    // fogSunColor = directIlluminance * sunlightSample * SUNLIGHT_INTENSITY
    // fogSkyColor  = skyIlluminance  * skylightSample
    // fogColor     = fogSunColor * 20.0 + fogSkyColor * 2.0
    float opacity = max(1.0 - transmittance, 0.0);
    vec3 fogSunColor = directFogColor * sunlightSample * VFOG_SUN_INTENSITY;
    vec3 fogSkyColor = skyFogColor * skylightSample;
    vec3 scattering = fogSunColor * VFOG_FINAL_SUN_MULTIPLIER + fogSkyColor * 2.0;
    vec3 sunScattering = fogSunColor * VFOG_FINAL_SUN_MULTIPLIER;
    vec3 skyScattering = fogSkyColor * 2.0;

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
        // R = unshadowed direct fog luminance
        // G = shadow retention ratio
        // B = shadow modulation ratio
        vec3 unshadowedSunColor = directFogColor * unshadowedSunSample * VFOG_SUN_INTENSITY * VFOG_FINAL_SUN_MULTIPLIER;
        float unshLum = dot(unshadowedSunColor, vec3(0.2126, 0.7152, 0.0722));
        float shLum = dot(sunScattering, vec3(0.2126, 0.7152, 0.0722));
        float retained = unshLum > 1e-6 ? clamp(shLum / unshLum, 0.0, 1.0) : 0.0;
        float modulation = unshLum > 1e-6 ? clamp(max(unshLum - shLum, 0.0) / unshLum, 0.0, 1.0) : 0.0;
        FragColor = vec4(
            clamp(unshLum * 50.0, 0.0, 1.0),
            retained,
            modulation,
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
    if (uVolumetricDebugMode == 19) {
        // Sun/sky fog ratio
        // R = sunScattering luminance (direct volumetric light)
        // G = skyScattering luminance (ambient fog)
        // B = sun / (sun + sky) ratio
        float sunLum = dot(sunScattering, vec3(0.2126, 0.7152, 0.0722));
        float skyLum = dot(skyScattering, vec3(0.2126, 0.7152, 0.0722));
        float total = sunLum + skyLum;
        float ratio = total > 1e-6 ? sunLum / total : 0.0;
        FragColor = vec4(
            clamp(sunLum * 50.0, 0.0, 1.0),
            clamp(skyLum * 10.0, 0.0, 1.0),
            clamp(ratio, 0.0, 1.0),
            1.0
        );
        return;
    }
    if (uVolumetricDebugMode == 20) {
        // Beam modulation: shadow-driven contrast inside the direct volumetric light.
        // R = unshadowed direct fog luminance
        // G = shadow retention ratio (shadowed / unshadowed)
        // B = shadow modulation ratio ((unshadowed - shadowed) / unshadowed)
        vec3 unshadowedSunColor = directFogColor * unshadowedSunSample * VFOG_SUN_INTENSITY * VFOG_FINAL_SUN_MULTIPLIER;
        float unshLum = dot(unshadowedSunColor, vec3(0.2126, 0.7152, 0.0722));
        float shLum = dot(sunScattering, vec3(0.2126, 0.7152, 0.0722));
        float retained = unshLum > 1e-6 ? clamp(shLum / unshLum, 0.0, 1.0) : 0.0;
        float modulation = unshLum > 1e-6 ? clamp(max(unshLum - shLum, 0.0) / unshLum, 0.0, 1.0) : 0.0;
        FragColor = vec4(
            clamp(unshLum * 50.0, 0.0, 1.0),
            retained,
            modulation,
            1.0
        );
        return;
    }

    FragColor = vec4(max(scattering, vec3(0.0)), 1.0 - opacity);
}
