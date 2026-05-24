#version 450 core
#include "gbuffer_contract.glsl"

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneLightingTex;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalAoTex;
uniform sampler2D uMaterialTex;
uniform sampler2D uMaterialAuxTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler3D uAtmosphereLut;
uniform sampler2D uNoiseTex;
uniform sampler2D uRippleNormalTex;
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
uniform float uWeatherWetness;
uniform float uSurfaceWetness;
uniform float uSkyWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uTime;
uniform sampler2D uVoxelLightTex; // GBuffer attachment 2: sky light.r, block light.g
uniform int uReflectionDebugMode; // 0=off, 1=pixelWetness, 2=reflectance, 3=ssrHit, 4=roughness, 5=specularWeight, 7=puddleMask, 8=rainSplashMask, 9=rainRippleNormal, 10=rainRippleStrength, 11=f0, 12=skyFallback, 13=reflectionRgb, 14=hasReflection, 15=skyLightRaw, 16=voxelLightRG, 17=materialAux, 18=skyGradient, 19=finalContribution, 20=reflectionSource, 21=reflectanceX32, 22=f0X32, 23=roughness, 24=reflectionSourceX8, 25=finalContributionX32, 29=reflectanceX128, 30=sourceGradientX128
uniform int uRainWetSurfacesEnabled;
uniform int uRainSurfaceRipplesEnabled;
uniform float uNearPlane;
uniform float uFarPlane;

#include "atmosphere_lut.glsl"

#include "derivative_brdf.glsl"

// PCG-based 2D hash function for high-quality pseudo-random offset
vec2 Hash2D(vec2 p) {
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p);
}

vec2 GenerateRandomOffset(vec2 screenPos, float time) {
    // Add a temporally animated offset using Golden Ratio to distribute noise over frames
    vec2 p = screenPos + fract(time * 0.6180339887498949) * 1000.0;
    return Hash2D(p);
}

#include "weather_surface.glsl"

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

vec2 projectWorldUv(vec3 worldPos) {
    vec4 clip = uViewProj * vec4(worldPos, 1.0);
    return clip.xy / max(clip.w, 0.00001) * 0.5 + 0.5;
}

// DerivativeMain GetDepthLinear: linearize depth buffer value to view-space distance.
// Uses the standard perspective projection inversion formula.
float getLinearDepth(float depth) {
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane) / (uFarPlane + uNearPlane - ndc * (uFarPlane - uNearPlane));
}

bool traceScreenSpaceReflection(vec3 worldPos,
                                vec3 reflectedDir,
                                float roughness,
                                out vec3 hitColor,
                                out float hitConfidence) {
    hitColor = vec3(0.0);
    hitConfidence = 0.0;

    // DerivativeMain ScreenSpaceReflections.glsl: RAYTRACE_SAMPLES * oneMinus(roughness)
    // Smooth surfaces trace farther with more steps; rough surfaces get fewer steps.
    float maxDistance = mix(56.0, 10.0, roughness);
    int baseSteps = int(mix(40.0, 16.0, roughness));
    float stepLength = maxDistance / float(baseSteps);

    // Dither: small offset to break up banding artifacts across pixels.
    // DerivativeMain uses InterleavedGradientNoiseTemporal; we use a PCG hash.
    float dither = fract(dot(gl_FragCoord.xy, vec2(0.754877669, 0.569840296))) * 0.9 + 0.1;

    vec3 rayOrigin = worldPos + reflectedDir * stepLength * dither;

    float prevT = 0.0;
    for (int i = 1; i <= baseSteps; ++i) {
        float progress = float(i) / float(baseSteps);
        // DerivativeMain ScreenSpaceRayTrace: adaptive step size based on depth difference.
        // Step grows with distance and roughness (rough cone widening).
        float stepScale = 1.0 + roughness * progress * 0.5;
        float t = float(i) * stepLength * stepScale;
        vec3 sampleWorld = rayOrigin + reflectedDir * t;
        vec2 uv = projectWorldUv(sampleWorld);

        // Screen bounds check with margin for refinement
        if (uv.x <= 0.002 || uv.x >= 0.998 || uv.y <= 0.002 || uv.y >= 0.998) {
            break;
        }

        float sceneDepth = texture(uDepthTex, uv).r;
        if (sceneDepth >= 0.9999) {
            prevT = t;
            continue;
        }

        // DerivativeMain ScreenSpaceRayTrace:51 — relative linear depth comparison.
        // abs(linearSample - currentDepth) / currentDepth < 0.2
        vec4 rayClip = uViewProj * vec4(sampleWorld, 1.0);
        float rayNdcZ = rayClip.z / max(rayClip.w, 0.00001);
        float rayDepth01 = clamp(rayNdcZ * 0.5 + 0.5, 0.0, 1.0);
        float rayLin = getLinearDepth(rayDepth01);
        float sceneLin = getLinearDepth(sceneDepth);
        float relDepthDiff = abs(rayLin - sceneLin) / max(rayLin, 0.1);

        // Hit condition: scene surface is in front of or very close to the ray,
        // and the relative depth difference is within threshold.
        // DerivativeMain threshold: 0.2
        bool crossedSurface = sceneDepth < rayDepth01;
        float thicknessThreshold = 0.2 + roughness * t * 0.05;
        bool withinThickness = relDepthDiff < thicknessThreshold;

        if (crossedSurface && withinThickness) {
            // DerivativeMain RAYTRACE_REFINEMENT: 6 bisection steps (default).
            // Each step halves the search interval, converging on the actual surface.
            vec3 lo = sampleWorld - reflectedDir * (t - prevT);
            vec3 hi = sampleWorld;
            for (int r = 0; r < 6; ++r) {
                vec3 mid = (lo + hi) * 0.5;
                vec2 midUv = projectWorldUv(mid);
                float midDepth = texture(uDepthTex, midUv).r;
                vec4 midClip = uViewProj * vec4(mid, 1.0);
                float midNdcZ = midClip.z / max(midClip.w, 0.00001);
                float midDepth01 = clamp(midNdcZ * 0.5 + 0.5, 0.0, 1.0);
                if (midDepth < midDepth01) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            vec3 refinedWorld = (lo + hi) * 0.5;
            vec2 refinedUv = projectWorldUv(refinedWorld);

            // Re-validate refined hit
            if (refinedUv.x < 0.001 || refinedUv.x > 0.999 || refinedUv.y < 0.001 || refinedUv.y > 0.999) {
                break;
            }

            // Edge fade: smooth falloff near screen borders
            vec2 edgeDist = min(refinedUv, 1.0 - refinedUv);
            float edgeFade = smoothstep(0.0, 0.08, min(edgeDist.x, edgeDist.y));

            // Distance fade: reflections weaken with distance
            float refinedT = length(refinedWorld - worldPos);
            float distanceFade = 1.0 - smoothstep(maxDistance * 0.3, maxDistance, refinedT);

            // Grazing angle fade: reflections at grazing angles are less reliable
            // (more likely to hit wrong surfaces or self-intersect).
            vec3 hitNormal = normalize(texture(uNormalAoTex, refinedUv).rgb * 2.0 - 1.0);
            float grazingDot = abs(dot(reflectedDir, hitNormal));
            float grazingFade = smoothstep(0.0, 0.25, grazingDot);

            // Normal facing: reject hits where the surface faces away from the ray
            float normalFacing = smoothstep(0.0, 0.2, dot(normalize(refinedWorld - worldPos), reflectedDir));

            hitConfidence = clamp(edgeFade * distanceFade * grazingFade * normalFacing * (1.0 - roughness * 0.5), 0.0, 1.0);
            hitColor = texture(uSceneLightingTex, refinedUv).rgb;
            return hitConfidence > 0.001;
        }
    }

    return false;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec4 packedMaterial = texture(uMaterialTex, vTexCoord);
    SurfaceMaterial material = unpackGBufferMaterial(packedMaterial);
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, vTexCoord));

    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vTexCoord, 1.0);
        vec3 skyDir = normalize(skyPos - uCameraPos);
        vec3 sky = sampleSkyRadianceCloudy(uSkyCaptureTex, skyDir);
        FragColor = vec4(sky, 0.0);
        return;
    }

    vec3 normal = normalize(texture(uNormalAoTex, vTexCoord).rgb * 2.0 - 1.0);
    vec3 worldPos = reconstructWorldPosition(vTexCoord, depth);
    vec3 viewDir = normalize(worldPos - uCameraPos);
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);

    // DerivativeMain wet surface — shared implementation in weather_surface.glsl
    float roughness = material.roughness;
    float f0Scalar = material.f0;
    vec2 voxelLightRaw = texture(uVoxelLightTex, vTexCoord).rg;
    float skyLightRaw01 = clamp(voxelLightRaw.r, 0.0, 1.0);
    float weatherSurfaceWetness = (uRainWetSurfacesEnabled != 0) ? uSurfaceWetness : 0.0;
    bool hasGBufferRainWetMask = uRainWetSurfacesEnabled != 0 &&
                                 !transMask.isTranslucent &&
                                 materialKindId(aux.materialKind) != MATERIAL_SKIN;
    float gbufferRainWetMask = hasGBufferRainWetMask ? aux.wetnessMask : 0.0;
    float pixelWetness = max(ComputePixelWetness(weatherSurfaceWetness, skyLightRaw01, aux.wetnessMask, normal.y),
                             gbufferRainWetMask);
    float puddleMask = saturate(gbufferRainWetMask);
    float puddleCoreMask = smoothstep(0.08, 0.65, puddleMask);
    float rainSplashMask = hasGBufferRainWetMask ? smoothstep(0.001, 0.08, length(normal.xz)) : 0.0;
    vec2 rainRippleDebug = hasGBufferRainWetMask ? normal.xz : vec2(0.0);
    float rainRippleStrengthDebug = length(rainRippleDebug) * gbufferRainWetMask;
    // DerivativeMain world0/deferred5.fsh writes specularData.x for the
    // reflection pass after Terrain.frag's wet smoothness mix. Mecraft stores
    // roughness directly, so apply that deferred5 conversion here at the same
    // consumer boundary.
    if (!transMask.isTranslucent && weatherSurfaceWetness > 1e-2) {
        float wetRoughnessScale = oneMinus(weatherSurfaceWetness * 0.3);
        roughness = sqr(sqrt(clamp(roughness, 0.0, 1.0)) * wetRoughnessScale);
        if (puddleMask > 1e-4) {
            // Terrain.frag promotes puddles through specularData.r smoothness and
            // specularData.g F0 before deferred5 converts smoothness to roughness.
            // Re-assert that contract after Mecraft's direct roughness packing so
            // the global rain wetness cannot make puddles read like nearby damp
            // blocks in the reflection pass.
            float derivativePuddleRoughness = sqr(oneMinus(puddleMask) * wetRoughnessScale);
            roughness = min(roughness, derivativePuddleRoughness);
            f0Scalar = max(f0Scalar, 0.04 * puddleMask);
        }
    }

    // Recompute reflected direction with wet-flattened normal.
    vec3 reflectedDir = reflect(viewDir, normal);

    // DerivativeMain world0/deferred6.fsh only samples a GGX facet normal when
    // material.isRough is true. Smooth rain puddles must keep the actual
    // GBuffer normal so RippleNormal can bend the reflection source visibly.
    bool materialIsRough = roughness > 0.005;

    // GGX VNDF Importance Sampling for rough surfaces
    vec3 sampleNormal = normal;
    if (materialIsRough) {
        // Construct orthonormal basis around normal (tangentToWorld)
        vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(up, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 tangentToWorld = mat3(tangent, bitangent, normal);

        // View direction in tangent space (pointing towards viewer)
        vec3 tangentView = -viewDir * tangentToWorld;

        // Generate temporal-spatial random offset
        vec2 xyNoise = GenerateRandomOffset(gl_FragCoord.xy, uTime);

        // Sample micro-normal in tangent space
        vec3 tangentHalfway = SampleGGXVNDF(tangentView, roughness, xyNoise);

        // Transform back to world space
        sampleNormal = tangentToWorld * tangentHalfway;

        // Compute reflection ray
        reflectedDir = reflect(viewDir, sampleNormal);

        // Fallback to standard reflection direction if ray points below the surface hemisphere
        if (dot(normal, reflectedDir) < 1e-6) {
            reflectedDir = reflect(viewDir, normal);
        }
    }

    vec3 skyReflection = sampleSkyRadianceCloudy(uSkyCaptureTex, reflectedDir);
    vec3 skyGradientDebug = abs(dFdx(skyReflection)) + abs(dFdy(skyReflection));

    // Early debug: pixelWetness and roughness are available for ALL pixels.
    if (uReflectionDebugMode == 1) {
        FragColor = vec4(vec3(pixelWetness), 0.0);
        return;
    }
    if (uReflectionDebugMode == 4) {
        FragColor = vec4(vec3(roughness), 0.0);
        return;
    }
    if (uReflectionDebugMode == 11) {
        FragColor = vec4(vec3(f0Scalar * 8.0), 0.0);
        return;
    }
    if (uReflectionDebugMode == 15) {
        FragColor = vec4(vec3(skyLightRaw01), 0.0);
        return;
    }
    if (uReflectionDebugMode == 16) {
        FragColor = vec4(clamp(voxelLightRaw.r, 0.0, 1.0),
                         clamp(voxelLightRaw.g, 0.0, 1.0),
                         0.0,
                         0.0);
        return;
    }
    if (uReflectionDebugMode == 17) {
        FragColor = vec4(clamp(aux.wetnessMask, 0.0, 1.0),
                         clamp(aux.porosity, 0.0, 1.0),
                         clamp(aux.metalness, 0.0, 1.0),
                         0.0);
        return;
    }
    if (uReflectionDebugMode == 7) {
        FragColor = vec4(vec3(puddleMask), 0.0);
        return;
    }
    if (uReflectionDebugMode == 8) {
        FragColor = vec4(vec3(rainSplashMask), 0.0);
        return;
    }
    if (uReflectionDebugMode == 9) {
        FragColor = vec4(vec3(abs(rainRippleDebug), 0.0) * 2.0, 0.0);
        return;
    }
    if (uReflectionDebugMode == 10) {
        FragColor = vec4(vec3(rainRippleStrengthDebug * 4.0), 0.0);
        return;
    }

    // DerivativeMain Material.inc GetMaterialData(vec2):
    // material.hasReflections = max0(0.625 - material.roughness) + material.isMetal > 5e-3.
    float derivativeReflectionMask = max(max(0.625 - clamp(roughness, 0.0, 1.0), 0.0),
                                         puddleMask * 0.625) + aux.metalness;
    bool hasDerivativeReflection = transMask.isTranslucent ||
                                   derivativeReflectionMask > 0.005;
    if (uReflectionDebugMode == 14) {
        FragColor = vec4(vec3(hasDerivativeReflection ? 1.0 : 0.0), 0.0);
        return;
    }
    if (!hasDerivativeReflection) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 ssrColor = vec3(0.0);
    float ssrHit = 0.0;
    traceScreenSpaceReflection(worldPos + normal * 0.025, reflectedDir, roughness, ssrColor, ssrHit);

    float nDotRay = max(dot(normal, reflectedDir), 0.0);
    float nDotView = max(dot(normal, -viewDir), 1e-6);
    float specular = 0.0;
    float dist = 0.0;
    bool usesRoughReflection = materialIsRough || weatherSurfaceWetness > 1e-2;
    if (usesRoughReflection) {
        vec3 halfWay = normalize(reflectedDir - viewDir);
        float lDotH = saturate(dot(reflectedDir, halfWay));
        float F = FresnelSchlick(lDotH, f0Scalar);
        float alpha2 = roughness * roughness;
        float V2 = V2SmithGGX(nDotView, max(nDotRay, 1e-6), alpha2);
        float V1Inverse = V1SmithGGXInverse(nDotView, alpha2);
        specular = nDotRay * F * V2 * V1Inverse;
        dist = saturate(max(ssrHit * 2.0, roughness * 3.0));
    } else {
        specular = FresnelDielectric(nDotView, f0Scalar);
    }
    specular *= oneMinus(aux.metalness);
    float reflectance = specular + aux.metalness;
    if (!transMask.isTranslucent && puddleMask > 1e-4) {
        // Mecraft adaptation: DerivativeMain's sky reflection source is sampled
        // from an HDR sky capture in the same composite chain. Our separated
        // reflection target plus temporal/filter passes made smooth puddles too
        // close to damp terrain, so use the same water F0 as a smooth dielectric
        // Fresnel floor only where Terrain.frag already wrote a puddle mask.
        float waterFresnel = FresnelDielectric(nDotView, max(f0Scalar, 0.04));
        reflectance = max(reflectance, waterFresnel * puddleCoreMask);
    }

    // Late debug: reflectance and ssrHit only valid after SSR trace.
    if (uReflectionDebugMode == 2) {
        FragColor = vec4(vec3(reflectance), 0.0);
        return;
    }
    if (uReflectionDebugMode == 3) {
        FragColor = vec4(vec3(ssrHit), 0.0);
        return;
    }
    if (uReflectionDebugMode == 5) {
        FragColor = vec4(vec3(reflectance * 8.0), 0.0);
        return;
    }
    if (uReflectionDebugMode == 21) {
        FragColor = vec4(vec3(reflectance * 32.0), 0.0);
        return;
    }
    if (uReflectionDebugMode == 29) {
        FragColor = vec4(vec3(reflectance * 128.0), 0.0);
        return;
    }
    if (uReflectionDebugMode == 22) {
        FragColor = vec4(vec3(f0Scalar * 32.0), 0.0);
        return;
    }
    if (uReflectionDebugMode == 23) {
        FragColor = vec4(vec3(roughness), 0.0);
        return;
    }

    // Smooth sky fallback: roughness drives earlier blend to sky reflection.
    // Smooth surfaces (low roughness) get more SSR contribution before blending to sky.
    // Rough surfaces blend to sky earlier — their reflections are already blurred.
    float skyLightmap = smoothstep(0.3, 0.8, skyLightRaw01 * skyLightRaw01 * skyLightRaw01);
    float nDotUp = saturate((dot(normal, vec3(0.0, 1.0, 0.0)) + 0.7) * 2.0) * 0.75 + 0.25;
    float skyFallbackWeight = skyLightmap * nDotUp;
    vec3 fallback = skyReflection * skyFallbackWeight;
    if (uReflectionDebugMode == 12) {
        FragColor = vec4(fallback, 0.0);
        return;
    }

    // Smooth SSR blend: when ssrHit is near zero, use a soft transition to fallback
    // instead of a hard lerp. This avoids visible seams at SSR boundaries.
    float ssrBlend = smoothstep(0.0, 0.15, ssrHit);
    vec3 color = mix(fallback, ssrColor, ssrBlend);
    if (uReflectionDebugMode == 20) {
        FragColor = vec4(color, 0.0);
        return;
    }
    if (uReflectionDebugMode == 24) {
        FragColor = vec4(color * 8.0, 0.0);
        return;
    }
    if (uReflectionDebugMode == 30) {
        vec3 sourceGradient = abs(dFdx(color)) + abs(dFdy(color));
        FragColor = vec4(sourceGradient * 128.0, 0.0);
        return;
    }

    // DerivativeMain world0/deferred6.fsh returns rgb = reflection * specular,
    // alpha = reflection distance/rough-filter data for opaque surfaces.
    vec3 reflectionRgb = max(color, vec3(0.0)) * reflectance;
    if (uReflectionDebugMode == 13) {
        FragColor = vec4(reflectionRgb * 8.0, 0.0);
        return;
    }
    if (uReflectionDebugMode == 18) {
        FragColor = vec4(skyGradientDebug * 64.0, 0.0);
        return;
    }
    if (uReflectionDebugMode == 19) {
        FragColor = vec4(reflectionRgb, 0.0);
        return;
    }
    if (uReflectionDebugMode == 25) {
        FragColor = vec4(reflectionRgb * 32.0, 0.0);
        return;
    }

    if (transMask.isTranslucent) {
        FragColor = vec4(reflectionRgb, 1.0 - clamp(reflectance, 0.0, 1.0));
    } else {
        FragColor = vec4(reflectionRgb, dist);
    }
}
