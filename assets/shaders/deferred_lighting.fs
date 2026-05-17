#version 450 core
#include "gbuffer_contract.glsl"
#include "lighting_environment.glsl"
#include "sky_sh.glsl"
#include "derivative_sunlight.glsl"
#include "weather_surface.glsl"

in vec2 vTexCoord;
flat in vec4 vSkySH_R;
flat in vec4 vSkySH_G;
flat in vec4 vSkySH_B;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uVoxelLightTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uDepthTex;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uShadowMapRaw;    // Raw depth for texelFetch (blockerSearch, debug)
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
uniform vec3 uShadowLightDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform int uAerialPerspectiveEnabled;
uniform int uVolumetricFogActive;
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
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uAerialReduction;
uniform float uLightningFlash;
uniform float uSurfaceWetness;
uniform float uSkyWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uDirectWeatherOcclusion;       // manual value when override enabled
uniform int uDirectWeatherOcclusionOverride; // 0=auto (skyWetness + procedural), 1=manual bypass
uniform float uPrecipitation;
uniform int uShadowsEnabled;
uniform int uSoftShadowsEnabled;
uniform int uPcssShadowsEnabled;
uniform int uContactShadowsEnabled;
uniform int uCloudShadowsEnabled;
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
uniform int uIsEyeInWater;       // DerivativeMain isEyeInWater: 1 when camera is underwater
uniform int uHeldBlockLightValue;
uniform int uHeldBlockLightValue2;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uDeferredDebugMode; // 0=off, 1=direct, 2=skylight, 3=blocklight, 4=minAmbient, 5=fakeBounce, 6=scene, 7=skyDirRatio, 8=NdotL, 9=cloudShadow, 10=outdoorMask, 11=directFrac, 12=beforeAO, 13=afterAO, 14=rawSkyLight, 15=skyLightMask, 16=vertexAO, 17=SSAO, 18=normalY, 19=contactShadow
uniform int uDerivativeStrictMode; // 1=disable Mecraft extras (minimumAmbient, sky specular, fake bounce) to match DerivativeMain baseline

// Shadow color/normal textures (DerivativeMain shadowcolor0/1 equivalent)
uniform sampler2D uShadowColorTex;
uniform sampler2D uShadowNormalTex;

// Atmosphere precomputed scattering LUT (256x128x33 RGBA32F)
uniform sampler3D uAtmosphereLut;

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

// Planckian locus blackbody — DerivativeMain/lib/Head/Common.inc Blackbody().
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

// Sky sampling — uses sampleEnvironmentSky from lighting_environment.glsl.
// Keep local sampleSkyIrradiance for normal-weighted 5-direction sampling.
vec3 sampleSkyCapture(vec3 dir) {
    return sampleEnvironmentSky(uSkyCaptureTex, dir);
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
    ivec2 size = textureSize(uShadowMapRaw, 0);
    if (texelCoord.x < 0 || texelCoord.y < 0 || texelCoord.x >= size.x || texelCoord.y >= size.y) {
        return 1.0;
    }
    float closest = texelFetch(uShadowMapRaw, texelCoord, 0).r;
    return (proj.z - bias <= closest) ? 1.0 : 0.0;
}

float sampleShadowDepthAt(vec3 proj, vec2 offsetTexels) {
    ivec2 size = textureSize(uShadowMapRaw, 0);
    vec2 uv = proj.xy + offsetTexels / vec2(size);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        return 1.0;
    }
    return texture(uShadowMapRaw, uv).r;
}


float noise2D(vec2 uv) {
    return texture(uNoiseTex, uv).r;
}

// DerivativeMain-style dither: per-pixel blue noise rotation for shadow sampling.
float shadowDither() {
    return noise2D(gl_FragCoord.xy / 256.0);
}

#define MECRAFT_SHADOW_ENABLE_STANDARD_SAMPLE
#include "mecraft_shadow.glsl"

float cloudShadowFactor(vec3 worldPos, vec3 lightDir, float outdoorMask) {
    // DerivativeMain/world0/deferred5.fsh:232 starts rainy direct light from
    // cloudShadow = mix(1.0, 0.03, wetness), before CLOUDS_SHADOW optionally
    // replaces it with sampled cloud density. Keep this weather dimming active
    // even when procedural cloud shadows are disabled.
    // Mecraft: when override enabled, bypass all cloud shadow computation
    // (overcast + procedural) for energy diagnosis.
    if (uDirectWeatherOcclusionOverride != 0) {
        return clamp(uDirectWeatherOcclusion, 0.0, 1.0);
    }

    // Auto mode: DerivativeMain overcast from skyWetness.
    float overcastShadow = mix(1.0, 0.03, clamp(uSkyWetness, 0.0, 1.0));

    if (uCloudShadowsEnabled == 0 || uCloudShadowStrength <= 0.001 || outdoorMask <= 0.001) {
        return overcastShadow;
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
    float strength = uCloudShadowStrength * outdoorMask * max(uCloudDensity, 0.0);
    float proceduralShadow = 1.0 - coverage * clamp(strength, 0.0, 0.45);

    // Mecraft keeps the procedural cloud texture when it is darker, then applies
    // the weather overcast floor so rain and storms actually remove direct light.
    return min(proceduralShadow, overcastShadow);
}

vec2 spiralDiskSample(int index, int sampleCount, float jitter) {
    float fi = float(index) + jitter;
    float radius = sqrt((float(index) + 0.5) / max(float(sampleCount), 1.0));
    float angle = fi * 2.39996323 + jitter * TAU;
    return vec2(cos(angle), sin(angle)) * radius;
}

float shapeShadowVisibility(float lit) {
    lit = clamp(lit, 0.0, 1.0);
    float contrasted = pow(lit, max(uShadowContrast, 0.001));
    return mix(clamp(uShadowMinLight, 0.0, 0.8), 1.0, contrasted);
}

vec3 sampleShadowColorTint(vec3 worldPos, vec3 normal, vec3 lightDir, float shadowVisibility) {
    // Mecraft CSM first pass is depth-only. Colored shadows / shadowcolor0 tint
    // will return after transparent shadow semantics are defined for the CSM array.
    return vec3(1.0);
}

// DerivativeMain ScreenSpaceShadow (SunLighting.glsl:88-125)
// Screen-space ray march for contact shadows.
// Mecraft adaptation: uses world-position reconstruction for depth linearization
// instead of DerivativeMain's GetDepthLinear() which uses OptiFine builtins.
// 16 steps with distance-adaptive step size and tighter z-tolerance.
float screenSpaceShadow(vec3 worldPos, vec2 screenUv, float sceneDepth, float dither, float sssAmount) {
    if (uContactShadowsEnabled == 0) return 1.0;

    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    vec3 ndcPos = clipPos.xyz / max(abs(clipPos.w), 0.0001);
    vec3 shadowLightDir = normalize(uShadowLightDirection);
    vec3 lightWorldOffset = worldPos + shadowLightDir * abs(clipPos.w) * 0.1;
    vec4 clipOffset = uViewProj * vec4(lightWorldOffset, 1.0);
    vec3 ndcOffset = clipOffset.xyz / max(abs(clipOffset.w), 0.0001);
    vec3 screenDir = normalize(vec3((ndcOffset.xy - ndcPos.xy) * 0.5, ndcOffset.z - ndcPos.z));

    // Distance-adaptive step size: finer near camera, coarser far away
    float viewDist = length(worldPos - uCameraPos);
    float distScale = clamp(viewDist / 64.0, 0.5, 2.0);
    float stepSize = max(0.008, 0.04 - sssAmount * 0.04) * uProjection[1][1] / 16.0 * distScale;
    vec3 rayStep = screenDir * stepSize;

    // Golden ratio dither to break up banding
    vec3 rayPos = vec3(screenUv, sceneDepth) + rayStep * (1.0 - sssAmount + dither * 0.75);

    // DerivativeMain: float absorption = pow(sssAmount, sqrt(length(viewPos)) * 0.5);
    float absorption = pow(clamp(sssAmount, 0.001, 1.0), sqrt(viewDist) * 0.5);

    const float zTolerance = 0.015;
    float shadow = 1.0;

    for (int i = 0; i < 16; ++i) {
        rayPos += rayStep;
        if (rayPos.x < 0.0 || rayPos.y < 0.0 || rayPos.x > 1.0 || rayPos.y > 1.0 || rayPos.z >= 1.0) break;

        float sampleDepth = texture(uDepthTex, rayPos.xy).r;
        vec3 sampleWorld = reconstructWorldPosition(rayPos.xy, sampleDepth);
        vec3 currentWorld = reconstructWorldPosition(rayPos.xy, rayPos.z);
        float linearSample = length(sampleWorld - uCameraPos);
        float linearCurrent = length(currentWorld - uCameraPos);

        if (sampleDepth < rayPos.z) {
            if (abs(linearSample - linearCurrent) / max(linearCurrent, 1e-6) < zTolerance) {
                shadow *= absorption;
            }
        }

        if (shadow < 1e-2) break;
    }
    return mix(1.0, shadow, clamp(uContactShadowStrength, 0.0, 1.0));
}

// Mecraft formal CSM shadow path.
// Returns per-channel shadow value, with colored shadows deferred until the
// transparent CSM caster contract is defined.
vec3 shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir, float sssAmount, out float outSssDepth) {
    outSssDepth = 0.0;
    if (uShadowsEnabled == 0) return vec3(1.0);

    lightDir = normalize(lightDir);

    ShadowSample csm = sampleCsmShadow(worldPos, normal, lightDir);
    float lit = csm.visibility;
    float shaped = shapeShadowVisibility(lit);
    return vec3(mix(1.0, shaped, csm.fade));
}

// Overload without SSS depth output for callers that don't need it
vec3 shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    float unused;
    return shadowFactor(worldPos, normal, lightDir, 0.0, unused);
}

vec3 aerialFogColor(vec3 baseFogColor, vec3 viewDir, float horizon, vec3 warmSunColor, LightingEnvironment env) {
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
    fogColor += env.moonIlluminance * moonForward * moonVisibility * nightFactor *
                (150.0 + 150.0 * clamp(uHorizonScatterStrength, 0.0, 2.0));
    return max(fogColor, vec3(0.0));
}

vec3 applyAerialPerspective(vec3 sceneColor,
                            vec3 worldPos,
                            float fogDistance,
                            float outdoorSkyMask,
                            vec3 warmSunColor,
                            LightingEnvironment env) {
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
    float weatherHaze = clamp(uFogWetness, 0.0, 1.0);
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

    vec3 fogColor = aerialFogColor(baseFogColor, viewDir, horizon, warmSunColor, env);
    return mix(sceneColor, fogColor, fogOpacity);
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    if (depth >= 0.9999) {
        if (uDeferredDebugMode > 0) {
            FragColor = vec4(0.0, 0.0, 0.0, 0.0);
            return;
        }
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

    // DerivativeMain-style wet surface effects — shared implementation in weather_surface.glsl
    float skyLightRaw01 = clamp(voxelLight.r, 0.0, 1.0);
    float pixelWetness = ComputePixelWetness(uSurfaceWetness, skyLightRaw01, surface.aux.wetnessMask, normal.y);

    if (!transMask.isTranslucent && pixelWetness > 1e-4) {
        albedo = ApplyWetAlbedo(albedo, surface.aux.porosity, pixelWetness);
        normal = ApplyWetNormal(normal, pixelWetness);
        roughness = ApplyWetRoughness(roughness, pixelWetness);
        f0Scalar = ApplyWetF0(f0Scalar, pixelWetness);
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
    // DerivativeMain/world0/deferred5.fsh:203:
    // mcLightmap.g = isEyeInWater == 1 ? 0.75 : cube(mcLightmap.g)
    float skyLightRaw = clamp(voxelLight.r, 0.0, 1.0);
    float skyLightMask = skyLightRaw * skyLightRaw * skyLightRaw;
    if (uIsEyeInWater != 0) {
        skyLightMask = 0.75;
    }
    float nightSkyMask = clamp(skyLightMask * uMoonVisibility, 0.0, 1.0);
    float outdoorSkyMask = max(skyLightMask, nightSkyMask);
    float blockLightMask = clamp(voxelLight.g, 0.0, 1.0);

    // ===== DerivativeMain-aligned lighting flow =====
    // Reference: deferred5.fsh main() + SunLighting.glsl + BlockLighting.glsl + BRDF.glsl

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    vec3 shadowLightDir = normalize(uShadowLightDirection);
    vec3 viewDir = normalize(uCameraPos - worldPos);

    // Debug capture variables (declared early so they can be assigned inline).
    float dbgNdotL = 0.0;
    float dbgOutdoorMask = 0.0;
    float dbgCloudShadow = 0.0;

    // Dot products using fast halfway-vector trick (DerivativeMain deferred5.fsh:213-227).
    // DerivativeMain's worldLightVector follows the active celestial shadow light; in Mecraft
    // that is uShadowLightDirection, which switches to the moon at night.
    float rawNdotL = dot(normal, shadowLightDir);
    float rawNdotM = dot(normal, moonDir);
    float LdotV = dot(shadowLightDir, viewDir);
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(rawNdotL, 0.0);
    float NdotM = max(rawNdotM, 0.0);
    dbgNdotL = NdotL;
    // Fast halfway vector: avoids normalize(lightDir + viewDir) per-pixel
    float halfwayNorm = inversesqrt(2.0 * LdotV + 2.0);
    float NdotH = max((rawNdotL + dot(normal, viewDir)) * halfwayNorm, 0.0);
    float LdotH = max((LdotV + 1.0) * halfwayNorm, 0.0);

    float ssao = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    float shadowSssDepth = 0.0;
    vec3 shadowColored = shadowFactor(worldPos, normal, shadowLightDir, sss, shadowSssDepth);
    float cloudShadow = cloudShadowFactor(worldPos, shadowLightDir, outdoorSkyMask);
    float sunShadow = (uShadowLightMode == 0) ? dot(shadowColored, vec3(0.333)) : 1.0;
    float moonShadow = (uShadowLightMode == 1) ? dot(shadowColored, vec3(0.333)) : 1.0;
    float activeShadow = (uShadowLightMode == 1) ? moonShadow : sunShadow;
    dbgOutdoorMask = outdoorSkyMask;
    dbgCloudShadow = cloudShadow;

    // --- Lighting environment from SkyCapture (unified data source) ---
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);
    vec3 directIlluminance = env.directIlluminance;
    // skyIlluminance is metadata irradiance, not used by deferred SH skylight.
    // SH radiance and metadata irradiance are different units — see comment at line 623.

    vec3 warmSunColor = artisticSunIlluminance(uSunLightColor, sunDir);
    warmSunColor = mix(warmSunColor, warmSunColor * vec3(1.16, 1.03, 0.78), clamp(uSunWarmth, 0.0, 1.5) * 0.65);

    // --- BRDF preparation (DerivativeMain BRDF.glsl — now via derivative_brdf.glsl include) ---
    float alpha = max(roughness * roughness, 0.002);
    float alpha2 = alpha * alpha;
    float f0ScalarClamped = max(f0Scalar, 0.005);

    // === DerivativeMain lighting order ===
    // Reference: deferred5.fsh main() — sceneData starts at 0

    // Initialize sceneData (DerivativeMain deferred5.fsh:194)
    vec3 sceneData = vec3(0.0);

    // Component tracking for debug views (uDeferredDebugMode)
    vec3 dbgDirect = vec3(0.0);
    vec3 dbgSkylight = vec3(0.0);
    vec3 dbgBlocklight = vec3(0.0);
    vec3 dbgMinAmbient = vec3(0.0);
    vec3 dbgFakeBounce = vec3(0.0);
    vec3 dbgBeforeAO = vec3(0.0);
    float dbgSkylightDirectRatio = 0.0;
    float dbgContactShadow = 1.0;

    // 1. Sunlight setup: 64 * waterTint * SUNLIGHT_INTENSITY * directIlluminance * cloudShadow
    // DerivativeMain deferred5.fsh:240 — underwater waterTint attenuates sunlight
    vec3 waterTint = vec3(1.0);
    if (uIsEyeInWater != 0) {
        // DerivativeMain: vec3(0.6, 0.9, 1.2) / max(3.0, opaqueDepth * 0.1 * WATER_FOG_DENSITY)
        // Blue-green attenuation that increases with depth
        float waterDensity = 0.1; // WATER_FOG_DENSITY default
        float attenuation = max(3.0, length(worldPos - uCameraPos) * waterDensity);
        waterTint = vec3(0.6, 0.9, 1.2) / attenuation;
    }
    vec3 sunlightMult = waterTint * directIlluminance * 64.0 * uDirectSunStrength * cloudShadow;
    // DerivativeMain: diffuse = vec3(1.0) — only multiplied by DiffuseHammon when shadow > 0
    vec3 diffuse = vec3(1.0);

    // 2. SSS (DerivativeMain SunLighting.glsl:176-188 — now via derivative_sunlight.glsl include)
    //    DerivativeMain deferred5.fsh:267-272: SSS is added to sceneData BEFORE shadow/diffuse
    if (sss > 1e-4 && shadowSssDepth < -1e-5) {
        // DerivativeMain deferred5.fsh:268-271 — exactly 3 operations, no fill light
        vec3 sssContrib = CalculateSubsurfaceScattering(albedo, sss, shadowSssDepth, LdotV);
        // DerivativeMain deferred5.fsh:270 — sssContrib *= eyeSkylightFix
        sssContrib *= outdoorSkyMask;
        // DerivativeMain deferred5.fsh:270 — sunlightMult MUST be reduced BEFORE SSS accumulation
        sunlightMult *= oneMinus(sss * 0.5);
        sceneData += sssContrib * sunlightMult;
    }

    // 3. Shadow / specular computation (DerivativeMain deferred5.fsh:276-300)
    vec3 shadow = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec3 directVisibilityDebug = vec3(0.0);
    if (NdotL > 1e-3) {
        // DerivativeMain: shadow = PercentageCloserFilter(...)
        shadow = shadowColored;
        shadow = mix(shadow, vec3(1.0), saturate(pow16(rcp(uShadowDistance * uShadowDistance) * dotSelf(worldPos - uCameraPos))));

        if (maxOf(shadow) > 1e-6) {
            // DerivativeMain: shadow *= ScreenSpaceShadow (contact shadows)
            dbgContactShadow = screenSpaceShadow(worldPos, vTexCoord, texture(uDepthTex, vTexCoord).r, shadowDither(), sss);
            shadow *= dbgContactShadow;

            // DerivativeMain deferred5.fsh:289 — diffuse *= DiffuseHammon ONLY when shadow > 0
            diffuse *= DiffuseHammon(LdotV, NdotV, NdotL, NdotH, roughness, albedo);

            // Specular (DerivativeMain deferred5.fsh:291-292)
            specular = vec3(SpecularBRDF(LdotH, NdotV, rawNdotL, NdotH, alpha2, f0ScalarClamped)) *
                       mix(vec3(1.0), albedo, surface.aux.metalness);
            // DerivativeMain deferred5.fsh:293 — specular *= SPECULAR_HIGHLIGHT_BRIGHTNESS + wetnessCustom
            specular *= 0.6 + uSurfaceWetness; // SPECULAR_HIGHLIGHT_BRIGHTNESS=0.6 (DerivativeMain Settings.glsl:133)

            // DerivativeMain deferred5.fsh:299 — shadow *= saturate(mcLightmap.g * 1e6)
            // Indoor surfaces with sky light = 0 get no direct sunlight
            shadow *= saturate(voxelLight.r * 1e6);
            // Diagnostic view: show direct visibility including weather/cloud
            // attenuation, but without HDR directIlluminance to avoid pure white.
            directVisibilityDebug = clamp(shadow * diffuse * NdotL * cloudShadow, vec3(0.0), vec3(1.0));
            // DerivativeMain deferred5.fsh:300 — shadow *= sunlightMult
            shadow *= sunlightMult;
        }
    }

    // 4. (DerivativeMain has no SSS fill light — removed self-invented extension)

    // DerivativeMain time weights (shaders.properties:125-131)
    // Computed from worldSunVectorY (= uSunDirection.y). meWeight peaks at horizon
    // (y=0.18) and the four weights sum to 1.0 at all times.
    float sunY = uSunDirection.y;
    float sunX = uSunDirection.x;
    float meFade = (sunY < 0.18) ? 0.37 + 1.2 * max(0.0, -sunY) : 1.7;
    float meWeight = pow(clamp(1.0 - meFade * abs(sunY - 0.18), 0.0, 1.0), 2.0);
    float timeNoon = (sunY > 0.0 ? 1.0 : 0.0) * (1.0 - meWeight);
    float timeMidnight = (sunY < 0.0 ? 1.0 : 0.0) * (1.0 - meWeight);
    float timeSunrise = (sunX > 0.0 ? 1.0 : 0.0) * meWeight;
    float timeSunset = (sunX < 0.0 ? 1.0 : 0.0) * meWeight;

    // 5. Skylight (DerivativeMain deferred5.fsh:305-323)
    // SkySH is computed once in the vertex shader (3 invocations) and passed via
    // flat interpolation, matching DerivativeMain's deferred5.vsh approach.
    SkySH skySH;
    skySH.R = vSkySH_R;
    skySH.G = vSkySH_G;
    skySH.B = vSkySH_B;
    vec3 skylight = evaluateSkySH(skySH, normal);
    skylight *= normal.y * 2.0 + 3.0;  // directional boost 1.0 (down) to 5.0 (up)

    // Wetness blend: under rain, lerp toward flat skySunLight (DerivativeMain deferred5.fsh:319)
    vec3 skySunLight = (normal.y * 0.24 + 0.4) * directIlluminance;
    skylight = mix(skylight, skySunLight, uSkyWetness * 0.7);
    // DerivativeMain/world0/deferred5.fsh:316
    skylight *= 0.8 - uSkyWetness * 0.2;

    // DerivativeMain keeps skylight independent from the shadow map; only direct
    // light is shadowed. Shadow-tinting skylight makes sun/moon shadows collapse
    // into black ambient patches and breaks daytime contrast.
    vec3 skylightContrib = skylight * skyLightMask;
    sceneData += skylightContrib;
    dbgSkylight = skylightContrib;

    // Skylight/direct ratio: this is the useful contrast diagnostic for shadows.
    // DerivativeMain uses sky radiance SH directly for skylight, while skyIlluminance
    // metadata is a separate atmosphere irradiance value, so comparing those 1:1 is
    // not unit-equivalent. What matters visually is how much sky fill competes with
    // the fully-lit direct term.
    float skylightLum = dot(skylightContrib, vec3(0.2126, 0.7152, 0.0722));
    float directPotentialLum = dot(sunlightMult * diffuse, vec3(0.2126, 0.7152, 0.0722));
    dbgSkylightDirectRatio = (directPotentialLum > 1e-6) ? skylightLum / directPotentialLum : 0.0;

    // Basic brightness (DerivativeMain deferred5.fsh:325)
    // sceneData += BASIC_BRIGHTNESS + nightVision * 0.1
    sceneData += 0.0005; // BASIC_BRIGHTNESS (DerivativeMain Settings.glsl:99)

    // Minimum ambient + fake bounce
    // DerivativeMain strict: these are Mecraft extensions, not in DerivativeMain deferred5.fsh
    float minimumAmbientMask = mix(0.35, 1.0, outdoorSkyMask);
    vec3 minAmbientContrib = uShadowTintColor * uMinimumAmbient * minimumAmbientMask * 0.62;
    if (uDerivativeStrictMode == 0) { sceneData += minAmbientContrib; }
    dbgMinAmbient = minAmbientContrib;
    // DerivativeMain: CalculateFakeBouncedLight (SunLighting.glsl:168-174)
    float bounce = CalculateFakeBouncedLight(normal, shadowLightDir);
    vec3 bounceContrib = bounce * sqr(skyLightMask) * sunlightMult * uFakeBounceStrength;
    if (uDerivativeStrictMode == 0) { sceneData += bounceContrib; }
    dbgFakeBounce = bounceContrib;

    // GI / AO (DerivativeMain deferred5.fsh:329-347)
    // AO multiplies accumulated diffuse+skylight (before blocklight)
    dbgBeforeAO = sceneData;
    sceneData *= ssao * vertexAo;

    // === Block lighting (DerivativeMain BlockLighting.glsl) ===
    vec3 blocklightColor = blackbodyApprox(3000.0);
    float albedoLuminance = length(albedo);
    // DerivativeMain: albedoRaw = texelFetch(colortex6, texel, 0).rgb — raw sRGB from GBuffer.
    // Mecraft's albedo is already linear; reconstruct sRGB equivalent.
    vec3 albedoRaw = LinearToSRGB(albedo);
    float lightSourceMask = 1.0;

    // DerivativeMain BlockLighting.glsl:9 — GetBlocklightFalloff(mcLightmap.r)
    // Applies nonlinear remap to block light channel before use.
    float mcLightmapR = blockLightMask;
    GetBlocklightFalloff(mcLightmapR);

    // Per-materialID emission (BlockLighting.glsl:15-89) — EMISSION_MODE 0
    vec3 EmissionColor = vec3(0.0);
    switch (materialKind) {
    // Total glowing — DerivativeMain case 20/36
        case MATERIAL_TOTAL_GLOWING: case MATERIAL_TEXTURED_EMISSIVE:
            EmissionColor += albedoLuminance;
            lightSourceMask = 0.1;
            break;
    // Torch like — DerivativeMain case 21
        case MATERIAL_TORCH_LIKE:
            EmissionColor += 4.0 * blocklightColor * float(albedoRaw.r > 0.8 || albedoRaw.r > albedoRaw.g * 1.4);
            lightSourceMask = 0.15;
            break;
    // Fire — DerivativeMain case 22/15
        case MATERIAL_FIRE: case MATERIAL_LAVA:
            EmissionColor += 6.0 * blocklightColor * cube(albedoLuminance);
            lightSourceMask = 0.1;
            break;
    // Glowstone like — DerivativeMain case 23
        case MATERIAL_GLOWSTONE_LIKE:
            EmissionColor += 2.5 * blocklightColor * cube(albedoLuminance);
            lightSourceMask = 0.15;
            break;
    // Sea lantern like — DerivativeMain case 24
        case MATERIAL_SEA_LANTERN_LIKE:
            EmissionColor += 2.0 * cube(albedoLuminance);
            lightSourceMask = 0.0;
            break;
    // Redstone — DerivativeMain case 25 (top/bottom distinction)
        case MATERIAL_REDSTONE:
            if (fract(worldPos.y) > 0.18) EmissionColor += step(0.65, albedoRaw.r);
            else EmissionColor += step(1.25, albedo.r / (albedo.g + albedo.b)) * step(0.5, albedoRaw.r);
            EmissionColor *= vec3(2.1, 0.9, 0.9);
            break;
    // Soul fire — DerivativeMain case 26
        case MATERIAL_SOUL_FIRE:
            EmissionColor += (albedoLuminance + 0.6) * step(0.53, albedoRaw.b);
            lightSourceMask = 0.5;
            break;
    // Amethyst — DerivativeMain case 27
        case MATERIAL_AMETHYST:
            EmissionColor += min(mcLightmapR * 2e2 + 0.05, 2.0) * pow(albedoLuminance, min(mcLightmapR * 1e2, 2.5));
            break;
    // Glowberry — DerivativeMain case 28
        case MATERIAL_GLOWBERRY:
            EmissionColor += saturate(dot(saturate(albedo - 0.1), vec3(1.0, -0.6, -0.99))) * vec3(28.0, 25.0, 21.0);
            lightSourceMask = 0.4;
            break;
    // Rails — DerivativeMain case 29
        case MATERIAL_RAILS:
            EmissionColor += vec3(2.1, 0.9, 0.9) * albedoLuminance * step(albedoRaw.g * 2.0 + albedoRaw.b, albedoRaw.r);
            break;
    // Beacon core — DerivativeMain case 30
        case MATERIAL_BEACON_CORE: {
            // DerivativeMain: fract(worldPos + cameraPosition) — their worldPos excludes camera.
            // Mecraft's worldPos already includes camera, so just use fract(worldPos).
            vec3 midBlockPos = abs(fract(worldPos) - 0.5);
            float maxComp = max(max(midBlockPos.x, midBlockPos.y), midBlockPos.z);
            if (maxComp < 0.4 && albedo.b > 0.5) EmissionColor += 6.0 * albedoLuminance;
            lightSourceMask = 0.2;
            break;
        }
    // Sculk — DerivativeMain case 31
        case MATERIAL_SCULK:
            EmissionColor += 0.04 * sqr(albedoLuminance) * float((albedoRaw.b * 2.0 > albedoRaw.r + albedoRaw.g) && albedoRaw.b > 0.55);
            break;
    // Glow lichen — DerivativeMain case 32
        case MATERIAL_GLOW_LICHEN:
            if (albedoRaw.r > albedoRaw.b * 1.2) EmissionColor += 3.0;
            else EmissionColor += albedoLuminance * 0.1;
            break;
    // Partial glowing — DerivativeMain case 33
        case MATERIAL_PARTIAL_GLOWING:
            EmissionColor += 30.0 * albedoLuminance * cube(saturate(albedo - 0.5));
            lightSourceMask = 0.5;
            break;
    // Middle glowing — DerivativeMain case 34
        case MATERIAL_MIDDLE_GLOWING: {
            // DerivativeMain: fract(worldPos.xz + cameraPosition.xz) — same cameraPosition note.
            vec2 midBlockPosXZ = abs(fract(worldPos.xz) - 0.5);
            float maxCompXZ = max(midBlockPosXZ.x, midBlockPosXZ.y);
            EmissionColor += step(maxCompXZ, 0.063) * albedoLuminance;
            break;
        }
    }

    // DerivativeMain BlockLighting.glsl:91 — sceneData += EmissionColor * TORCHLIGHT_BRIGHTNESS
    vec3 sceneDataBeforeBlocklight = sceneData; // snapshot for debug
    sceneData += EmissionColor * uBlockLightStrength;

    // EMISSION_MODE 1: material.emissiveness with brightness and curve.
    // DerivativeMain: sceneData += material.emissiveness * 1.5 * EMISSION_BRIGHTNESS
    // EMISSION_CURVE (2.2) is applied during G-buffer unpacking (unpackGBufferMaterial),
    // matching DerivativeMain GetMaterialData() which applies pow(x, EMISSIVE_CURVE).
    if (materialEmission > 0.01) {
        sceneData += materialEmission * 1.5 * uBlockLightStrength;
    }

    // Emissive ores (BlockLighting.glsl:98-109, EMISSIVE_ORES)
    if (materialKind == MATERIAL_ORE) {
        float isOre = saturate((max(max(dot(albedoRaw, vec3(2.0, -1.0, -1.0)),
                                       dot(albedoRaw, vec3(-1.0, 2.0, -1.0))),
                                   dot(albedoRaw, vec3(-1.0, -1.0, 2.0))) - 0.1) * rcp(0.3));
        // DerivativeMain: LinearToSRGB(isOre * pow5(max0(albedoRaw - 0.1))) * 2.0
        sceneData += LinearToSRGB(isOre * pow5(max0(albedoRaw - vec3(0.1)))) * 2.0;
    }
    if (materialKind == MATERIAL_NETHER_ORE) {
        float isNetherOre = saturate(dot(albedoRaw, vec3(-20.0, 30.0, 10.0)));
        // DerivativeMain: LinearToSRGB(isNetherOre * cube(max0(albedoRaw - 0.1))) * 2.0
        sceneData += LinearToSRGB(isNetherOre * cube(max0(albedoRaw - vec3(0.1)))) * 2.0;
    }

    // Blocklight falloff (BlockLighting.glsl:111-115, Overworld)
    // DerivativeMain: mcLightmap.r * (ao * oneMinus(mcLightmap.r) + mcLightmap.r) * 2.0 * blocklightColor * TORCHLIGHT_BRIGHTNESS * lightSourceMask
    // Note: mcLightmapR has already been through GetBlocklightFalloff.
    if (mcLightmapR > 1e-5) {
        sceneData += mcLightmapR * (ssao * oneMinus(mcLightmapR) + mcLightmapR) *
                     2.0 * blocklightColor * uBlockLightStrength * lightSourceMask;
    }

    // Held torchlight (BlockLighting.glsl:117-128, Overworld)
    // DerivativeMain: uses heldBlockLightValue/heldBlockLightValue2 OptiFine builtins.
    int heldLightMax = max(uHeldBlockLightValue, uHeldBlockLightValue2);
    if (heldLightMax > 0) {
        // DerivativeMain: falloff = rcp(dotSelf(worldPos) + 1.0)
        // DerivativeMain's worldPos is relative to camera; Mecraft's is absolute.
        vec3 heldPos = worldPos - uCameraPos;
        float falloff = rcp(dotSelf(heldPos) + 1.0);
        // DerivativeMain: falloff *= fma(NdotV, 0.8, 0.2)
        falloff *= fma(NdotV, 0.8, 0.2);
        // DerivativeMain Overworld: falloff * (ao * oneMinus(falloff) + falloff) * 0.2 * max(held...) * HELDLIGHT_BRIGHTNESS * blocklightColor
        sceneData += falloff * (ssao * oneMinus(falloff) + falloff) * 0.2 * float(heldLightMax) * uBlockLightStrength * blocklightColor;
    }

    // Hardcoded material-specific additions (BlockLighting.glsl:130)
    sceneData += float(materialKind == 12) * 12.0 +
                 float(materialKind == 36) * 2.0 +
                 float(materialKind == 19) * albedoLuminance * 2e2;

    dbgBlocklight = sceneData - sceneDataBeforeBlocklight;

    // [Phase 0] Lightning flash: brief intense illumination during storms.
    // Temporary hack — does not flow through sky capture, cloud, or volumetric fog.
    // TODO: Phase 1 should route lightningColor through skylight SH, cloud shadow,
    // and volumetric fog for consistent scene-wide flash.
    if (uLightningFlash > 0.001) {
        sceneData += directIlluminance * uLightningFlash * 4.0 * outdoorSkyMask;
    }

    // === DerivativeMain compositing (deferred5.fsh:352-357) ===
    // DerivativeMain order: sceneData += shadow * diffuse → sceneData *= albedo → sceneData *= oneMinus(isMetal) → sceneData += shadow * specular

    // Add shadow * diffuse BEFORE albedo multiply
    // (DerivativeMain deferred5.fsh:352: sceneData += shadow * diffuse)
    dbgDirect = directVisibilityDebug;
    sceneData += shadow * diffuse;

    // Multiply by albedo AFTER all diffuse/ambient/emission accumulation
    // (DerivativeMain deferred5.fsh:353: sceneData *= albedo)
    sceneData *= albedo;

    // Metal mask: DerivativeMain deferred5.fsh:355
    // if (isEyeInWater == 0) material.isMetal *= 0.2 * smoothstep(0.3, 0.8, mcLightmap.g) + 0.8;
    float metalMask = surface.aux.metalness;
    if (uIsEyeInWater == 0) {
        metalMask *= 0.2 * smoothstep(0.3, 0.8, voxelLight.r) + 0.8;
    }
    sceneData *= oneMinus(metalMask);

    // Additive specular on top (not multiplied by albedo)
    // (DerivativeMain deferred5.fsh:356: sceneData += shadow * specular)
    // Note: shadow already contains sunlightMult, and specular already contains SPECULAR_HIGHLIGHT_BRIGHTNESS + wetnessCustom
    sceneData += shadow * specular;

    vec3 color = sceneData;

    // Sky specular (environment reflection) — Mecraft extension, not in DerivativeMain deferred5.fsh
    // DerivativeMain handles sky reflections in a separate SSR pass (deferred6.fsh)
    if (uDerivativeStrictMode == 0) {
        color += evaluateSkySH(skySH, normal) * FresnelSchlick(max(dot(normal, viewDir), 0.0), f0ScalarClamped) *
                 pow(oneMinus(roughness), 1.65) * (0.018 + 0.105 * outdoorSkyMask) * derivativeSpecularMask;
    }

    // Shadow desaturation (Mecraft extension, not in DerivativeMain)
    // Uses the raw 0-1 shadow value for the active sun/moon shadow light.
    float shadowDesatMask = oneMinus(activeShadow) * outdoorSkyMask;
    if (uDerivativeStrictMode == 0) {
        color = desaturateLinear(color, shadowDesatMask * uShadowDesaturation);
    }

    // Aerial perspective: skip when volumetric fog is active, because the volumetric
    // march already integrates atmospheric transmittance at each step. Running both
    // causes double-fogging (double dimming + double scattering).
    // When volumetric fog is off, this provides the fallback land atmospheric scattering.
    if (uDerivativeStrictMode == 0 && uFogEnabled != 0 && uVolumetricFogActive == 0) {
        float fogDistance = length(worldPos - uCameraPos);
        color = applyAerialPerspective(color, worldPos, fogDistance, outdoorSkyMask, warmSunColor, env);
    }

    // Lighting component debug views (uDeferredDebugMode)
    // These show individual contributions BEFORE albedo/specular/fog, useful for diagnosing
    // which component drives nighttime brightness.
    if (uDeferredDebugMode == 1) { color = dbgDirect; }
    else if (uDeferredDebugMode == 2) { color = dbgSkylight + 0.0005; }
    else if (uDeferredDebugMode == 3) { color = dbgBlocklight; }
    else if (uDeferredDebugMode == 4) { color = dbgMinAmbient; }
    else if (uDeferredDebugMode == 5) { color = dbgFakeBounce; }
    else if (uDeferredDebugMode == 6) { color = sceneData; } // before aerial/fog
    else if (uDeferredDebugMode == 7) {
        // Skylight/direct ratio heatmap: green≈0.25 balanced, red means sky fill
        // is too strong relative to direct light and shadows will look washed out.
        float r = clamp(dbgSkylightDirectRatio / 0.25, 0.0, 2.0);
        color = vec3(
            smoothstep(1.0, 2.0, r),           // red channel: overbright
            1.0 - abs(r - 1.0),                // green channel: peak at 1.0
            smoothstep(1.0, 0.0, r)            // blue channel: underbright
        );
    }
    else if (uDeferredDebugMode == 8) { color = vec3(dbgNdotL); }
    else if (uDeferredDebugMode == 9) { color = vec3(dbgCloudShadow); }
    else if (uDeferredDebugMode == 10) { color = vec3(dbgOutdoorMask); }
    else if (uDeferredDebugMode == 11) {
        // Direct energy fraction: how much direct contributes to total diffuse.
        float dLum = dot(dbgDirect, vec3(0.2722, 0.6741, 0.0537));
        float sLum = dot(dbgSkylight, vec3(0.2722, 0.6741, 0.0537));
        color = vec3(dLum / max(dLum + sLum, 0.001));
    }
    else if (uDeferredDebugMode == 12) { color = dbgBeforeAO; }
    else if (uDeferredDebugMode == 13) { color = sceneData; }
    else if (uDeferredDebugMode == 14) { color = vec3(skyLightRaw); }
    else if (uDeferredDebugMode == 15) { color = vec3(skyLightMask); }
    else if (uDeferredDebugMode == 16) { color = vec3(vertexAo); }
    else if (uDeferredDebugMode == 17) { color = vec3(ssao); }
    else if (uDeferredDebugMode == 18) { color = vec3(normal.y * 0.5 + 0.5); }
    else if (uDeferredDebugMode == 19) { color = vec3(dbgContactShadow); }

    // Alpha encodes translucency: 0 = opaque, 1 = translucent (water/glass/ice/stained glass).
    // Downstream composite passes use this to apply refraction/tinting selectively.
    float translucency = transMask.isTranslucent ? 1.0 : 0.0;
    FragColor = vec4(max(color, vec3(0.0)), translucency);
}
