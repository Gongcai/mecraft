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
uniform int uReflectionDebugMode; // 0=off, 1=pixelWetness, 2=reflectance, 3=ssrHit, 4=roughness, 5=specularWeight
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

    // Roughness-adaptive parameters: smooth surfaces trace farther with finer steps;
    // rough surfaces trace shorter distances with coarser steps (result gets blurred anyway).
    float maxDistance = mix(56.0, 10.0, roughness);
    int baseSteps = int(mix(40.0, 16.0, roughness));
    float stepLength = maxDistance / float(baseSteps);

    // Dither: small offset to break up banding artifacts across pixels.
    // Uses the screen position to create a stable per-pixel pattern.
    float dither = fract(dot(gl_FragCoord.xy, vec2(0.754877669, 0.569840296))) * 0.9 + 0.1;

    vec3 rayOrigin = worldPos + reflectedDir * stepLength * dither;

    float prevT = 0.0;
    for (int i = 1; i <= baseSteps; ++i) {
        float progress = float(i) / float(baseSteps);
        // Step size grows with distance and roughness (rough cone widening)
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

        // DerivativeMain-style view-space depth comparison: relative linear depth difference.
        // More robust than absolute thickness at varying distances.
        vec4 rayClip = uViewProj * vec4(sampleWorld, 1.0);
        float rayNdcZ = rayClip.z / max(rayClip.w, 0.00001);
        float rayDepth01 = clamp(rayNdcZ * 0.5 + 0.5, 0.0, 1.0);
        float rayLin = getLinearDepth(rayDepth01);
        float sceneLin = getLinearDepth(sceneDepth);
        float relDepthDiff = abs(rayLin - sceneLin) / max(rayLin, 0.1);

        // Hit condition: scene surface is in front of or very close to the ray,
        // and the relative depth difference is within threshold.
        // As the reflection cone widens, we grow the thickness threshold.
        bool crossedSurface = sceneDepth < rayDepth01;
        float thicknessThreshold = 0.25 + roughness * t * 0.05;
        bool withinThickness = relDepthDiff < thicknessThreshold;

        if (crossedSurface && withinThickness) {
            // Binary refinement: narrow down the hit position with 4 bisection steps.
            // Each step halves the search interval, converging on the actual surface.
            vec3 lo = sampleWorld - reflectedDir * (t - prevT);
            vec3 hi = sampleWorld;
            for (int r = 0; r < 4; ++r) {
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
    float skyLightRaw01 = clamp(texture(uVoxelLightTex, vTexCoord).r, 0.0, 1.0);
    float pixelWetness = ComputePixelWetness(uSurfaceWetness, skyLightRaw01, aux.wetnessMask, normal.y);
    float puddleMask = ComputeRainPuddleMask(worldPos, uSurfaceWetness, skyLightRaw01, normal.y, aux.porosity, uTime);
    float wetFilmMask = pixelWetness * 0.25;
    float wetMix = max(wetFilmMask, puddleMask);

    if (!transMask.isTranslucent && wetMix > 1e-4) {
        float filmWetness = wetFilmMask + puddleMask;
        normal = ApplyWetNormal(normal, filmWetness);
        roughness = ApplyWetRoughness(roughness, filmWetness);
        f0Scalar = ApplyWetF0(f0Scalar, filmWetness);
    }

    if (!transMask.isTranslucent && puddleMask > 1e-4) {
        float rippleMask = smoothstep(0.18, 0.45, puddleMask);
        vec2 rainRipple = SampleRainRippleNormal(uRippleNormalTex, worldPos, 1.0, uTime, 0.60, 1.0);
        normal = normalize(mix(normal, normalize(vec3(rainRipple.x, 1.0, rainRipple.y)), rippleMask * 0.5));
    }

    // Recompute reflected direction with wet-flattened normal.
    vec3 reflectedDir = reflect(viewDir, normal);

    // GGX VNDF Importance Sampling for rough surfaces
    vec3 sampleNormal = normal;
    if (roughness > 0.0) {
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

    // Early debug: pixelWetness and roughness are available for ALL pixels.
    if (uReflectionDebugMode == 1) {
        FragColor = vec4(vec3(pixelWetness), 0.0);
        return;
    }
    if (uReflectionDebugMode == 4) {
        FragColor = vec4(vec3(roughness), 0.0);
        return;
    }

    vec3 sceneFallback = texture(uSceneLightingTex, vTexCoord).rgb;
    float smoothness = 1.0 - clamp(roughness, 0.0, 1.0);
    bool hasDerivativeReflection = transMask.isTranslucent ||
                                   aux.metalness > 0.5 ||
                                   smoothness > 0.375 ||
                                   puddleMask > 0.02;
    if (!hasDerivativeReflection) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float fresnel = pow(1.0 - clamp(dot(-viewDir, normal), 0.0, 1.0), 5.0);
    // Wet boost: DerivativeMain rain wet surfaces get stronger Fresnel and base reflectance.
    float wetReflectBoost = wetFilmMask * mix(0.02, 0.06, smoothness) +
                            puddleMask * mix(0.18, 0.34, smoothness);
    float reflectance = clamp(f0Scalar * 2.0 + smoothness * 0.18 + fresnel * 0.18 +
                              aux.porosity * 0.16 + wetReflectBoost, 0.0, 1.0);
    vec3 ssrColor = vec3(0.0);
    float ssrHit = 0.0;
    traceScreenSpaceReflection(worldPos + normal * 0.025, reflectedDir, roughness, ssrColor, ssrHit);

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
        float specularFloor = mix(0.48, 0.72, pixelWetness);
        float specular = reflectance * mix(specularFloor, 1.0, ssrHit);
        FragColor = vec4(vec3(specular), 0.0);
        return;
    }

    // Smooth sky fallback: roughness drives earlier blend to sky reflection.
    // Smooth surfaces (low roughness) get more SSR contribution before blending to sky.
    // Rough surfaces blend to sky earlier — their reflections are already blurred.
    float skyBlendRoughness = smoothstep(0.15, 0.65, roughness);
    float fallbackSkyWeight = mix(0.74, 0.95, wetMix);
    // Roughness-aware sky weight: rougher surfaces get more sky fallback
    fallbackSkyWeight = mix(fallbackSkyWeight, min(fallbackSkyWeight + 0.18, 1.0), skyBlendRoughness);
    vec3 roughSky = mix(skyReflection, skyReflection * vec3(0.82, 0.91, 1.04), roughness * 0.45);
    float fallbackMix = clamp(fallbackSkyWeight + smoothness * 0.20, 0.0, 1.0);
    vec3 fallback = mix(sceneFallback * 0.06, roughSky, fallbackMix);

    // Smooth SSR blend: when ssrHit is near zero, use a soft transition to fallback
    // instead of a hard lerp. This avoids visible seams at SSR boundaries.
    float ssrBlend = smoothstep(0.0, 0.15, ssrHit);
    vec3 color = mix(fallback, ssrColor, ssrBlend);

    // Premultiplied output (DerivativeMain convention):
    // rgb = reflection * specular, a = 1 - specular (scene pass-through)
    // Wet surfaces get a higher specular floor so reflection contributes more to final scene.
    float specularFloor = mix(0.48, 0.72, wetMix);
    float specular = reflectance * mix(specularFloor, 1.0, ssrHit);
    FragColor = vec4(max(color, vec3(0.0)) * specular, 1.0 - specular);
}
