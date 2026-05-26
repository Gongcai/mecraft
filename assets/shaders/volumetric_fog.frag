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
uniform vec2 uJitter;
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
uniform float uVolumetricMaxDistance;
uniform float uSkyWetness;
uniform float uWeatherStorm; // DerivativeMain BiomeSandstorm: desert storm intensity
uniform float uLightningFlash;
uniform float uShadowDistance;
uniform float uShadowExtent;
uniform float uShadowTexelWorldSize;
uniform float uShadowConstantBias;
uniform float uShadowSlopeBias;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform vec3 uCloudDynamicWeather; // DerivativeMain cloudDynamicWeather.xyz: cirrocumulus/cirrus/storm
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform float uCloudWetness;        // cloud wetness for density modulation

// Planar cloud uniforms (for cloud shadow projection to cirrus layer)
uniform float uPlanarCloudCoverage;
uniform float uPlanarCloudDensity;
uniform float uPlanarCloudAltitude;
uniform int uShadowsEnabled;
uniform int uVolumetricLightEnabled; // DerivativeMain VOLUMETRIC_LIGHT: base haze (airDensity)
uniform int uVolumetricFogEnabled;
uniform int uShadowLightMode;
uniform float uTime;
uniform float uCloudTimeScale;
uniform bool uNoiseEnabled;
uniform int uVolumetricDebugMode;
uniform int uVolumetricSkyRayEnabled;
uniform int uVolumetricTimeFadeEnabled; // DerivativeMain TIME_FADE toggle
uniform int uVolumetricQualityTier; // DerivativeMain FOG_TYPE: 0=Low, 1=Medium, 2=High, 3=Ultra
uniform int uVolumetricFogSamples; // DerivativeMain VOLUMETRIC_FOG_SAMPLES: march step count (default 20)
uniform int uVolumetricStaticJitter; // 1 = freeze jitter for stable debug
uniform int uFrameIndex;
uniform float uVolumetricShadowBiasScale; // bias multiplier for A/B testing (default 1.0)

// Underwater volumetric light (DerivativeMain UW_VOLUMETRIC_LIGHT)
uniform int uIsEyeInWater;
uniform int uUwVolumetricLightEnabled; // DerivativeMain UW_VOLUMETRIC_LIGHT toggle
uniform vec3 uWaterAbsorption; // RGB absorption coefficients (default 0.4, 0.14, 0.08)
uniform float uUnderwaterVolumetricLightStrength; // DerivativeMain UW_VOLUMETRIC_LIGHT_STRENGTH

// DerivativeMain-style VFog independent profile (decoupled from weather)
uniform float uVFogCenterHeight;   // SEA_LEVEL: y-level where fog is densest (default 63.0)
uniform float uVFogHeightSpread;   // High/Ultra falloff denominator: 100 -> exponent 0.01
uniform float uVFogNoiseScale;     // noise sampling scale for structured fog (default 0.04)
uniform float uVFogLightStrength;  // DerivativeMain VOLUMETRIC_LIGHT_STRENGTH (default 0.2)
uniform float uVFogDensityScale;   // user density multiplier / volFogDensity (default 1.0)

// Cloud shadow uniforms (shared with deferred_lighting)
uniform int uCloudShadowsEnabled;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudShadowSpeed;

#include "lighting_environment.glsl"
#include "atmosphere_lut.glsl"
#include "mecraft_shadow.glsl"
#include "cloud_density.glsl"

const int noiseTextureResolution = 256;
const float noiseTexturePixelSize = 1.0 / float(noiseTextureResolution);

// Dynamic ray step count from DerivativeMain CalculateVolumetricFog().
// FOG_TYPE controls density shape; VOLUMETRIC_FOG_SAMPLES controls march quality.
int getFogSteps(float rayLength) {
    float maxSamples = float(uVolumetricFogSamples); // DerivativeMain VOLUMETRIC_FOG_SAMPLES.
    return int(min(maxSamples, maxSamples * 0.4 + rayLength * 0.1));
}

// DerivativeMain-aligned volumetric fog constants
// VFOG_SUN_INTENSITY: per-sample sun illuminance scale. DerivativeMain uses SUNLIGHT_INTENSITY.
const float VFOG_SUN_INTENSITY = 1.0;
// VFOG_FINAL_SUN_MULTIPLIER: final sun scattering multiplier (DerivativeMain fogSunColor * 20.0)
// Only applied to High/Ultra shadowed sun path.
const float VFOG_FINAL_SUN_MULTIPLIER = 20.0;
// VFOG_AIR_DENSITY: Rayleigh-phase air scatter strength (DerivativeMain VOLUMETRIC_LIGHT_STRENGTH = 0.2)
const float VFOG_AIR_DENSITY = 0.2;

// DerivativeMain FOG_TYPE density shapes.
// DerivativeMain CornetteShanksPhase (cloud_target.fs:109)
// More accurate than HG for forward-peaked fog scattering.
float cornetteShanksPhase(float cosTheta, float g) {
    float gg = g * g;
    float mu2 = cosTheta * cosTheta;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (3.0 * (1.0 - gg) * (1.0 + mu2)) / (8.0 * 3.14159265 * (2.0 + gg) * denom * sqrt(denom));
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
    vec2 ndcXY = uv * 2.0 - 1.0 - uJitter;
    vec4 clip = vec4(ndcXY, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 vfogCurve3(vec3 x) {
    return x * x * (3.0 - 2.0 * x);
}

// DerivativeMain/lib/Head/Noise.inc Get3DNoiseSmooth().
// Mecraft adaptation: use uNoiseTex and hash fallback when the shaderpack noise texture is unavailable.
float get3DNoiseSmooth(vec3 position) {
    if (!uNoiseEnabled) {
        return hash13(position);
    }

    vec3 p = floor(position);
    vec3 b = vfogCurve3(position - p);
    vec2 uv = p.xy + b.xy + 97.0 * p.z;
    vec2 rg = texture(uNoiseTex, (uv + 0.5) * noiseTexturePixelSize).xy;
    return mix(rg.x, rg.y, b.z);
}

// DerivativeMain CalculateFogDensity — 4 quality tiers.
// Returns full fog density including height falloff and noise structure.
// Low/Medium: simple asymmetric falloff ± 1 octave.
// High/Ultra: symmetric SEA_LEVEL falloff + multi-octave 3D noise → cloud blobs.
// Source: DerivativeMain/lib/Atmosphere/VolumetricFog.glsl lines 94-151
float CalculateVFogDensity(in vec3 rayPosition) {
    float ns = max(uVFogNoiseScale, 0.001);
    // uVFogHeightSpread controls falloff exponent: spread=100 → exponent=0.01 (DerivativeMain default)
    float falloffExp = 1.0 / max(uVFogHeightSpread, 1.0);
    // DerivativeMain shaders.properties: volFogWind = vec3(volFogTime, 0.0, volFogTime * 0.6).
    float volFogTime = uTime * 0.01;
    vec3 volFogWind = vec3(volFogTime, 0.0, volFogTime * 0.6);

    if (uVolumetricQualityTier <= 0) {
        // FOG_TYPE 0 — Low: simple asymmetric falloff, no noise.
        // DerivativeMain: exp2(min((SEA_LEVEL + 32.0 - y) * rcp(12.0), 0.2)) * 0.5
        float fogDensity = exp2(min((uVFogCenterHeight + 32.0 - rayPosition.y) / 12.0, 0.2));
        return fogDensity * 0.5;
    }

    if (uVolumetricQualityTier <= 1) {
        // FOG_TYPE 1 — Medium: asymmetric falloff + 2-octave noise.
        // DerivativeMain: exp2(min((SEA_LEVEL + 28.0 - y) * 0.15, 0.2)) + noise erosion
        float fogDensity = exp2(min((uVFogCenterHeight + 28.0 - rayPosition.y) * 0.15, 0.2));
        vec3 p = rayPosition * ns * 1.75;
        float noise = get3DNoiseSmooth(p + volFogWind) * 4.0;
        noise -= get3DNoiseSmooth(p * 4.0 + volFogWind);
        fogDensity = clamp(noise * 4.0 * fogDensity - 5.0, 0.0, 1.0) * 1.4;
        float sunY = uSunDirection.y;
        float meFade = (sunY < 0.18) ? 0.37 + 1.2 * max(0.0, -sunY) : 1.7;
        float meWeight = pow(clamp(1.0 - meFade * abs(sunY - 0.18), 0.0, 1.0), 2.0);
        float timeNoon = (sunY > 0.0 ? 1.0 : 0.0) * (1.0 - meWeight);
        if (uWeatherStorm < 5e-3) {
            fogDensity = mix(fogDensity, 1.0, timeNoon);
        }
        return fogDensity;
    }

    if (uVolumetricQualityTier <= 2) {
        // FOG_TYPE 2 — High: symmetric SEA_LEVEL falloff + 4-octave noise → cloud blobs.
        // DerivativeMain: fastExp(-abs(y - SEA_LEVEL) * 0.01), 4 octaves at 0.04/3.2/9.6/28.8
        float falloff = exp2(-abs(rayPosition.y - uVFogCenterHeight) * falloffExp);
        vec3 p = rayPosition * ns;
        p += volFogWind;
        float noise = get3DNoiseSmooth(p) * 0.5;
        p += volFogWind;
        noise += get3DNoiseSmooth(p * 3.2) * 0.25;
        p += volFogWind;
        noise += get3DNoiseSmooth(p * 9.6) * 0.125;
        p += volFogWind;
        noise += get3DNoiseSmooth(p * 28.8) * 0.0625;
        float fogDensity = clamp(noise * 12.0 * falloff - 4.5, 0.0, 1.0);
        return fogDensity * 9.0;
    }

    // FOG_TYPE 3 — Ultra: symmetric SEA_LEVEL falloff + 5-octave loop → dense cloud sea.
    // DerivativeMain: exp2(-abs(y - SEA_LEVEL) * 0.01), 5 octaves loop at 0.013 * 4^i
    float falloff = exp2(-abs(rayPosition.y - uVFogCenterHeight) * falloffExp);
    vec3 p = (rayPosition + volFogWind) * ns * 0.325;
    float weight = 0.5;
    float noise = 0.0;
    for (uint i = 0u; i < 5u; i++, weight *= 0.5) {
        noise += weight * get3DNoiseSmooth(p);
        p = (p + volFogWind) * 4.0;
    }
    float fogDensity = clamp(falloff * noise * 400.0 - 170.0, 0.0, 1.0);
    return fogDensity * 48.0;
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

// Cloud shadow for volumetric fog — DerivativeMain VFog CalculateCloudShadow().
// Enabled by CLOUDS_SHADOW (uCloudShadowsEnabled). When disabled, returns 1.0.
// No strength/scale/speed modulation — shadow shape comes from real cloud density.
// DerivativeMain VFog path: no 0.03 floor (unlike surface path).
float vfogCloudShadow(vec3 worldPos, vec3 lightDir) {
    if (uCloudShadowsEnabled == 0) return 1.0;

    lightDir = normalize(lightDir);
    float cloudDensity = 0.0;
    vec3 checkOrigin = worldPos + vec3(0.0, cloudPlanetRadius, 0.0);

    // VC_SHADOW: cumulus cloud shadow (two shell samples at 15% and 50% thickness)
    // DerivativeMain VolumetricClouds.glsl:57: storm raises cloud altitude — must match main renderer.
    if (uCloudHeight > 0.0 && uCloudThickness > 0.0) {
        float stormZ = uCloudDynamicWeather.z;
        float stormCloudHeight = uCloudHeight * (1.0 + stormZ * 2.0);
        float cloudThick = max(uCloudThickness, 1.0);
        float weatherCoverage = clamp(uCloudCoverage * (1.0 - stormZ * 0.3), 0.5, 1.5);
        float cloudShellRadius = cloudPlanetRadius + stormCloudHeight;

        vec2 bottomHit = cloudRaySphereIntersection(checkOrigin, lightDir, cloudShellRadius + 0.15 * cloudThick);
        if (bottomHit.y >= 0.0) {
            vec3 samplePos = bottomHit.y * lightDir + worldPos;
            float sampleNH = clamp((samplePos.y - stormCloudHeight) / cloudThick, 0.0, 1.0);
            cloudDensity += cloudDensityAt(samplePos, sampleNH, weatherCoverage, 1.0);
        }

        vec2 topHit = cloudRaySphereIntersection(checkOrigin, lightDir, cloudShellRadius + 0.50 * cloudThick);
        if (topHit.y >= 0.0) {
            vec3 samplePos = topHit.y * lightDir + worldPos;
            float sampleNH = clamp((samplePos.y - stormCloudHeight) / cloudThick, 0.0, 1.0);
            cloudDensity += cloudDensityAt(samplePos, sampleNH, weatherCoverage, 1.0);
        }
    }

    // PC_SHADOW: planar cloud (cirrus) shadow
    if (uPlanarCloudAltitude > 0.0) {
        float cloudPlaneRadius = cloudPlanetRadius + uPlanarCloudAltitude;
        vec2 planeHit = cloudRaySphereIntersection(checkOrigin, lightDir, cloudPlaneRadius);
        if (planeHit.y >= 0.0) {
            vec2 cirrusPos = planeHit.y * lightDir.xz + worldPos.xz;
            float coverage = clamp(uPlanarCloudCoverage + uCloudWetness * 0.2, 0.05, 0.95);
            cloudDensity += cirrusCloudDensity(cirrusPos, coverage) * 10.0;
        }
    }

    cloudDensity = mix(0.4, cloudDensity, clamp(sqr(abs(lightDir.y) * 2.0), 0.0, 1.0));
    cloudDensity = clamp(cloudDensity, 0.0, 1.0);

    return exp2(-cloudDensity * cloudDensity * 2e2);
}

// DerivativeMain VolumetricFog.glsl:317-380 UnderwaterVolumetricLight()
// Mecraft adaptation: samples CSM transparent shadow contract (shadowtex0/1 + shadowcolor0/1)
// to detect water/transparent occlusion and apply colored water absorption.
float uwDepthStep(float refZ, float depthSample) {
    return step(refZ, depthSample);
}

float sampleCsmDepthRawStep(vec2 uv, int cascadeIndex, float refZ) {
    float depthSample = texture(uCsmShadowDepthRaw, vec3(uv, float(cascadeIndex))).r;
    return uwDepthStep(refZ, depthSample);
}

float sampleCsmDepthAllRawStep(vec2 uv, int cascadeIndex, float refZ) {
    float depthSample = sampleCsmDepthAllRaw(uv, cascadeIndex);
    return uwDepthStep(refZ, depthSample);
}

vec4 UnderwaterVolumetricLight(vec3 worldPos, vec3 worldDir, float dither) {
    float rayLength = min(24.0, length(worldPos - uCameraPos));
    int steps = min(22, int(12.0 + 0.5 * rayLength));
    float rSteps = 1.0 / float(steps);
    float stepLength = rayLength * rSteps;

    vec3 shadowLightDir = normalize(uShadowLightDirection);
    vec3 coeff = uWaterAbsorption + 0.02;
    vec3 stepTransmittance = exp(-coeff * stepLength);
    vec3 transmittance = vec3(1.0);
    vec3 scattering = vec3(0.0);

    for (int i = 1; i < steps; ++i) {
        float t = (float(i) + dither) * rSteps;
        vec3 samplePos = uCameraPos + worldDir * (t * rayLength);
        vec3 sampleShadow = vec3(1.0);

        VFogShadowData shadowData = computeVolumetricShadowSetup(samplePos, shadowLightDir);
        if (shadowData.valid) {
            vec2 uv = shadowData.proj.xy;
            int ci = shadowData.cascadeIndex;
            float z = shadowData.proj.z;

            // DerivativeMain UW VL uses raw shadowtex fetches and step(), not PCF.
            // Keep this path crisp so water/opaque blockers modulate the ray march.
            float opaqueLit = sampleCsmDepthRawTexel(uv, ci) >= z ? 1.0 : 0.0;

            // Transparent/water detection is only valid in cascade 0. Outside the
            // contract we still keep the unshadowed water scattering instead of
            // dropping the sample; UW volumetric light exists even without blockers.
            if (ci == 0) {
                float allLit = sampleCsmDepthAllRawTexel(uv, 0) >= z ? 1.0 : 0.0;
                vec4 color0 = sampleCsmShadowColor0RawTexel(uv, 0);
                vec4 color1 = sampleCsmShadowColor1RawTexel(uv, 0);
                float waterSurfaceY = color1.w * 512.0 - 128.0;
                float waterDepth = waterSurfaceY - samplePos.y;
                bool hasWaterSurface = color0.a < 0.5 && color1.w > 0.001 && waterDepth > 0.1;

                if (hasWaterSurface) {
                    // Water surface data is the caustic/shaft carrier. It should
                    // modulate even when the opaque/all depth comparison is too
                    // coarse to flag an exact blocker at this sample.
                    vec3 caustic = pow(max(color0.rgb, vec3(0.0)), vec3(4.0));
                    sampleShadow = vec3(opaqueLit) * mix(vec3(0.45), caustic * 1.85, 0.85);
                    sampleShadow *= fastExp(-coeff * 0.4 * max(waterDepth, 8.0));
                } else if (allLit < 1.0) {
                    sampleShadow = vec3(opaqueLit);

                    if (opaqueLit != allLit) {
                        vec3 shadowColorSample = pow4(color0.rgb);
                        sampleShadow = shadowColorSample * (vec3(opaqueLit) - vec3(allLit)) + vec3(allLit);
                    }
                }
            } else {
                sampleShadow = vec3(opaqueLit);
            }
        }

        scattering += sampleShadow * transmittance * oneMinus(stepTransmittance);
        transmittance *= stepTransmittance;
    }

    // DerivativeMain: refracted sun direction through water surface for phase
    float eta = 1.0 / 1.33; // 1/WATER_REFRACT_IOR
    vec3 lightVector = refract(normalize(uShadowLightDirection), vec3(0.0, -1.0, 0.0), eta);
    float LdotV = dot(lightVector, worldDir);
    float phase = atmHenyeyGreensteinPhase(LdotV, 0.8) + atmHenyeyGreensteinPhase(LdotV, 0.6);

    // DerivativeMain: 8.0/coeff * directIlluminance * scattering * phase * STRENGTH
    vec3 env = getLightingEnvironment(uSkyCaptureTex).directIlluminance;
    vec3 fogColor = 8.0 / coeff * env * oneMinus(0.95 * clamp(uSkyWetness, 0.0, 1.0));
    fogColor *= scattering * phase * uUnderwaterVolumetricLightStrength;

    return vec4(fogColor, 1.0);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;

    vec3 viewDir;
    float marchDistance;
    if (depth >= 1.0) {
        // DerivativeMain always marches sky pixels when VOLUMETRIC_LIGHT is compiled in.
        // Keep the debug sky-ray toggle from removing the noon airDensity haze.
        bool needsSkyRay = (uVolumetricLightEnabled != 0) ||
                           (uVolumetricFogEnabled != 0 && uVolumetricSkyRayEnabled != 0);
        if (!needsSkyRay) {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        // Sky pixel: reconstruct view direction and march to max distance
        vec4 farPoint = uInvViewProj * vec4(vTexCoord * 2.0 - 1.0 - uJitter, 1.0, 1.0);
        viewDir = normalize(farPoint.xyz / max(farPoint.w, 0.0001) - uCameraPos);
        marchDistance = max(uVolumetricMaxDistance, 1.0);
    } else {
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
        vec3 ray = worldPos - uCameraPos;
        float distance = length(ray);
        viewDir = ray / max(distance, 0.0001);
        marchDistance = min(distance, max(uVolumetricMaxDistance, 1.0));
    }

    // Shadow contract debug views (debug view 75-77, shader mode 30-32).
    // Keep this before the underwater branch so UW VL does not hide the contract diagnostics.
    if (uVolumetricDebugMode >= 30 && uVolumetricDebugMode <= 32) {
        float sceneDepth = texture(uDepthTex, vTexCoord).r;
        if (sceneDepth < 1.0) {
            vec3 worldPos = reconstructWorldPosition(vTexCoord, sceneDepth);
            VFogShadowData sd = computeVolumetricShadowSetup(worldPos, normalize(uShadowLightDirection));
            if (sd.valid) {
                // Water shadow contract only maintained for cascade 0.
                // Show marker color for out-of-contract cascades.
                if (sd.cascadeIndex != 0) {
                    FragColor = vec4(0.15, 0.0, 0.0, 1.0); // dark red = no contract data
                    return;
                }
                vec2 uv = sd.proj.xy;
                if (uVolumetricDebugMode == 30) {
                    float dAll = sampleCsmDepthAllRaw(uv, 0);
                    float dOpa = texture(uCsmShadowDepthRaw, vec3(uv, 0.0)).r;
                    float gap = abs(dAll - dOpa) * 20.0;
                    FragColor = vec4(heatmap(clamp(gap, 0.0, 1.0)), 1.0);
                    return;
                }
                if (uVolumetricDebugMode == 31) {
                    FragColor = sampleCsmShadowColor0(uv, 0);
                    return;
                }
                if (uVolumetricDebugMode == 32) {
                    FragColor = sampleCsmShadowColor1(uv, 0);
                    return;
                }
            }
        }
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // DerivativeMain composite.fsh:79-85 — UW_VOLUMETRIC_LIGHT replaces overworld VFog
    if (uIsEyeInWater == 1 && uUwVolumetricLightEnabled != 0) {
        float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth < 1.0 ? depth : 0.9999);
        vec4 uwResult = UnderwaterVolumetricLight(worldPos, viewDir, dither);

        // UW debug views (debug view 72-74, shader mode 27-29)
        if (uVolumetricDebugMode == 27) {
            float lum = dot(max(uwResult.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
            FragColor = vec4(heatmap(clamp(lum * 20.0, 0.0, 1.0)), 1.0);
            return;
        }
        if (uVolumetricDebugMode == 28) {
            float rayLength = min(24.0, length(worldPos - uCameraPos));
            int steps = min(22, int(12.0 + 0.5 * rayLength));
            vec3 shadowLightDir = normalize(uShadowLightDirection);
            float waterHitCount = 0.0;
            float avgCaustic = 0.0;
            float avgWaterDepth = 0.0;
            for (int i = 1; i < steps; ++i) {
                float t = float(i) / float(steps);
                vec3 samplePos = uCameraPos + viewDir * (t * rayLength);
                VFogShadowData shadowData = computeVolumetricShadowSetup(samplePos, shadowLightDir);
                if (shadowData.valid && shadowData.cascadeIndex == 0) {
                    float opaqueLit = sampleCsmDepthRawTexel(shadowData.proj.xy, 0) >= shadowData.proj.z ? 1.0 : 0.0;
                    float allLit = sampleCsmDepthAllRawTexel(shadowData.proj.xy, 0) >= shadowData.proj.z ? 1.0 : 0.0;
                    vec4 color0 = sampleCsmShadowColor0RawTexel(shadowData.proj.xy, 0);
                    vec4 color1 = sampleCsmShadowColor1RawTexel(shadowData.proj.xy, 0);
                    float waterDepth = color1.w * 512.0 - 128.0 - samplePos.y;
                    if (color0.a < 0.5 && color1.w > 0.001 && waterDepth > 0.1) {
                        waterHitCount += 1.0;
                        avgCaustic += dot(color0.rgb, vec3(0.333333));
                        avgWaterDepth += clamp(waterDepth / 24.0, 0.0, 1.0);
                    } else if (allLit < 1.0 && opaqueLit > 0.5) {
                        waterHitCount += 0.25;
                    }
                }
            }
            float invSteps = 1.0 / float(max(steps - 1, 1));
            float invHits = waterHitCount > 0.0 ? 1.0 / waterHitCount : 0.0;
            FragColor = vec4(
                clamp(waterHitCount * invSteps * 4.0, 0.0, 1.0),
                clamp(avgCaustic * invHits, 0.0, 1.0),
                clamp(avgWaterDepth * invHits, 0.0, 1.0),
                1.0
            );
            return;
        }
        if (uVolumetricDebugMode == 29) {
            float eta = 1.0 / 1.33;
            vec3 lightVec = refract(normalize(uShadowLightDirection), vec3(0.0, -1.0, 0.0), eta);
            float LdotV = dot(lightVec, viewDir);
            float phase = atmHenyeyGreensteinPhase(LdotV, 0.8) + atmHenyeyGreensteinPhase(LdotV, 0.6);
            FragColor = vec4(heatmap(clamp(phase * 2.0, 0.0, 1.0)), 1.0);
            return;
        }

        FragColor = vec4(max(uwResult.rgb, vec3(0.0)), uwResult.a);
        return;
    }

    // --- Lighting environment: physical sky data from SkyCapture ---
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    float dayFactor = clamp(uSkyIntensity, 0.0, 1.0);
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
    // This affects fog appearance only, not density — density is controlled by VFog profile.
    float wetness = clamp(uSkyWetness, 0.0, 1.0);
    skyFogColor *= 1.0 - 0.8 * wetness;
    directFogColor *= 1.0 - 0.8 * wetness;

    // Lightning flash: boost fog scattering so flash lights up the atmosphere.
    skyFogColor *= 1.0 + uLightningFlash * 4.0;
    directFogColor *= 1.0 + uLightningFlash * 4.0;

    // DerivativeMain VolumetricFog.glsl:193-195: airDensity = VOLUMETRIC_LIGHT_STRENGTH + wetness * BiomeSandstorm.
    // BiomeSandstorm is approximated by uWeatherStorm (desert storm intensity).
    // Gated by uVolumetricLightEnabled (DerivativeMain #ifdef VOLUMETRIC_LIGHT).
    float airDensity = (uVolumetricLightEnabled != 0)
        ? (uVFogLightStrength + wetness * uWeatherStorm) * atmRayleighPhase(LdotV) * (3.0 / max(uVolumetricMaxDistance, 1.0))
        : 0.0;

    vec3 shadowLightDir = normalize(uShadowLightDirection);

    // Debug: sun phase term for debug views (not used in main integration)
    float phaseTerm = pow(max(LdotV, 0.0), 4.0) * 0.10 + pow(max(LdotV, 0.0), 18.0) * 0.36 +
                      atmRayleighPhase(LdotV) * 0.35 * 0.11 + atmHenyeyGreensteinPhase(LdotV, 0.6) * 0.65 * 0.11;

    float strength = clamp(uVolumetricFogStrength, 0.0, 2.0);
    strength *= (uVolumetricFogEnabled != 0) ? 1.0 : 0.0;
    // DerivativeMain: mistDensity = VOLUMETRIC_FOG_DENSITY * volFogDensity.
    // uVFogDensityScale is the user-controlled volFogDensity equivalent,
    // independent of weather for Mecraft's VFog profile.
    // Tier density multiplier (0.5/1.4/9.0/48.0) is baked into CalculateVFogDensity().
    const float VFogDensityBase = 0.002;
    float baseDensity = VFogDensityBase *
                        strength *
                        max(uVolumetricBaseDensity, 0.0) *
                        max(uVFogDensityScale, 0.0);

    // DerivativeMain VolumetricFog.glsl:191: Low/Medium phase applied to mistDensity.
    // For FOG_TYPE <= 1, phase modifies density before the march loop.
    // High/Ultra (FOG_TYPE > 1) applies multi-lobe phase per-step instead.
    float mistDensity = baseDensity;
    if (uVolumetricQualityTier < 2) {
        // DerivativeMain VolumetricFog.glsl:191 — CornetteShanks phase for Low/Medium.
        // Fixed g=0.7 (no wetness modulation — fog profile is weather-independent).
        float csPhase = cornetteShanksPhase(LdotV, 0.7) * 0.45 +
                        atmHenyeyGreensteinPhase(LdotV, -0.3) * 0.15 + 0.1;
        mistDensity *= csPhase;
    }

    // DerivativeMain VolumetricFog.glsl:210-213: TIME_FADE modulates both airDensity and mistDensity.
    // Both use max(..., wetness) as a floor so rain/fog weather never fully suppresses haze.
    if (uVolumetricTimeFadeEnabled != 0) {
        float sunY = uSunDirection.y;
        float meFade = (sunY < 0.18) ? 0.37 + 1.2 * max(0.0, -sunY) : 1.7;
        float meWeight = pow(clamp(1.0 - meFade * abs(sunY - 0.18), 0.0, 1.0), 2.0);
        float timeMidnight = (sunY < 0.0 ? 1.0 : 0.0) * (1.0 - meWeight);
        airDensity *= max(clamp(meWeight + 0.25, 0.0, 1.0) + timeMidnight * 4.0, wetness);
        mistDensity *= max(meWeight * meWeight + timeMidnight * 2.0, wetness);
    }

    // DerivativeMain R1 dither: quasi-random low-discrepancy sequence based on
    // the golden ratio. Each frame shifts by 1/PHI, providing temporal variation
    // that TAA accumulates into a higher-quality result.
    // DerivativeMain lib/Head/Noise.inc:89 R1()
    float jitter;
    if (uVolumetricStaticJitter != 0) {
        // Fixed per-pixel jitter (no camera/time dependence) for stable debug
        jitter = fract(dot(vTexCoord, vec2(12.9898, 78.233)) + 0.5);
    } else {
        // DerivativeMain/world0/composite.fsh computes the half-resolution fog
        // seed as: texel = ivec2(gl_FragCoord.xy); texel *= 2; texel & 255.
        ivec2 noiseTexel = (ivec2(gl_FragCoord.xy) * 2) & ivec2(255);
        float seed = uNoiseEnabled
            ? texelFetch(uNoiseTex, noiseTexel, 0).a
            : hash13(vec3(gl_FragCoord.xy, 17.0));
        const float PHI1 = 1.61803398874;
        jitter = fract(seed + float(uFrameIndex) / PHI1);
    }
    int fogSteps = getFogSteps(marchDistance);
    float stepLength = marchDistance / float(fogSteps);
    // DerivativeMain integration form: accumulate dimensionless samples, apply radiance after.
    vec3 sunlightSample = vec3(0.0);   // shadow * phase * fogSample (per step)
    float skylightSample = 0.0;        // fogSample (per step)
    vec3 unshadowedSunSample = vec3(0.0); // debug: same units as sunlightSample without shadowing
    float transmittance = 1.0;
    float maxDensitySeen = 0.0;
    float blobTransmittanceDebug = 1.0;
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
        // DerivativeMain: fogDensity = CalculateFogDensity(rayPosition) * mistDensity + airDensity.
        // CalculateVFogDensity includes height falloff + noise structure internally.
        // airDensity provides smooth continuous haze; fogDensity provides cloud blobs.
        float vfogDensity = CalculateVFogDensity(samplePos);
        float nearFade = smoothstep(5.0, 32.0, t * marchDistance);
        float density = mistDensity * vfogDensity * nearFade;
        blobTransmittanceDebug *= exp(-density * stepLength);
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
                float d = mistDensity * CalculateVFogDensity(checkPos);
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
    if (uVolumetricDebugMode == 21) {
        // Blob-only opacity diagnostic: integrated CalculateVFogDensity without airDensity,
        // lighting, transmittance color, or weather color changes.
        float blobOpacity = 1.0 - blobTransmittanceDebug;
        FragColor = vec4(heatmap(clamp(blobOpacity * 12.0, 0.0, 1.0)), 1.0);
        return;
    }
    if (uVolumetricDebugMode == 22) {
        // Cloud shadow factor at surface position (for CLOUDS_SHADOW validation).
        // White = fully lit, black = fully shadowed.
        vec3 worldPos = reconstructWorldPosition(vTexCoord, depth < 1.0 ? depth : 0.9999);
        float cs = vfogCloudShadow(worldPos, shadowLightDir);
        FragColor = vec4(vec3(clamp(cs, 0.0, 1.0)), 1.0);
        return;
    }

    FragColor = vec4(max(scattering, vec3(0.0)), 1.0 - opacity);
}
