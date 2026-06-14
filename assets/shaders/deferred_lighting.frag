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
uniform sampler2D uRippleNormalTex;
uniform bool uNoiseEnabled;  // for cloud_density.glsl noise fallback

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
uniform vec3 uCloudDynamicWeather; // DerivativeMain cloudDynamicWeather.xyz: cirrocumulus/cirrus/storm
uniform int uAerialPerspectiveEnabled;
uniform int uVolumetricFogActive;
uniform int uVolumetricLightEnabled;
uniform float uShadowTintStrength;
uniform float uDirectSunStrength;
uniform float uSkyAmbientStrength;
uniform float uWeatherSkylightScale;
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
uniform float uPlanarCloudCoverage;
uniform float uPlanarCloudDensity;
uniform float uPlanarCloudAltitude;
uniform float uTime;
uniform float uCloudTimeScale;
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
uniform int uDeferredDebugMode; // 0=off, 1=direct, 2=skylight, 3=blocklight, 4=minAmbient, 5=fakeBounce, 6=scene, 7=skyDirRatio, 8=NdotL, 9=cloudShadow, 10=outdoorMask, 11=directFrac, 12=beforeAO, 13=afterAO, 14=rawSkyLight, 15=skyLightMask, 16=vertexAO, 17=SSAO, 18=normalY, 19=contactShadow, 20=puddleMask, 21=rainSplashMask, 22=rainRippleNormal
uniform int uDerivativeStrictMode; // 1=disable Mecraft extras (minimumAmbient, sky specular, fake bounce) to match DerivativeMain baseline
uniform int uRainWetSurfacesEnabled;
uniform int uRainSurfaceRipplesEnabled;

// Shadow color/normal textures (DerivativeMain shadowcolor0/1 equivalent)
uniform sampler2D uShadowColorTex;
uniform sampler2D uShadowNormalTex;

// Atmosphere precomputed scattering LUT (256x128x33 RGBA32F)
uniform sampler3D uAtmosphereLut;

#include "atmosphere_lut.glsl"

#include "cloud_density.glsl"

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
    // DerivativeMain surface cloud shadow contract:
    //   Default (CLOUDS_SHADOW off): cloudShadow = mix(1.0, 0.03, wetness)
    //   CLOUDS_SHADOW on: cloudShadow = max(CloudShadow(pos, dir), 0.03)
    // Mecraft: when override enabled, bypass all cloud shadow computation for energy diagnosis.
    if (uDirectWeatherOcclusionOverride != 0) {
        return clamp(uDirectWeatherOcclusion, 0.0, 1.0);
    }

    float wetness = clamp(uSkyWetness, 0.0, 1.0);
    float overcastShadow = mix(1.0, 0.03, wetness);

    // CLOUDS_SHADOW disabled: DerivativeMain default — weather overcast only.
    if (uCloudShadowsEnabled == 0 || outdoorMask <= 0.001) {
        return overcastShadow;
    }

    // CLOUDS_SHADOW enabled: sample real cloud density with 0.03 floor.
    // DerivativeMain surface CloudShadow() — VC (cumulus) + PC (cirrus).
    lightDir = normalize(lightDir);
    float cloudDensity = 0.0;
    vec3 checkOrigin = worldPos + vec3(0.0, cloudPlanetRadius, 0.0);

    // VC_SHADOW: cumulus cloud shadow.
    // DerivativeMain: two shell samples at 15% and 50% of thickness.
    // DerivativeMain VolumetricClouds.glsl:57: storm raises altitude — must match main renderer.
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
            float coverage = clamp(uPlanarCloudCoverage + wetness * 0.2, 0.05, 0.95);
            cloudDensity += cirrusCloudDensity(cirrusPos, coverage) * 10.0;
        }
    }

    cloudDensity = mix(0.4, cloudDensity, clamp(sqr(abs(lightDir.y) * 2.0), 0.0, 1.0));
    cloudDensity = clamp(cloudDensity, 0.0, 1.0);

    // DerivativeMain: maximum 3% direct light even in dense cloud.
    return max(exp2(-cloudDensity * cloudDensity * 2e2), 0.03);
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

vec3 sampleTransparentShadowTint(vec3 worldPos, vec3 normal, vec3 lightDir) {
    // Mecraft adaptation of DerivativeMain SunLighting.glsl colored shadows:
    // compare shadowtex0 (DepthAll) with shadowtex1 (opaque-only), then read
    // shadowcolor0 from the same cascade as the receiver.
    float viewDistance = length(worldPos - uCameraPos);
    int cascadeIndex = selectCsmCascade(viewDistance);

    normal = normalize(normal);
    lightDir = normalize(lightDir);
    float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 1.0e-3) return vec3(1.0);

    ivec3 shadowSize = textureSize(uCsmShadowMap, 0);
    float texelWorld = max(uCsmCascades[cascadeIndex].texelWorldSize, 0.0001);
    float normalOffset = shadowNormalOffsetWorld(ndotl, viewDistance, texelWorld,
                                                 uShadowDistance, uShadowNormalOffset);
    vec3 proj = csmProjectWorld(worldPos + normal * normalOffset, cascadeIndex);
    if (shadowProjOutOfBounds(proj)) return vec3(1.0);

    float bias = csmDepthBias(ndotl, viewDistance, cascadeIndex, shadowSize,
                              uShadowDistance, uShadowConstantBias, uShadowSlopeBias);
    float refZ = proj.z - bias;
    float opaqueLit = sampleCsmDepthCompare(proj.xy, cascadeIndex, refZ);
    float allLit = sampleCsmDepthAllCompare(proj.xy, cascadeIndex, refZ);
    float transparentHit = saturate(opaqueLit - allLit);
    if (transparentHit <= 1.0e-3) return vec3(1.0);

    vec4 colorSample = sampleCsmShadowColor0RawTexel(proj.xy, cascadeIndex);
    if (colorSample.a >= 0.5) return vec3(1.0);

    vec4 auxSample = sampleCsmShadowColor1RawTexel(proj.xy, cascadeIndex);
    bool waterCaster = auxSample.a > 0.20 && auxSample.a < 0.60;
    if (waterCaster) {
        float caustic = dot(colorSample.rgb, vec3(0.333333));
        float line = smoothstep(0.52, 0.92, caustic);
        line *= line;
        vec3 causticTint = vec3(1.0) + vec3(0.35, 0.85, 1.20) * line * 1.65;
        return mix(vec3(1.0), causticTint, transparentHit);
    }

    vec3 tint = pow4(colorSample.rgb);
    return mix(vec3(1.0), tint, transparentHit);
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
// Returns per-channel shadow value with colored shadow tint from transparent casters.
vec3 shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir, float sssAmount, out float outSssDepth, out float outSssWeight) {
    outSssDepth = 0.0;
    outSssWeight = 0.0;
    if (uShadowsEnabled == 0) return vec3(1.0);

    lightDir = normalize(lightDir);

    ShadowSample csm = sampleCsmShadow(worldPos, normal, lightDir);
    outSssDepth = csm.blockerDepth;
    outSssWeight = csm.sssWeight;
    float lit = csm.visibility;
    float shaped = shapeShadowVisibility(lit);
    float shadowScalar = mix(1.0, shaped, csm.fade);

    // Apply colored transparent caster tint (glass/water) from DepthAll + shadowcolor0.
    vec3 colorTint = sampleTransparentShadowTint(worldPos, normal, lightDir);
    return colorTint * shadowScalar;
}

// Overload without SSS depth output for callers that don't need it
vec3 shadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
    float unused;
    float unusedWeight;
    return shadowFactor(worldPos, normal, lightDir, 0.0, unused, unusedWeight);
}

// DerivativeMain composite1.fsh:139-170 LAND_ATMOSPHERIC_SCATTERING
// LUT-based physical aerial perspective using Bruneton precomputed atmosphere.
// Replaces the empirical fog formula with Rayleigh + Mie + ozone transmittance
// and in-scattering from the atmosphere LUT.
vec3 applyAerialPerspective(vec3 sceneColor,
                            vec3 worldPos,
                            float fogDistance,
                            float outdoorSkyMask,
                            vec3 warmSunColor,
                            LightingEnvironment env) {
    vec3 viewDir = normalize(worldPos - uCameraPos);

    // Fallback when disabled: use vanilla fog
    if (uAerialPerspectiveEnabled == 0) {
        float distanceTransmittance = computeFogFactor(fogDistance);
        return mix(srgbToLinear(uFogColor), sceneColor, distanceTransmittance);
    }

    float outdoorMask = smoothstep(0.05, 0.65, outdoorSkyMask);
    if (outdoorMask < 0.01) return sceneColor;

    // Atmosphere parameters: camera and endpoint in planet-centered coordinates
    vec3 planetCenter = vec3(0.0, atmPlanetRadius, 0.0);
    vec3 cameraPlanet = uCameraPos + planetCenter;
    float eyeR = max(length(cameraPlanet), atmAtmosphereBottomRadius + 1.0);
    vec3 cameraUp = cameraPlanet / eyeR;
    vec3 sunDir = normalize(uSunDirection);

    // Cosines for LUT parameterization
    float eyeMu = dot(viewDir, cameraUp);      // view angle at camera
    float muS = dot(sunDir, cameraUp);          // sun angle at camera
    float nu = dot(viewDir, sunDir);            // view-sun angle

    // Height density: suppress aerial fog at high altitudes
    float heightDensity = 1.0 - smoothstep(96.0, 220.0, worldPos.y);

    // RGB transmittance along the view ray (Beer-Lambert, from LUT)
    bool groundHit = atmRayIntersectsGround(eyeR, eyeMu);
    vec3 transmittance = groundHit ? vec3(0.0) : atmGetTransmittance(eyeR, eyeMu, fogDistance, false);
    transmittance = mix(vec3(1.0), transmittance, outdoorMask * heightDensity);

    // In-scattering: query combined Rayleigh+Mie scattering at eye position
    vec3 singleMieScattering;
    vec3 scattering = atmGetCombinedScattering(atmModel, eyeR, eyeMu, muS, nu, groundHit, singleMieScattering);

    // Add single Mie component (DerivativeMain Atmosphere.glsl:550-553)
    float rayleighPhase = atmRayleighPhase(nu);
    float miePhase = atmHenyeyGreensteinPhase(nu, atmMiePhaseG);
    scattering = scattering * rayleighPhase + singleMieScattering * miePhase;

    // Scale by illuminance (DerivativeMain uses directIlluminance for sun+moon)
    scattering *= env.directIlluminance;

    // Apply wetness attenuation (DerivativeMain composite1.fsh:170)
    float wetness = clamp(uFogWetness, 0.0, 1.0);
    scattering *= 1.0 - wetness * 0.6;

    // Composite: attenuate scene + add in-scattered light (scaled by 20.0 for irradiance convention)
    return sceneColor * transmittance + scattering * 20.0;
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
    bool isThinPlantReceiver = (materialKind == MATERIAL_GRASS_LIKE);
    TranslucentMask transMask = decodeTranslucentMask(surface.aux.materialKind);

    // DerivativeMain-style wet surface effects — shared implementation in weather_surface.glsl
    float skyLightRaw01 = clamp(voxelLight.r, 0.0, 1.0);
    float weatherSurfaceWetness = (uRainWetSurfacesEnabled != 0) ? uSurfaceWetness : 0.0;
    bool hasGBufferRainWetMask = uRainWetSurfacesEnabled != 0 &&
                                 !transMask.isTranslucent &&
                                 materialKind != MATERIAL_SKIN;
    float gbufferRainWetMask = hasGBufferRainWetMask ? surface.aux.wetnessMask : 0.0;
    float pixelWetness = max(ComputePixelWetness(weatherSurfaceWetness, skyLightRaw01, surface.aux.wetnessMask, normal.y),
                             gbufferRainWetMask);

    bool hasDerivativeSpecular = (max(0.625 - roughness, 0.0) + surface.aux.metalness > 0.005) ||
                                 transMask.isTranslucent;
    float derivativeSpecularMask = hasDerivativeSpecular ? 1.0 : 0.0;
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    float puddleMask = gbufferRainWetMask;
    float rainSplashMask = hasGBufferRainWetMask ? smoothstep(0.001, 0.08, length(normal.xz)) : 0.0;
    vec2 rainRippleDebug = hasGBufferRainWetMask ? normal.xz : vec2(0.0);
    float rainRippleStrengthDebug = length(rainRippleDebug) * gbufferRainWetMask;
    bool isRainWetSurface = hasGBufferRainWetMask && gbufferRainWetMask > 1e-4;
    float f0ScalarClamped = max(f0Scalar, 0.005);

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
    float NdotH = max((NdotL + NdotV) * halfwayNorm, 0.0);
    float LdotH = max((LdotV + 1.0) * halfwayNorm, 0.0);

    float ssaoRaw = (uSsaoEnabled != 0) ? texture(uSsaoTex, vTexCoord).r : 1.0;
    // Thin alpha-tested vegetation produces noisy SSAO/contact-shadow edges, but
    // it still needs CSM visibility or grass/flowers glow inside tree shadows.
    float ssao = isThinPlantReceiver ? 1.0 : ssaoRaw;
    float shadowSssDepth = 0.0;
    float shadowSssWeight = 0.0;
    vec3 shadowColored = shadowFactor(worldPos, normal, shadowLightDir, sss, shadowSssDepth, shadowSssWeight);
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
    float alpha2 = sqr(roughness);
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
    if (sss > 1e-4 && shadowSssDepth < -1e-5 && shadowSssWeight > 1e-4) {
        // DerivativeMain deferred5.fsh:268-271 — exactly 3 operations, no fill light
        vec3 sssContrib = CalculateSubsurfaceScattering(albedo, sss, shadowSssDepth, LdotV);
        // DerivativeMain deferred5.fsh:270 — sssContrib *= eyeSkylightFix
        sssContrib *= outdoorSkyMask;
        sssContrib *= shadowSssWeight;
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
            if (!isThinPlantReceiver) {
                dbgContactShadow = screenSpaceShadow(worldPos, vTexCoord, texture(uDepthTex, vTexCoord).r, shadowDither(), sss);
                shadow *= dbgContactShadow;
            }

            // DerivativeMain deferred5.fsh:289 — diffuse *= DiffuseHammon ONLY when shadow > 0
            diffuse *= DiffuseHammon(LdotV, NdotV, NdotL, NdotH, roughness, albedo);

            // Specular (DerivativeMain deferred5.fsh:291-292)
            specular = vec3(SpecularBRDF(LdotH, NdotV, rawNdotL, NdotH, alpha2, f0ScalarClamped)) *
                       mix(vec3(1.0), albedo, surface.aux.metalness);
            // DerivativeMain deferred5.fsh:297 — specular *= SPECULAR_HIGHLIGHT_BRIGHTNESS + wetnessCustom.
            specular *= 0.6 + weatherSurfaceWetness; // SPECULAR_HIGHLIGHT_BRIGHTNESS=0.6 (DerivativeMain Settings.glsl:133)

            // DerivativeMain uses saturate(mcLightmap.g * 1e6), effectively a
            // boolean gate. Mecraft's interpolated voxel skylight can cross zero
            // across underwater geometry viewed from above, which makes direct-only
            // lighting hard-cut to black. Keep the underwater camera path fully lit
            // like DerivativeMain's fixed 0.75, but fade exterior direct light with
            // the same continuous raw sky visibility used by the forward path.
            float directSkyVisibility = (uIsEyeInWater != 0) ? 1.0 : skyLightRaw;
            shadow *= directSkyVisibility;
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

    // DerivativeMain deferred5.fsh:316: additive lightning flash.
    // Adds fixed luminance so lightning lights up dark areas (night) too,
    // rather than only scaling existing skylight.
    skylight += vec3(1.0) * uLightningFlash * 1.2;

    // Weather profile: scale skylight during precipitation (rain/storm dimming).
    skylight *= uWeatherSkylightScale;

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

    // Lightning flash is now routed through sky SH (line ~652), flowing
    // consistently through skylight → scene → reflections → volumetric fog.

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
    if (uDerivativeStrictMode == 0 && !isRainWetSurface) {
        color += evaluateSkySH(skySH, normal) * FresnelSchlick(max(dot(normal, viewDir), 0.0), f0ScalarClamped) *
                 pow(oneMinus(roughness), 1.65) * (0.018 + 0.105 * outdoorSkyMask) * derivativeSpecularMask;
    }

    // Shadow desaturation (Mecraft extension, not in DerivativeMain)
    // Uses the raw 0-1 shadow value for the active sun/moon shadow light.
    float shadowDesatMask = oneMinus(activeShadow) * outdoorSkyMask;
    if (uDerivativeStrictMode == 0) {
        color = desaturateLinear(color, shadowDesatMask * uShadowDesaturation);
    }

    // Aerial perspective: skip when volumetric fog OR volumetric light is active,
    // because the volumetric march already integrates atmospheric transmittance at
    // each step. Running both causes double-fogging (double dimming + double scattering).
    // DerivativeMain Settings.glsl: #undef LAND_ATMOSPHERIC_SCATTERING when
    // VOLUMETRIC_FOG || VOLUMETRIC_LIGHT is defined. Only apply as fallback when both are off.
    if (uDerivativeStrictMode == 0 && uFogEnabled != 0 && uVolumetricFogActive == 0 && uVolumetricLightEnabled == 0) {
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
    else if (uDeferredDebugMode == 20) { color = vec3(puddleMask); }
    else if (uDeferredDebugMode == 21) { color = vec3(rainSplashMask); }
    else if (uDeferredDebugMode == 22) { color = vec3(abs(rainRippleDebug), 0.0) * 2.0; }
    else if (uDeferredDebugMode == 23) { color = vec3(rainRippleStrengthDebug * 4.0); }

    // Alpha encodes translucency: 0 = opaque, 1 = translucent (water/glass/ice/stained glass).
    // Downstream composite passes use this to apply refraction/tinting selectively.
    float translucency = transMask.isTranslucent ? 1.0 : 0.0;
    FragColor = vec4(max(color, vec3(0.0)), translucency);
}
