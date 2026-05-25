out vec4 FragColor;
#include "gbuffer_contract.glsl"
#include "lighting_environment.glsl"
uniform sampler3D uAtmosphereLut;
#include "atmosphere_lut.glsl"

#ifndef MECRAFT_TRANSPARENT_COMPOSITE
#define MECRAFT_TRANSPARENT_COMPOSITE 0
#endif

in vec2 vUV;
in float vLight;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vFogDist;
in vec3 vWorldPos;
flat in float vTintKind;
flat in float vMaterialKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uLightmapDay;
uniform sampler2D uLightmapNight;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform sampler2D uOpaqueDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uSceneColorTex;
uniform sampler2D uWaterNoiseTex;
uniform int uSkyCaptureEnabled;
uniform int uCompositeInputsEnabled;
uniform int uWaterCompositeEnabled;
uniform int uForceBaseLod;
uniform int uDepthSofteningEnabled;
uniform int uFogEnabled;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uDebugLightMode; // 0=off, 1=sky light heatmap, 2=block light heatmap, 3=combined heatmap
uniform float uSkyIntensity; // 0.0-1.0, day/night interpolation factor
uniform vec3 uSunLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uMoonLightColor;
uniform float uMoonVisibility;
uniform int uAerialPerspectiveEnabled;
uniform int uVolumetricLightEnabled;
uniform int uVolumetricFogActive;
uniform float uDirectSunStrength;
uniform float uSkyAmbientStrength;
uniform float uWeatherSkylightScale;
uniform float uMinimumAmbient;
uniform float uBlockLightStrength;
uniform int uHeldBlockLightValue;
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
uniform float uPrecipitation;
uniform float uWindTime;
uniform float uAnimationTime;
uniform int uWaterEffectsEnabled;
uniform float uWaterStillFirstLayer;
uniform float uWaterStillLayerCount;
uniform float uWaterFlowFirstLayer;
uniform float uWaterFlowLayerCount;
uniform vec3 uWaterAbsorption;
uniform vec3 uCameraPos;

    const float kTwoPi = 6.28318530718;

    vec3 srgbToLinear(vec3 color) {
        return pow(max(color, vec3(0.0)), vec3(2.2));
    }

    vec3 desaturateLinear(vec3 color, float amount) {
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        return mix(color, vec3(luma), clamp(amount, 0.0, 1.0));
    }

    // Planckian locus blackbody — matches DerivativeMain's Common.inc Blackbody().
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

    bool layerInRange(float layer, float firstLayer, float layerCount) {
        return layerCount > 0.5 && layer >= firstLayer - 0.5 && layer < firstLayer + layerCount - 0.5;
    }

    bool isWaterLayer(float layer) {
        return layerInRange(layer, uWaterStillFirstLayer, uWaterStillLayerCount) ||
               layerInRange(layer, uWaterFlowFirstLayer, uWaterFlowLayerCount);
    }

    float hash12(vec2 p) {
        vec3 p3 = fract(vec3(p.xyx) * 0.1031);
        p3 += dot(p3, p3.yzx + 33.33);
        return fract((p3.x + p3.y) * p3.z);
    }

    float valueNoise(vec2 p) {
        vec2 i = floor(p);
        vec2 f = fract(p);
        vec2 u = f * f * (3.0 - 2.0 * f);
        float a = hash12(i);
        float b = hash12(i + vec2(1.0, 0.0));
        float c = hash12(i + vec2(0.0, 1.0));
        float d = hash12(i + vec2(1.0, 1.0));
        return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    }

    float waterNoise(vec2 p, float time) {
        float n = valueNoise(p * 0.72 + vec2(time * 0.035, -time * 0.021));
        n += valueNoise(p * 1.93 + vec2(-time * 0.052, time * 0.044)) * 0.52;
        n += valueNoise(p * 4.17 + vec2(time * 0.090, time * 0.027)) * 0.24;
        return n / 1.76;
    }

    bool compositeInputsActive() {
        return (MECRAFT_TRANSPARENT_COMPOSITE != 0) && (uCompositeInputsEnabled != 0);
    }

    float sampleWaterCompositeNoise(vec2 proceduralP, vec2 textureUv, vec2 wind, float timeScale) {
        float procedural = waterNoise(proceduralP, uAnimationTime * timeScale);
        if (uWaterCompositeEnabled == 0) {
            return procedural;
        }
        float externalNoise = texture(uWaterNoiseTex, textureUv + wind).r;
        return mix(procedural, externalNoise, 0.0);
    }

    vec3 applyWaterComposite(vec3 color, float alpha, float faceNormal, float depthGap, vec2 screenUv) {
        float topFace = step(-0.5, faceNormal) * step(faceNormal, 0.5);
        vec2 p = vWorldPos.xz;
        float n = sampleWaterCompositeNoise(p,
                                            p * 0.018,
                                            vec2(uAnimationTime * 0.006, -uAnimationTime * 0.004),
                                            1.0);
        float nFine = sampleWaterCompositeNoise(p * 2.35 + vec2(17.2, -9.4),
                                                (p + vec2(17.2, -9.4)) * 0.046,
                                                vec2(-uAnimationTime * 0.010, uAnimationTime * 0.008),
                                                1.37);
        float wave = (n - 0.5) * 2.0;
        float shimmer = smoothstep(0.62, 0.90, nFine) *
                        (1.0 - smoothstep(96.0, 180.0, vFogDist));

        vec3 viewDir = normalize(uCameraPos - vWorldPos);
        float facing = clamp(abs(dot(viewDir, vec3(0.0, 1.0, 0.0))), 0.0, 1.0);
        float fresnel = pow(1.0 - facing, 3.0);

        vec3 shallowTint = srgbToLinear(vec3(0.34, 0.66, 0.88));
        vec3 deepTint = max(srgbToLinear(vec3(0.06, 0.24, 0.42)) * max(uWaterAbsorption, vec3(0.001)), vec3(0.0));
        float absorption = clamp(depthGap * 280.0, 0.0, 1.0);
        float distanceAbsorption = smoothstep(12.0, 84.0, vFogDist);
        vec3 waterTint = mix(shallowTint, deepTint, max(absorption, distanceAbsorption * 0.45));

        if (compositeInputsActive()) {
            vec2 refractUv = clamp(screenUv + vec2(wave, nFine - 0.5) * (0.0015 + 0.0040 * fresnel) * topFace,
                                   vec2(0.0),
                                   vec2(1.0));
            vec3 sceneColor = texture(uSceneColorTex, refractUv).rgb;
            color = mix(color, sceneColor * waterTint, (0.08 + 0.14 * absorption) * topFace);
        }

        color = mix(color, color * waterTint, 0.34 + absorption * 0.26);
        color += shallowTint * (0.038 + 0.024 * wave + 0.055 * shimmer) * topFace;
        color += vec3(1.0) * fresnel * (0.066 + 0.060 * topFace + 0.032 * shimmer);
        return max(color, vec3(0.0));
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

    vec3 artisticSunIlluminance(vec3 sunColor, vec3 sunDir) {
        float elevation = clamp(sunDir.y, 0.0, 1.0);
        vec3 noonWarmth = vec3(1.10, 1.00, 0.84);
        vec3 lowSunWarmth = vec3(1.38, 0.82, 0.42);
        vec3 tint = mix(lowSunWarmth, noonWarmth, smoothstep(0.08, 0.62, elevation));
        float energy = mix(1.35, 1.08, smoothstep(0.04, 0.70, elevation));
        return max(sunColor * tint * energy, vec3(0.0));
    }

    // Ambient Occlusion brightness levels
    // Level 0 (fully occluded corner) = 0.72, level 3 (open) = 1.0
    // Keep the range narrow so corners are darkened but never go jet-black.
    const float aoLevels[4] = float[](0.72, 0.82, 0.91, 1.0);

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

    // DerivativeMain composite1.fsh:139-170 LAND_ATMOSPHERIC_SCATTERING
    // LUT-based physical aerial perspective (chunk version).
    // Uses atmosphere LUT for RGB transmittance and in-scattering.
    vec3 applyAerialPerspective(vec3 sceneColor,
                                vec3 worldPos,
                                float fogDistance,
                                float outdoorSkyMask,
                                vec3 warmSunColor) {
        vec3 viewDir = normalize(worldPos - uCameraPos);

        if (uAerialPerspectiveEnabled == 0) {
            float distanceTransmittance = computeFogFactor(fogDistance);
            return mix(srgbToLinear(uFogColor), sceneColor, distanceTransmittance);
        }

        float outdoorMask = smoothstep(0.05, 0.65, outdoorSkyMask);
        if (outdoorMask < 0.01) return sceneColor;

        vec3 planetCenter = vec3(0.0, atmPlanetRadius, 0.0);
        vec3 cameraPlanet = uCameraPos + planetCenter;
        float eyeR = max(length(cameraPlanet), atmAtmosphereBottomRadius + 1.0);
        vec3 cameraUp = cameraPlanet / eyeR;
        vec3 sunDir = normalize(uSunDirection);

        float eyeMu = dot(viewDir, cameraUp);
        float muS = dot(sunDir, cameraUp);
        float nu = dot(viewDir, sunDir);

        float heightDensity = 1.0 - smoothstep(96.0, 220.0, worldPos.y);

        bool groundHit = atmRayIntersectsGround(eyeR, eyeMu);
        vec3 transmittance = groundHit ? vec3(0.0) : atmGetTransmittance(eyeR, eyeMu, fogDistance, false);
        transmittance = mix(vec3(1.0), transmittance, outdoorMask * heightDensity);

        vec3 singleMieScattering;
        vec3 scattering = atmGetCombinedScattering(atmModel, eyeR, eyeMu, muS, nu, groundHit, singleMieScattering);

        float rayleighPhase = atmRayleighPhase(nu);
        float miePhase = atmHenyeyGreensteinPhase(nu, atmMiePhaseG);
        scattering = scattering * rayleighPhase + singleMieScattering * miePhase;

        // Approximate direct illuminance from sun/moon colors (no LightingEnvironment in chunk shader)
        vec3 directIlluminance = uSunLightColor * clamp(uSkyIntensity, 0.0, 1.0)
                               + uMoonLightColor * clamp(uMoonVisibility, 0.0, 1.0);
        scattering *= directIlluminance;

        float wetness = clamp(uFogWetness, 0.0, 1.0);
        scattering *= 1.0 - wetness * 0.6;

        return sceneColor * transmittance + scattering * 20.0;
    }

    vec3 decodeFaceNormal(float face) {
        if (face > -2.5 && face < -0.5) {
            return normalize(vec3(0.0, 1.0, 0.0));
        }
        int idx = int(round(face));
        if (idx == 0) return vec3(0.0, 1.0, 0.0);
        if (idx == 1) return vec3(0.0,-1.0, 0.0);
        if (idx == 2) return vec3(0.0, 0.0, 1.0);
        if (idx == 3) return vec3(0.0, 0.0,-1.0);
        if (idx == 4) return vec3(-1.0,0.0, 0.0);
        return vec3(1.0, 0.0, 0.0);
    }

    void main() {
        // Debug light visualization modes
        if (uDebugLightMode != 0) {
            float val;
            if (uDebugLightMode == 1) {
                val = vSunlight;
            } else if (uDebugLightMode == 2) {
                val = vBlockLight;
            } else {
                val = vLight;
            }
            // Heatmap: black -> blue -> cyan -> green -> yellow -> red -> white
            vec3 heatmap;
            if (val < 0.25) {
                heatmap = mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), val * 4.0);
            } else if (val < 0.5) {
                heatmap = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), (val - 0.25) * 4.0);
            } else if (val < 0.75) {
                heatmap = mix(vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), (val - 0.5) * 4.0);
            } else {
                heatmap = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (val - 0.75) * 4.0);
            }
            FragColor = vec4(heatmap, 1.0);
            return;
        }

        // Cross vegetation alpha-cutout mips can darken noticeably at distance.
        bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
        bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
        float sampledLayer = vLayer;
        if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
            float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
            sampledLayer += frame;
        }

        bool waterLayer = (uWaterEffectsEnabled != 0) && isWaterLayer(sampledLayer);
        if (MECRAFT_TRANSPARENT_COMPOSITE != 0 && !waterLayer) {
            TranslucentMask transMask = decodeTranslucentMask(vMaterialKind);
            // In the transparent composite pass, discard fragments that are actually
            // alpha-tested cutout vegetation (not true translucent surfaces).
            bool cutoutVegetation =
                isCrossVegetation ||
                (!transMask.isTranslucent && !isDerivativeEmissiveMaterialId(materialKindId(vMaterialKind)));
            if (cutoutVegetation) {
                discard;
            }
        }
        vec2 uv = vUV;
        if (waterLayer) {
            vec2 p = vWorldPos.xz;
            float n0 = waterNoise(p, uAnimationTime);
            float n1 = waterNoise(p + vec2(13.7, 5.1), uAnimationTime * 1.21);
            vec2 ripple = (vec2(n0, n1) - vec2(0.5)) * 0.040;
            float rippleDistance = 0.25 + 0.75 * (1.0 - smoothstep(18.0, 128.0, vFogDist));
            uv += ripple * rippleDistance;
        }

        vec3 sampleCoord = vec3(uv, sampledLayer);
        vec4 texColor = forceBaseLod
            ? textureLod(texArray, sampleCoord, 0.0)
            : texture(texArray, sampleCoord);

        if (texColor.a < 0.1)
            discard;

        vec3 albedo = srgbToLinear(texColor.rgb);
        if (vTintKind > 0.5 && vTintKind < 1.5) {
            albedo *= srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
        } else if (vTintKind > 1.5 && vTintKind < 2.5) {
            albedo *= srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
        }
        albedo = desaturateLinear(albedo, uAlbedoDesaturation);

        // AO: bilinear interpolate through the discrete AO levels
        // GPU smoothly interpolates vAO between vertex values (e.g., 2.3),
        // so we must NOT discretize with int() - that destroys the gradient.
        float aoIdx = clamp(vAO, 0.0, 3.0);
        int aoLow = int(aoIdx);
        int aoHigh = min(aoLow + 1, 3);
        float aoFactor = mix(aoLevels[aoLow], aoLevels[aoHigh], fract(aoIdx));

        // Lightmap lookup:
        // vBlockLight and vSunlight are raw light levels normalized to [0,1] range (level/15).
        // The lightmap image layout:
        //   X axis (left to right) = block light 0 -> 15
        //   Y axis (top to bottom) = sky light 15 -> 0 (inverted)
        // OpenGL V=0 is the top of the image (sky=15, brightest), V=1 is bottom (sky=0, darkest).
        // So we invert vSunlight: high sky level -> low V -> top of texture -> bright.
        vec2 lightmapUV = vec2(vBlockLight, 1.0 - vSunlight);
        vec3 dayLight = srgbToLinear(texture(uLightmapDay, lightmapUV).rgb);
        vec3 nightLight = srgbToLinear(texture(uLightmapNight, lightmapUV).rgb);
        vec3 vanillaLight = mix(nightLight, dayLight, clamp(uSkyIntensity, 0.0, 1.0));
        // Match DerivativeMain: the skylight channel is sky visibility, while
        // day/night intensity is supplied by the sky cache illuminance values.
        float skyLightMask = clamp(vSunlight, 0.0, 1.0);
        float nightSkyMask = clamp(vSunlight * uMoonVisibility, 0.0, 1.0);
        float outdoorSkyMask = max(skyLightMask, nightSkyMask);
        float blockLightMask = clamp(vBlockLight, 0.0, 1.0);
        vec3 normal = decodeFaceNormal(vNormal);
        vec3 sunDir = normalize(uSunDirection);
        vec3 moonDir = normalize(uMoonDirection);
        vec3 viewDir = normalize(uCameraPos - vWorldPos);
        SurfaceMaterial material = surfaceMaterialForKind(vMaterialKind, 0.0);
        float roughness = material.roughness;
        float sss = material.sss;

        // DerivativeMain-style wet surface effects (RainEffect.glsl + Terrain.frag).
        // Per-pixel wetness from surfaceWetness * outdoor exposure * upward-facing.
        float weatherW = clamp(uSurfaceWetness, 0.0, 1.0);
        float outdoorWetMask = clamp(vSunlight * 10.0 - 9.0, 0.0, 1.0);
        float upwardWet = clamp((normal.y - 0.5) / 0.4, 0.0, 1.0);
        float fwdWetness = weatherW * outdoorWetMask * upwardWet;
        if (fwdWetness > 1e-4) {
            // Wet albedo: desaturate 25% and darken 15% (DerivativeMain Terrain.frag:225)
            float wetLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
            vec3 wetAlbedo = mix(vec3(wetLuma), albedo, 0.75) * 0.85;
            albedo = mix(albedo, wetAlbedo, fwdWetness);
            // Roughness reduction: wet surfaces are smoother
            roughness = mix(roughness, max(0.08, roughness * 0.36), fwdWetness);
        }
        float rawNdotL = dot(normal, sunDir);
        float rawNdotM = dot(normal, moonDir);
        float ndotl = max(rawNdotL, 0.0);
        float ndotv = max(dot(normal, viewDir), 0.04);
        float diffuse = hammonDiffuseApprox(rawNdotL, ndotv, roughness);
        float moonDiffuse = pow(max(rawNdotM, 0.0), 0.90);

        vec3 warmSunColor = artisticSunIlluminance(uSunLightColor, sunDir);
        warmSunColor = mix(warmSunColor, warmSunColor * vec3(1.16, 1.03, 0.78), clamp(uSunWarmth, 0.0, 1.5) * 0.65);
        vec3 coolSkyColor = mix(uSkyAmbientColor, uSkyAmbientColor * vec3(0.78, 0.92, 1.18), clamp(uSkyCoolness, 0.0, 1.0));
        vec3 capturedSky = (uSkyCaptureEnabled != 0) ? sampleSkyIrradiance(normal) : coolSkyColor;
        float skyCaptureInfluence = (uSkyCaptureEnabled != 0) ? mix(0.12, 0.34, 1.0 - clamp(uSkyIntensity, 0.0, 1.0)) : 0.0;
        coolSkyColor = mix(coolSkyColor, capturedSky, skyCaptureInfluence);
        float terminatorFill = roughTerminatorFill(rawNdotL, roughness);
        vec3 directSun = warmSunColor * (diffuse + terminatorFill) * skyLightMask * uDirectSunStrength * (1.56 + 0.28 * (1.0 - roughness));
        float moonMask = nightSkyMask;
        vec3 moonFill = uMoonLightColor * moonMask * (0.026 + 0.052 * uSkyAmbientStrength);
        vec3 directMoon = uMoonLightColor * moonDiffuse * moonMask * (0.36 + 0.18 * uSkyAmbientStrength);
        float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 skyAmbient = coolSkyColor * (0.026 + 0.54 * outdoorSkyMask) *
                          uSkyAmbientStrength *
                          mix(0.48, 1.0, upward) +
                          moonFill;
        // Lightning flash: boost sky ambient so flash propagates consistently.
        skyAmbient *= 1.0 + uLightningFlash * 4.0;
        // Weather profile: scale skylight during precipitation.
        skyAmbient *= uWeatherSkylightScale;
        vec3 minimumAmbient = uShadowTintColor * uMinimumAmbient * mix(0.28, 0.92, outdoorSkyMask) * 0.62;
        float groundFacing = clamp(dot(normal, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
        vec3 fakeBounce = warmSunColor * uFakeBounceStrength * pow(skyLightMask, 4.0) * (0.28 + 0.58 * groundFacing);
        vec3 blockLightColor = mix(blackbodyApprox(3000.0), vanillaLight, 0.18);
        vec3 blockLight = blockLightColor * pow(blockLightMask, 2.2) * uBlockLightStrength;

        // Held block light: dynamic illumination from the player's held item.
        vec3 heldLight = vec3(0.0);
        if (uHeldBlockLightValue > 0) {
            float dist = length(vWorldPos - uCameraPos);
            float heldFalloff = max(1.0 - dist * 0.065, 0.0);
            heldFalloff *= heldFalloff;
            heldLight = blockLightColor * heldFalloff * float(uHeldBlockLightValue) / 15.0 * uBlockLightStrength;
        }

        vec3 lightColor = directSun + directMoon + skyAmbient + minimumAmbient + fakeBounce + blockLight + heldLight;
        lightColor = mix(lightColor, vanillaLight, 0.035);

        // Combine texture, lightmap color, and AO
        vec3 finalColor = albedo * lightColor * aoFactor;
        float backScatter = pow(max(dot(-normal, sunDir), 0.0), 1.35) * skyLightMask;
        finalColor += albedo * warmSunColor * backScatter * sss * (0.38 + 0.16 * uDirectSunStrength);
        if (isDerivativeEmissiveMaterialId(materialKindId(vMaterialKind))) {
            float emissionLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
            float emissionPeak = max(max(albedo.r, albedo.g), albedo.b);
            float emissionMask = smoothstep(0.34, 0.72, max(emissionLuma, emissionPeak * 0.72));

            // Per-type emission color (DerivativeMain BlockLighting.glsl style)
            vec3 warmBlock = blackbodyApprox(3000.0);
            bool isTorchLike = albedo.r > 0.8 || albedo.r > albedo.g * 1.4;
            bool isSeaLantern = albedo.b > albedo.r * 1.1 && albedo.b > 0.4;
            bool isRedstone = albedo.r > 0.65 && albedo.r > albedo.g * 1.8;

            vec3 emissionColor;
            if (isTorchLike) {
                emissionColor = warmBlock * 4.0;
            } else if (isSeaLantern) {
                emissionColor = vec3(1.0, 0.95, 0.92) * 2.0;
            } else if (isRedstone) {
                emissionColor = vec3(2.1, 0.9, 0.9);
            } else {
                emissionColor = mix(albedo, vec3(1.0, 0.88, 0.64) * max(emissionLuma, 0.45), 0.42);
            }
            finalColor += emissionColor * emissionMask * (0.42 + 0.64 * uBlockLightStrength);
        }
        finalColor = desaturateLinear(finalColor, (1.0 - max(diffuse, moonDiffuse * 0.65)) * outdoorSkyMask * uShadowDesaturation * 0.45);

        // Aerial perspective: skip when volumetric fog OR volumetric light is active.
        // DerivativeMain Settings.glsl: #undef LAND_ATMOSPHERIC_SCATTERING when
        // VOLUMETRIC_FOG || VOLUMETRIC_LIGHT is defined. Only apply as fallback when both are off.
        if (uFogEnabled != 0 && uVolumetricFogActive == 0 && uVolumetricLightEnabled == 0) {
            finalColor = applyAerialPerspective(finalColor, vWorldPos, vFogDist, outdoorSkyMask, warmSunColor);
        }

        float alpha = texColor.a;
        float waterDepthGap = 0.0;
        vec2 screenUv = vec2(0.0);
        if (uDepthSofteningEnabled != 0 || compositeInputsActive()) {
            vec2 opaqueDepthSize = max(vec2(textureSize(uOpaqueDepthTex, 0)), vec2(1.0));
            screenUv = gl_FragCoord.xy / opaqueDepthSize;
        }
        if (uDepthSofteningEnabled != 0 && alpha < 0.999) {
            float opaqueDepth = texture(uOpaqueDepthTex, screenUv).r;
            if (opaqueDepth < 1.0) {
                float depthGap = max(opaqueDepth - gl_FragCoord.z, 0.0);
                waterDepthGap = depthGap;
                float nearSoftening = 1.0 - smoothstep(36.0, 72.0, vFogDist);
                float contactFade = smoothstep(0.000005, 0.00035, depthGap);
                float softenedAlpha = mix(alpha, alpha * contactFade, nearSoftening * 0.45);
                alpha = max(softenedAlpha, texColor.a * 0.65);
            }
        }

        if (waterLayer) {
            finalColor = applyWaterComposite(finalColor, alpha, vNormal, waterDepthGap, screenUv);
            alpha = clamp(alpha + 0.08, texColor.a * 0.70, 0.92);
        }

        FragColor = vec4(finalColor, alpha);
    }
