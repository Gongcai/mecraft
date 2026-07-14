#version 450 core
#include "gbuffer_contract.glsl"
#include "render_contract.glsl"
#include "rhi_screen_coordinates.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uSceneLightingTex;
layout(binding = 1) uniform sampler2D uReflectionTex;
layout(binding = 2) uniform sampler2D uCloudTex;
layout(binding = 3) uniform sampler2D uDepthTex;
layout(binding = 4) uniform sampler2D uNormalAoTex;
layout(binding = 5) uniform sampler2D uMaterialTex;
layout(binding = 6) uniform sampler2D uMaterialAuxTex;
layout(binding = 7) uniform sampler2D uSkyCaptureTex;
layout(binding = 8) uniform sampler2D uAlbedoTex;
layout(binding = 9) uniform sampler2D uSsgiTex;
#ifdef MECRAFT_SCENE_VOXEL_GI
layout(binding = 10) uniform sampler3D uVoxelGiTex;
#endif

layout(std140, binding = 11) uniform SceneCompositeParams {
    mat4 pInvViewProj;
    vec4 pCameraPosSkyIntensity;
    vec4 pSunDirectionVisibility;
    vec4 pMoonDirectionVisibility;
    vec4 pAtmosphereComposite;
    vec4 pReflectionWater;
    vec4 pStatus;
    vec4 pVoxelOriginSize;
    vec4 pVoxel0;
    vec4 pVoxel1;
    vec4 pVoxel2;
    ivec4 pFlags0;
    ivec4 pFlags1;
};

#define uInvViewProj pInvViewProj
#define uCameraPos pCameraPosSkyIntensity.xyz
#define uSkyIntensity pCameraPosSkyIntensity.w
#define uSunDirection pSunDirectionVisibility.xyz
#define uSunVisibility pSunDirectionVisibility.w
#define uMoonDirection pMoonDirectionVisibility.xyz
#define uMoonVisibility pMoonDirectionVisibility.w
#define uMoonPhaseAngle pAtmosphereComposite.x
#define uSkyWetness pAtmosphereComposite.y
#define uWeatherWetness pAtmosphereComposite.z
#define uCloudCompositeStrength pAtmosphereComposite.w
#define uReflectionCompositeStrength pReflectionWater.x
#define uWaterAbsorption pReflectionWater.yzw
#define uBlindness pStatus.x
#define uDarknessFactor pStatus.y
#define uVoxelGiOrigin pVoxelOriginSize.xyz
#define uVoxelGiVoxelSize pVoxelOriginSize.w
#define uVoxelGiResolution pVoxel0.x
#define uVoxelGiMipCount pVoxel0.y
#define uVoxelGiStrength pVoxel0.z
#define uVoxelGiNormalBias pVoxel0.w
#define uVoxelGiSampleDistance pVoxel1.x
#define uVoxelGiTraceDistance pVoxel1.y
#define uVoxelGiConeAperture pVoxel1.z
#define uVoxelGiOccupancyScale pVoxel1.w
#define uVoxelGiOcclusionStrength pVoxel2.x
#define uVoxelGiReceiverShadowBoost pVoxel2.y
#define uSsgiEnabled pFlags0.x
#define uVoxelGiEnabled pFlags0.y
#define uVoxelGiDebugEnabled pFlags0.z
#define uVoxelGiConeSteps pFlags0.w
#define uReflectionDebugMode pFlags1.x
#define uIsEyeInWater pFlags1.y

#include "lighting_environment.glsl"
#include "fogs.glsl"
#include "procedural_celestials.glsl"

vec3 tonemapDebugSafe(vec3 color) {
    return max(color, vec3(0.0));
}

vec3 reconstructWorldPosition(vec2 clipUv, float depth) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

#ifdef MECRAFT_SCENE_VOXEL_GI
vec3 voxelGiUv(vec3 worldPos) {
    return (worldPos - uVoxelGiOrigin) / max(uVoxelGiVoxelSize * uVoxelGiResolution, 0.0001);
}

float voxelGiInside(vec3 uv) {
    vec3 edge = step(vec3(0.0), uv) * step(uv, vec3(1.0));
    return edge.x * edge.y * edge.z;
}

vec4 sampleVoxelGiRadiance(vec3 worldPos, float lod) {
    vec3 uv = voxelGiUv(worldPos);
    return textureLod(uVoxelGiTex, clamp(uv, vec3(0.0), vec3(1.0)), lod) * voxelGiInside(uv);
}

float voxelGiConeCoverage(float occupancy, float coneDiameter) {
    float normalizedOccupancy = clamp(occupancy, 0.0, 1.0);
    float footprint = max(1.0, pow(coneDiameter / max(uVoxelGiVoxelSize, 0.0001), 2.0) * max(uVoxelGiOccupancyScale, 0.0));
    return 1.0 - pow(max(1.0 - normalizedOccupancy, 0.0001), footprint);
}

vec4 traceVoxelGiCone(vec3 start, vec3 direction, float weight) {
    int steps = clamp(uVoxelGiConeSteps, 1, 12);
    float traceDistance = max(uVoxelGiTraceDistance, uVoxelGiVoxelSize);
    float firstDistance = max(uVoxelGiSampleDistance, uVoxelGiVoxelSize);
    float stepLength = max(uVoxelGiVoxelSize, (traceDistance - firstDistance) / float(steps));
    vec3 radiance = vec3(0.0);
    float opacity = 0.0;

    for (int i = 0; i < 12; ++i) {
        if (i >= steps) {
            break;
        }
        float distanceAlongCone = firstDistance + (float(i) + 0.35) * stepLength;
        float coneDiameter = max(uVoxelGiVoxelSize, distanceAlongCone * max(uVoxelGiConeAperture, 0.05));
        float lod = clamp(log2(coneDiameter / max(uVoxelGiVoxelSize, 0.0001)), 0.0, max(uVoxelGiMipCount - 1.0, 0.0));
        vec4 sampleData = sampleVoxelGiRadiance(start + direction * distanceAlongCone, lod);
        float coverage = voxelGiConeCoverage(sampleData.a, coneDiameter);
        float stepRatio = max(stepLength / max(uVoxelGiVoxelSize, 0.0001), 1.0);
        float correctedAlpha = 1.0 - pow(max(1.0 - coverage, 0.0001), stepRatio);
        float surfaceVisibility = mix(1.0, 0.58, coverage);
        vec3 surfaceRadiance = sampleData.a > 1e-4
            ? sampleData.rgb / max(sampleData.a, 1e-4)
            : vec3(0.0);
        float alphaStep = clamp(correctedAlpha * max(uVoxelGiOcclusionStrength, 0.0), 0.0, 1.0);
        float frontToBack = (1.0 - opacity) * alphaStep;
        radiance += surfaceRadiance * surfaceVisibility * frontToBack;
        opacity += frontToBack;
        if (opacity > 0.965) {
            break;
        }
    }

    return vec4(radiance * weight, weight);
}

vec3 sampleVoxelGiContribution(vec3 worldPos, vec3 normal, vec3 albedo, float vertexAo, vec3 sceneColor) {
    vec3 n = normalize(normal);
    vec3 up = abs(n.y) < 0.92 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = normalize(cross(n, tangent));

    vec3 start = worldPos + n * max(uVoxelGiNormalBias, 0.0);
    vec3 radiance = vec3(0.0);
    float weightSum = 0.0;

    vec3 selfSample = sampleVoxelGiRadiance(worldPos - n * (uVoxelGiVoxelSize * 0.35), 0.0).rgb;
    radiance += selfSample * 0.25;
    weightSum += 0.35;

    vec3 directions[5] = vec3[5](
        n,
        normalize(n + tangent * 0.75),
        normalize(n - tangent * 0.75),
        normalize(n + bitangent * 0.75),
        normalize(n - bitangent * 0.75)
    );

    float directionWeights[5] = float[5](1.0, 0.58, 0.58, 0.58, 0.58);
    for (int i = 0; i < 5; ++i) {
        vec4 coneTrace = traceVoxelGiCone(start, directions[i], directionWeights[i]);
        radiance += coneTrace.rgb;
        weightSum += coneTrace.a;
    }

    vec3 irradiance = radiance / max(weightSum, 0.0001);
    vec3 receiverTint = mix(vec3(0.50), albedo, 0.70);
    float ao = clamp(vertexAo, 0.0, 1.0);
    float aoWeight = mix(0.58, 1.0, ao);
    float sunFacing = max(dot(n, normalize(uSunDirection)), 0.0);
    float daylight = clamp(uSunVisibility * uSkyIntensity, 0.0, 1.0);
    float backlitWeight = (1.0 - smoothstep(0.08, 0.72, sunFacing)) * daylight;
    float lowLightWeight = 1.0 - smoothstep(0.035, 0.55, luminance(sceneColor));
    float receiverWeight = 1.0 + max(uVoxelGiReceiverShadowBoost, 0.0) *
                           (backlitWeight * 0.75 + lowLightWeight * 0.45 + (1.0 - ao) * 0.35);
    return irradiance * receiverTint * aoWeight * receiverWeight * max(uVoxelGiStrength, 0.0);
}
#endif

void applyUnderwaterFog(inout vec3 color, float fogDistance, LightingEnvironment env) {
    // DerivativeMain/lib/Water/WaterFog.glsl UnderwaterFog(), adapted for
    // Mecraft's fullscreen scene composite where the camera is already underwater.
    const float waterSkylight = 0.75;
    float fogDensity = (0.1 + 0.05 * uSkyWetness * waterSkylight) * max(fogDistance, 0.0);

    vec3 skyFogBase = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    vec3 fogColor = mix(skyFogBase * 0.4,
                        vec3(luminance(skyFogBase) * 0.1),
                        0.8 * uSkyWetness * waterSkylight) * (1.0 / 3.14159265359);

    vec3 absorption = uWaterAbsorption * 8.0 + 0.03;
    vec3 transmittance = exp(-absorption * max(fogDensity, 2.0) + 0.4);

    color *= transmittance;
    color += fogColor * clamp(waterSkylight + 0.2, 0.0, 1.0) * (1.0 - transmittance);
}

void main() {
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    vec4 scene = texture(uSceneLightingTex, textureUv);
    float depth = texture(uDepthTex, textureUv).r;
    vec3 color = scene.rgb;
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    // DerivativeMain deferred5.fsh:168-176: clouds composited ONLY on sky pixels.
    // Geometry pixels never get cloud blending (clouds are always behind geometry).
    vec4 cloud = texture(uCloudTex, textureUv);
    if (depth >= 0.9999) {
        vec3 skyPos = reconstructWorldPosition(vClipUv, 1.0);
        vec3 skyDir = normalize(skyPos - uCameraPos);
        // DerivativeMain composites live cloud data over raw colortex5 sky.
        // Using the cloudy atlas half here double-applies sky/cloud attenuation.
        vec3 sky = sampleSkyRadiance(uSkyCaptureTex, skyDir);
        float solarLobeMask = proceduralSunAngularMask(skyDir, 0.018, 0.180);
        float solarCoreMask = proceduralSunAngularMask(skyDir, 0.000, 0.070);
        sky *= (1.0 - solarLobeMask * 0.94) * (1.0 - solarCoreMask * 0.92);
        sky *= min(1.0, mix(18.0, 1.5, solarCoreMask) / max(luminance(sky), 1e-5));
        sky += renderProceduralMoonDisk(skyDir);
        sky += renderProceduralSunDisk(skyDir);
        float cloudSolarMask = proceduralSunAngularMask(skyDir, 0.018, 0.155);
        float cloudSolarCoreMask = proceduralSunAngularMask(skyDir, 0.000, 0.085);
        cloud.rgb *= (1.0 - cloudSolarMask * 0.985) * (1.0 - cloudSolarCoreMask * 0.98);
        cloud.rgb *= min(1.0, mix(10.0, 0.25, cloudSolarCoreMask) / max(luminance(cloud.rgb), 1e-5));
        cloud.a = mix(cloud.a, 1.0, cloudSolarMask * 0.96);
        // Premultiplied alpha: sceneData = sceneData * cloudData.a + cloudData.rgb
        // Strength blends between unmodified sky and full premultiplied result,
        // so reducing strength doesn't break sky transmittance energy.
        vec3 premul = sky * cloud.a + cloud.rgb;
        color = mix(sky, premul, clamp(uCloudCompositeStrength, 0.0, 1.0));
    }

    // DerivativeMain composite1 convention:
    // - translucent surfaces use reflection.a as scene pass-through
    // - opaque reflective surfaces add reflection.rgb directly
    vec4 reflection = texture(uReflectionTex, textureUv);

    // Reflection debug: bypass composite, output raw reflection data.
    if (uReflectionDebugMode > 0) {
        if (uReflectionDebugMode == 6) {
            // Composite delta: actual contribution of reflection to final scene.
            float compositeStr = clamp(uReflectionCompositeStrength, 0.0, 1.0);
            SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, textureUv));
            TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
            vec3 delta = transMask.isTranslucent
                ? compositeStr * (reflection.rgb - color * (1.0 - reflection.a))
                : compositeStr * reflection.rgb;
            FragColor = vec4(abs(delta), 1.0);
        } else if (uReflectionDebugMode == 26) {
            float sceneLum = max(luminance(color), 1e-5);
            float reflLum = luminance(reflection.rgb) * clamp(uReflectionCompositeStrength, 0.0, 1.0);
            FragColor = vec4(vec3(clamp(reflLum / sceneLum, 0.0, 1.0)), 1.0);
        } else if (uReflectionDebugMode == 27) {
            FragColor = vec4(vec3(luminance(color)), 1.0);
        } else if (uReflectionDebugMode == 28) {
            float reflLum = luminance(reflection.rgb) * clamp(uReflectionCompositeStrength, 0.0, 1.0);
            FragColor = vec4(vec3(reflLum * 64.0), 1.0);
        } else {
            FragColor = vec4(reflection.rgb, 1.0);
        }
        return;
    }

    SurfaceMaterial material = unpackGBufferMaterial(texture(uMaterialTex, textureUv));
    SurfaceMaterialAux aux = unpackGBufferMaterialAux(texture(uMaterialAuxTex, textureUv));
    TranslucentMask transMask = decodeTranslucentMask(aux.materialKind);
#ifdef MECRAFT_SCENE_VOXEL_GI
    if (uVoxelGiEnabled != 0 && depth < 0.9999 && !transMask.isTranslucent) {
        vec3 worldPos = reconstructWorldPosition(vClipUv, depth);
        vec4 normalAo = texture(uNormalAoTex, textureUv);
        vec3 normal = normalize(normalAo.rgb * 2.0 - 1.0);
        vec3 albedo = texture(uAlbedoTex, textureUv).rgb;
        vec3 voxelGi = sampleVoxelGiContribution(worldPos, normal, albedo, normalAo.a, color);
        if (uVoxelGiDebugEnabled != 0) {
            FragColor = vec4(max(voxelGi, vec3(0.0)) * 6.0, 1.0);
            return;
        }
        color += max(voxelGi, vec3(0.0));
    }
#endif
    if (uSsgiEnabled != 0 && depth < 0.9999 && !transMask.isTranslucent) {
        vec4 ssgi = texture(uSsgiTex, textureUv);
        float confidence = smoothstep(0.06, 0.45, ssgi.a);
        float lowLightWeight = 1.0 - smoothstep(0.08, 0.85, luminance(color));
        float surfaceWeight = mix(0.28, 1.0, lowLightWeight);
        color += max(ssgi.rgb, vec3(0.0)) * confidence * surfaceWeight;
    }

    float compositeStrength = clamp(uReflectionCompositeStrength, 0.0, 1.0);
    if (transMask.isTranslucent) {
        // DerivativeMain composite1.fsh: sceneData = sceneData * reflectionData.a + reflectionData.rgb
        float sceneWeight = mix(1.0, reflection.a, compositeStrength);
        color = color * sceneWeight + reflection.rgb * compositeStrength;
    } else {
        // DerivativeMain opaque path: sceneData += reflectionData.rgb * mix(1, albedo, metal).
        // Mecraft has already applied the metal tint in reflection_probe.fs.
        color += reflection.rgb * compositeStrength;
    }

    // Translucent tinting (DerivativeMain composite1 equivalent).
    // For stained glass: absorb light through colored medium.
    // For ice: tint by squared albedo (approximates volumetric absorption).
    if (scene.a > 0.5 && depth < 0.9999) {
        vec3 transAlbedo = texture(uAlbedoTex, textureUv).rgb;
        if (transMask.isStainedGlass) {
            // Beer-Lambert absorption: darker albedo = more absorption.
            vec3 absorbColor = mix(vec3(1.0), transAlbedo, 0.72);
            color *= absorbColor;
        } else if (transMask.isIce) {
            color *= transAlbedo * transAlbedo;
        }
    }

    // DerivativeMain composite1.fsh: underwater fog + CommonFog (final fog pass).
    // Water uses detailed Beer-Lambert + sky scatter; lava/powder_snow use CommonFog only.
    // Blindness/darkness CommonFog applies regardless of eye medium.
    {
        vec3 fogWorldPos = reconstructWorldPosition(
            vClipUv, depth < 0.9999 ? depth : 0.9999);
        float fogDistance = length(fogWorldPos - uCameraPos);
        if (uIsEyeInWater == 1) {
            applyUnderwaterFog(color, fogDistance, env);
        }
        CommonFog(color, fogDistance, uIsEyeInWater,
                  uBlindness, uDarknessFactor, uWeatherWetness,
                  env.skyIlluminance, env.directIlluminance);
    }

    FragColor = vec4(tonemapDebugSafe(color), scene.a);
}
