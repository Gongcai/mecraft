#version 450 core

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 saturate(vec3 value) {
    return clamp(value, vec3(0.0), vec3(1.0));
}

float remap(float lowValue, float highValue, float value) {
    return saturate((value - lowValue) / max(highValue - lowValue, 1.0e-5));
}

vec3 LinearToSRGB(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 SRGBtoLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

#include "gbuffer_contract.glsl"
#include "rhi_screen_coordinates.glsl"
#include "weather_surface.glsl"
#include "clustered_light_evaluation.glsl"
#include "terrain_probe_capture_params.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vSunlight;
layout(location = 2) in float vBlockLight;
layout(location = 3) in float vAO;
layout(location = 4) in float vNormal;
layout(location = 5) in float vLayer;
layout(location = 6) in float vAnimationFrameCount;
layout(location = 7) in float vAnimationFps;
layout(location = 8) in float vAnimated;
layout(location = 9) flat in float vTintKind;
layout(location = 10) flat in float vMaterialKind;
layout(location = 11) in vec2 vTintUV;
layout(location = 12) in vec3 vWorldPos;

layout(location = 0) out vec4 outRadiance;

layout(set = 1, binding = 0) uniform sampler2DArray texArray;
layout(set = 1, binding = 3) uniform sampler2D uGrassColormap;
layout(set = 1, binding = 4) uniform sampler2D uFoliageColormap;
layout(set = 1, binding = 9) uniform sampler2D uNoiseTex;
layout(set = 1, binding = 10) uniform sampler2D uRippleNormalTex;
#ifdef RHI_TERRAIN_NORMAL_MAPS
layout(set = 1, binding = 11) uniform sampler2DArray uBlockNormalTex;
#endif
#ifdef RHI_TERRAIN_SPECULAR_MAPS
layout(set = 1, binding = 12) uniform sampler2DArray uBlockSpecularTex;
#endif
layout(set = 1, binding = 14) uniform sampler2D uProbeOpaqueColorTex;
layout(set = 1, binding = 15) uniform sampler2D uProbeOpaqueDepthTex;
layout(set = 2, binding = 1, std430) readonly buffer TerrainProbeCaptureLightBuffer {
    GpuLight uProbeLights[];
};

#define uAnimationTime uProbeMaterialTiming.x
#define uShaderTime uProbeMaterialTiming.y
#define uSurfaceWetness uProbeMaterialTiming.z
#define uBlockParallaxDepth uProbeMaterialTiming.w
#define uForceBaseLod uProbeMaterialFlags.x
#define uHasBlockNormalMaps uProbeMaterialFlags.y
#define uHasBlockSpecularMaps uProbeMaterialFlags.z
#define uBlockParallaxEnabled uProbeMaterialFlags.w
#define uRainWetSurfacesEnabled uProbeWeatherFlags.x
#define uRainSurfaceRipplesEnabled uProbeWeatherFlags.y
#define uWaterAbsorption uProbeWaterAbsorptionIor.xyz
#define uWaterIor uProbeWaterAbsorptionIor.w
#define uWaterWaveHeight uProbeWaterWaveParams.x
#define uWaterWaveSpeed uProbeWaterWaveParams.y

const int kBlockParallaxMaxSteps = 28;
const float kBlockParallaxMinViewZ = 0.10;
const float kBlockParallaxFadeStart = 16.0;
const float kBlockParallaxFadeEnd = 48.0;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 redstoneTintSrgb(vec2 tintUV) {
    float power = clamp(floor(tintUV.x * 16.0), 0.0, 15.0) / 15.0;
    int tint = int(clamp(floor(tintUV.y * 16.0), 0.0, 15.0));
    const vec3 lowPalette[16] = vec3[16](
        vec3(0.30, 0.00, 0.00), vec3(0.00, 0.05, 0.30), vec3(0.00, 0.22, 0.03), vec3(0.22, 0.18, 0.00),
        vec3(0.18, 0.00, 0.28), vec3(0.00, 0.20, 0.24), vec3(0.28, 0.09, 0.00), vec3(0.22, 0.22, 0.22),
        vec3(0.35, 0.02, 0.12), vec3(0.10, 0.20, 0.36), vec3(0.04, 0.28, 0.17), vec3(0.32, 0.24, 0.04),
        vec3(0.25, 0.07, 0.34), vec3(0.02, 0.30, 0.30), vec3(0.32, 0.16, 0.08), vec3(0.36, 0.36, 0.36));
    const vec3 highPalette[16] = vec3[16](
        vec3(1.00, 0.10, 0.02), vec3(0.08, 0.35, 1.00), vec3(0.08, 0.95, 0.18), vec3(1.00, 0.86, 0.08),
        vec3(0.78, 0.18, 1.00), vec3(0.05, 0.92, 1.00), vec3(1.00, 0.38, 0.05), vec3(0.82, 0.82, 0.82),
        vec3(1.00, 0.18, 0.42), vec3(0.35, 0.62, 1.00), vec3(0.18, 1.00, 0.62), vec3(1.00, 0.74, 0.20),
        vec3(0.82, 0.40, 1.00), vec3(0.25, 1.00, 0.92), vec3(1.00, 0.56, 0.25), vec3(1.00, 1.00, 1.00));
    return mix(lowPalette[tint], highPalette[tint], power);
}

vec3 decodeFaceNormal(float face) {
    if (face > -2.5 && face < -0.5) {
        return normalize(vec3(0.0, 1.0, 0.0));
    }
    int index = int(round(face));
    if (index == 0) return vec3(0.0, 1.0, 0.0);
    if (index == 1) return vec3(0.0, -1.0, 0.0);
    if (index == 2) return vec3(0.0, 0.0, 1.0);
    if (index == 3) return vec3(0.0, 0.0, -1.0);
    if (index == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}

mat3 tangentFrame(vec3 normal, vec3 position, vec2 uv) {
    vec3 dpdx = dFdx(position);
    vec3 dpdy = dFdy(position);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float invDeterminant = 1.0 / (duvdx.x * duvdy.y - duvdx.y * duvdy.x);
    vec3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) * invDeterminant);
    vec3 bitangent = normalize((dpdy * duvdx.x - dpdx * duvdy.x) * invDeterminant);
    return mat3(tangent, bitangent, normal);
}

vec4 sampleBlockMap(sampler2DArray mapTexture, vec2 uv, float layer, bool forceBaseLod, vec2 uvDx, vec2 uvDy) {
    vec3 coordinate = vec3(uv, layer);
    return forceBaseLod ? textureLod(mapTexture, coordinate, 0.0) : textureGrad(mapTexture, coordinate, uvDx, uvDy);
}

#ifdef RHI_TERRAIN_NORMAL_MAPS
float sampleLabPbrHeight(vec2 uv, float layer, bool forceBaseLod, vec2 uvDx, vec2 uvDy) {
    return sampleBlockMap(uBlockNormalTex, uv, layer, forceBaseLod, uvDx, uvDy).a;
}

vec2 composeBlockTileUv(vec2 baseUv, vec2 tileUv) {
    return floor(baseUv) + fract(tileUv);
}

vec2 applyBlockParallaxMap(vec3 geometricNormal, vec3 position, vec2 baseUv, float layer, bool forceBaseLod,
                           vec2 uvDx, vec2 uvDy) {
    if (uBlockParallaxEnabled == 0 || uBlockParallaxDepth <= 0.0) {
        return baseUv;
    }
    vec3 cameraOffset = uProbePosition.xyz - position;
    float viewDistance = length(cameraOffset);
    float distanceFade = 1.0 - smoothstep(kBlockParallaxFadeStart, kBlockParallaxFadeEnd, viewDistance);
    if (distanceFade <= 0.0) {
        return baseUv;
    }
    mat3 frame = tangentFrame(geometricNormal, position, baseUv);
    vec3 tangentViewDirection = transpose(frame) * (cameraOffset / max(viewDistance, 1.0e-4));
    if (tangentViewDirection.z <= 0.001) {
        return baseUv;
    }
    float viewZ = max(tangentViewDirection.z, kBlockParallaxMinViewZ);
    float grazing = 1.0 - clamp(viewZ, 0.0, 1.0);
    int stepCount = int(mix(8.0, float(kBlockParallaxMaxSteps), grazing * distanceFade));
    float layerStep = 1.0 / float(stepCount);
    vec2 uvStep = (tangentViewDirection.xy / viewZ) * (uBlockParallaxDepth * distanceFade) / float(stepCount);
    vec2 tileUv = fract(baseUv);
    vec2 currentUv = composeBlockTileUv(baseUv, tileUv);
    float currentLayerDepth = 0.0;
    float currentDepth = 1.0 - sampleLabPbrHeight(currentUv, layer, forceBaseLod, uvDx, uvDy);
    for (int index = 0; index < kBlockParallaxMaxSteps; ++index) {
        if (index >= stepCount || currentLayerDepth >= currentDepth) {
            break;
        }
        tileUv -= uvStep;
        currentLayerDepth += layerStep;
        currentUv = composeBlockTileUv(baseUv, tileUv);
        currentDepth = 1.0 - sampleLabPbrHeight(currentUv, layer, forceBaseLod, uvDx, uvDy);
    }
    if (currentLayerDepth <= 0.0) {
        return baseUv;
    }
    vec2 previousUv = composeBlockTileUv(baseUv, tileUv + uvStep);
    float previousDepth = 1.0 - sampleLabPbrHeight(previousUv, layer, forceBaseLod, uvDx, uvDy);
    float afterDepth = currentDepth - currentLayerDepth;
    float beforeDepth = previousDepth - currentLayerDepth + layerStep;
    float denominator = afterDepth - beforeDepth;
    float weight = abs(denominator) > 1.0e-5 ? clamp(afterDepth / denominator, 0.0, 1.0) : 0.0;
    return mix(currentUv, previousUv, weight);
}
#endif

bool hasAuthoredSpecularData(vec4 specularTexel) {
    return !isLabPbrInternalNeutralSpecular(specularTexel);
}

mat3 waterTangentFrame(vec3 normal) {
    if (abs(normal.y) > 0.5) {
        return mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), normal);
    }
    if (abs(normal.x) > 0.5) {
        return mat3(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), normal);
    }
    return mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), normal);
}

float waterCurve(float value) {
    return value * value * (3.0 - 2.0 * value);
}

vec2 waterCurve(vec2 value) {
    return value * value * (3.0 - 2.0 * value);
}

float sampleSmoothWaterNoise(vec2 coordinate) {
    coordinate += 0.5;
    vec2 whole = floor(coordinate);
    coordinate = whole + waterCurve(coordinate - whole) - 0.5;
    return texture(uNoiseTex, coordinate / 256.0).r;
}

float waterHeight(vec2 position) {
    float waveTime = uShaderTime * 1.2 * uWaterWaveSpeed;
    position.y *= 0.8;
    float wave = 0.0;
    wave += sampleSmoothWaterNoise((position + vec2(0.0, position.x - waveTime)) * 0.8);
    wave += sampleSmoothWaterNoise((position - vec2(-waveTime, position.x)) * 1.6) * 0.5;
    wave += sampleSmoothWaterNoise((position + vec2(waveTime * 0.6, position.x - waveTime)) * 2.4) * 0.2;
    wave += sampleSmoothWaterNoise((position - vec2(waveTime * 0.6, position.x - waveTime)) * 3.6) * 0.1;
    return wave / (0.8 + dot(abs(dFdx(position) + dFdy(position)), vec2(80.0 / 512.0)));
}

vec3 waterWaveNormal(vec2 position) {
    float center = waterHeight(position);
    float left = waterHeight(position + vec2(0.04, 0.0));
    float up = waterHeight(position + vec2(0.0, 0.04));
    return normalize(vec3(vec2(center - left, center - up) * uWaterWaveHeight, 0.5));
}

vec2 waterParallaxPosition(vec3 worldPosition, vec3 tangentViewDirection) {
    vec3 stepSize = tangentViewDirection * vec3(vec2(0.1 * uWaterWaveHeight), 1.0);
    stepSize *= 0.02 / max(abs(stepSize.z), 0.001);
    vec3 samplePosition = vec3(worldPosition.xz - worldPosition.y, 1.0) + stepSize;
    float sampledHeight = waterHeight(samplePosition.xy);
    for (uint index = 0u; sampledHeight < samplePosition.z && index < 24u; ++index) {
        samplePosition += stepSize;
        sampledHeight = waterHeight(samplePosition.xy);
    }
    return samplePosition.xy;
}

vec3 reconstructProbeWorldPosition(vec2 clipUv, float depth) {
    vec4 clip = vec4(clipUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uProbeInverseViewProjection * clip;
    return world.xyz / max(world.w, 1.0e-5);
}

vec3 sampleProbeWaterTransmission(vec3 waterNormal, out float opticalDistance) {
    vec2 extent = vec2(textureSize(uProbeOpaqueColorTex, 0));
    vec2 screenUv = rhiNativeFragCoordToScreenUv(gl_FragCoord.xy, extent);
    vec2 clipUv = rhiScreenUvToClipUv(screenUv);
    vec2 textureUv = rhiScreenUvToTextureUv(screenUv);
    float opaqueDepth = texture(uProbeOpaqueDepthTex, textureUv).r;
    opticalDistance = 0.0;
    if (opaqueDepth <= gl_FragCoord.z || opaqueDepth >= 0.9999) {
        return texture(uProbeOpaqueColorTex, textureUv).rgb;
    }

    vec3 opaquePosition = reconstructProbeWorldPosition(clipUv, opaqueDepth);
    opticalDistance = clamp(distance(vWorldPos, opaquePosition), 0.0, 512.0);
    vec3 viewNormal = normalize(mat3(uProbeView) * waterNormal);
    vec3 viewUp = normalize(uProbeView[1].xyz);
    vec2 refractOffset = (viewUp.xy - viewNormal.xy) *
                         (clamp(opticalDistance, 0.0, 1.0) * 0.5 /
                          max(length(uProbePosition.xyz - vWorldPos), 1.0e-4));
    vec2 refractedClipUv = clamp(clipUv + refractOffset, vec2(0.0), vec2(1.0));
    vec2 refractedScreenUv = rhiScreenUvToClipUv(refractedClipUv);
    vec2 refractedTextureUv = rhiScreenUvToTextureUv(refractedScreenUv);
    float refractedDepth = texture(uProbeOpaqueDepthTex, refractedTextureUv).r;
    if (refractedDepth >= gl_FragCoord.z && refractedDepth < 0.9999) {
        opaquePosition = reconstructProbeWorldPosition(refractedClipUv, refractedDepth);
        opticalDistance = clamp(distance(vWorldPos, opaquePosition), 0.0, 512.0);
        textureUv = refractedTextureUv;
    }
    return texture(uProbeOpaqueColorTex, textureUv).rgb;
}

void main() {
    bool isCrossVegetation = vNormal > -2.5 && vNormal < -0.5;
    bool forceBaseLod = uForceBaseLod != 0 || isCrossVegetation;
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        sampledLayer += mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
    }

    vec2 uvDx = dFdx(vUV);
    vec2 uvDy = dFdy(vUV);
    vec3 geometricNormal = decodeFaceNormal(vNormal);
    int derivativeMaterialId = derivativeFragmentMaterialId(materialKindId(vMaterialKind));
    bool isWater = derivativeMaterialId == MATERIAL_WATER;
    vec2 sampleUv = vUV;
#ifdef RHI_TERRAIN_NORMAL_MAPS
    if (!isCrossVegetation && !isWater && uHasBlockNormalMaps != 0) {
        sampleUv = applyBlockParallaxMap(geometricNormal, vWorldPos, vUV, sampledLayer, forceBaseLod, uvDx, uvDy);
    }
#endif

    vec4 texColor = sampleBlockMap(texArray, sampleUv, sampledLayer, forceBaseLod, uvDx, uvDy);
    if (texColor.a < 0.1) {
        discard;
    }
    vec3 albedo = srgbToLinear(texColor.rgb);
    if (vTintKind > 0.5 && vTintKind < 1.5) {
        albedo *= srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        albedo *= srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
    } else if (vTintKind > 2.5 && vTintKind < 3.5) {
        albedo *= srgbToLinear(redstoneTintSrgb(vTintUV));
    }

    vec3 normal = geometricNormal;
    float ao = clamp(vAO / 3.0, 0.0, 1.0);
    bool isEmissiveMaterial = isDerivativeEmissiveMaterialId(derivativeMaterialId) ||
                              derivativeMaterialId == MATERIAL_ORE || derivativeMaterialId == MATERIAL_NETHER_ORE;
    float emissiveLuminance = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float emissivePeak = max(max(albedo.r, albedo.g), albedo.b);
    float emissiveMask = smoothstep(0.34, 0.72, max(emissiveLuminance, emissivePeak * 0.72));
    float emissiveHint = isEmissiveMaterial ? emissiveMask * clamp(vBlockLight * 1.25, 0.0, 1.0) : 0.0;
    SurfaceMaterial material = surfaceMaterialForKind(vMaterialKind, emissiveHint);
    SurfaceMaterialAux materialAux = surfaceMaterialAuxForKind(vMaterialKind);

#ifdef RHI_TERRAIN_NORMAL_MAPS
    if (!isCrossVegetation && !isWater && uHasBlockNormalMaps != 0) {
        LabPbrNormalSample normalSample = decodeLabPbrNormal(
            sampleBlockMap(uBlockNormalTex, sampleUv, sampledLayer, forceBaseLod, uvDx, uvDy));
        normal = normalize(tangentFrame(normal, vWorldPos, vUV) * normalSample.tangentNormal);
        ao *= normalSample.materialAo;
    }
#endif
#ifdef RHI_TERRAIN_SPECULAR_MAPS
    if (!isWater && uHasBlockSpecularMaps != 0) {
        vec4 specularTexel = sampleBlockMap(uBlockSpecularTex, sampleUv, sampledLayer, forceBaseLod, uvDx, uvDy);
        if (hasAuthoredSpecularData(specularTexel)) {
            LabPbrSpecularSample decoded = decodeLabPbrSpecular(specularTexel, albedo);
            material.perceptualRoughness = decoded.perceptualRoughness;
            material.encodedF0OrMetalId = clamp(specularTexel.g, 0.0, 1.0);
            if (decoded.emissionProvided) material.emission = decoded.emission;
            material.sss = decoded.subsurface;
            materialAux.porosity = decoded.porosity;
            materialAux.metalness = decoded.metalness;
        }
    }
#endif

    if (isWater) {
        vec3 viewDirection = normalize(uProbePosition.xyz - vWorldPos);
        mat3 frame = waterTangentFrame(geometricNormal);
        vec3 tangentViewDirection = normalize(transpose(frame) * viewDirection);
        vec2 parallaxPosition = waterParallaxPosition(vWorldPos, tangentViewDirection);
        vec3 tangentNormal = waterWaveNormal(parallaxPosition);
        if (uRainSurfaceRipplesEnabled != 0 && uSurfaceWetness > 0.01) {
            float skylightFactor = clamp(vSunlight * 10.0 - 9.0, 0.0, 1.0);
            vec2 rainNormal = SampleRainRippleNormal(
                uRippleNormalTex, vWorldPos, 1.0, uShaderTime, 0.60, 1.0);
            tangentNormal.xy += rainNormal * uSurfaceWetness * skylightFactor;
            tangentNormal = normalize(tangentNormal);
        }
        normal = normalize(frame * tangentNormal);
    }

    bool canReceiveRain = !isCrossVegetation && derivativeMaterialId != MATERIAL_WATER &&
                          derivativeMaterialId != MATERIAL_ICE && derivativeMaterialId != MATERIAL_STAINED_GLASS;
    if (canReceiveRain && uRainWetSurfacesEnabled != 0 && uSurfaceWetness > 1.0e-2) {
        float upwardFacing = remap(0.5, 0.9, normal.y);
        float rainWetness = ComputeRainSurfaceWetnessNoiseFromFacing(
            uNoiseTex, vWorldPos, uSurfaceWetness, vSunlight, upwardFacing, uShaderTime);
        float splashWetness = ComputeRainSplashMaskFromNoise(rainWetness);
        float puddleWetness = ComputeRainPuddleMaskFromNoise(rainWetness, materialAux.porosity);
        if (uRainSurfaceRipplesEnabled != 0 && max(splashWetness, puddleWetness) > 1.0e-4) {
            float rippleStrength = max(splashWetness, puddleWetness * 0.75);
            vec2 rainNormal = SampleRainRippleNormal(uRippleNormalTex, vWorldPos, 1.0, uShaderTime, 0.60, 1.0);
            normal = normalize(mix(normal, vec3(rainNormal.x, 1.0, rainNormal.y), min(rippleStrength * 0.65, 0.5)));
        }
        if (puddleWetness > 1.0e-4) {
            material.perceptualRoughness *= 1.0 - puddleWetness;
            material.encodedF0OrMetalId = max(material.encodedF0OrMetalId, 0.04 * puddleWetness);
        }
        float wetAlbedo = ComputeRainWetAlbedoMaskFromNoise(rainWetness);
        if (wetAlbedo > 1.0e-4) {
            albedo = ApplyWetAlbedo(albedo, materialAux.porosity, wetAlbedo);
        }
    }

    vec3 viewDirection = normalize(uProbePosition.xyz - vWorldPos);
    normal = faceforward(normalize(normal), -viewDirection, normalize(normal));
    vec3 f0 = decodeLabPbrF0(material.encodedF0OrMetalId, albedo);
    float f90 = pbrMaterialSpecularF90(material.specularF90, materialAux.metalness);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float alphaSquared = pbrPerceptualRoughnessToAlphaSquared(material.perceptualRoughness);
    float skyVisibility = clamp(vSunlight, 0.0, 1.0);

    vec3 lightDirection = normalize(uProbeSunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float lDotH = max(dot(lightDirection, halfDirection), 0.0);
    vec3 fresnel = pbrFresnelSchlick(lDotH, f0, f90);
    vec3 diffuseWeight = pbrDiffuseWeight(fresnel, materialAux.metalness);
    vec3 radiance = (diffuseWeight * pbrLambertDiffuse(albedo) * nDotL +
                     pbrEvaluateDirectSpecular(lDotH, nDotV, nDotL, nDotH, alphaSquared, f0, f90)) *
                    uProbeSunColor.rgb * skyVisibility;

    vec3 localDiffuse = vec3(0.0);
    vec3 localSpecular = vec3(0.0);
    vec3 probeRelativeSurface = vWorldPos - uProbePosition.xyz;
    for (uint lightIndex = 0u; lightIndex < uProbeLightCount.x; ++lightIndex) {
        GpuLightSurfaceContribution contribution = evaluateGpuLight(
            uProbeLights[lightIndex], probeRelativeSurface, normal, viewDirection, f0, f90, nDotV, alphaSquared);
        localDiffuse += contribution.diffuse;
        localSpecular += contribution.specular;
    }
    radiance += albedo * (1.0 - materialAux.metalness) * localDiffuse + localSpecular;
    radiance += albedo * uProbeAmbientColor.rgb * ao * skyVisibility;
    radiance += albedo * material.emission * 1.5;

    if (isWater) {
        float opticalDistance = 0.0;
        vec3 transmitted = sampleProbeWaterTransmission(normal, opticalDistance);
        float fresnel = pbrFresnelDielectricFromIor(max(dot(normal, viewDirection), 1.0e-6), uWaterIor);
        vec3 absorption = uWaterAbsorption * 8.0 + 0.03;
        vec3 transmittance = exp(-absorption * (0.16 * opticalDistance));
        vec3 inScattering = uProbeAmbientColor.rgb * skyVisibility * (1.0 - transmittance) * 0.4;
        vec3 waterReflection = max(radiance, uProbeAmbientColor.rgb * skyVisibility);
        vec3 waterRadiance = (transmitted * transmittance + inScattering) * (1.0 - fresnel) +
                             waterReflection * fresnel;
        outRadiance = vec4(max(waterRadiance, vec3(0.0)), 1.0);
        return;
    }

    bool opticalLayer = derivativeMaterialId == MATERIAL_STAINED_GLASS ||
                        derivativeMaterialId == MATERIAL_ICE;
    float outputAlpha = opticalLayer ? texColor.a : 1.0;
    outRadiance = opticalLayer ? vec4(radiance * outputAlpha, outputAlpha) : vec4(radiance, 1.0);
}
