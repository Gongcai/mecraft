#version 450 core
#include "gbuffer_contract.glsl"
#include "derivative_shadow.glsl"
#include "weather_surface.glsl"

layout (location = 0) out vec4 GAlbedoMaterial;
layout (location = 1) out vec4 GNormalAo;
layout (location = 2) out vec4 GVoxelLight;
layout (location = 3) out vec4 GMaterial;
layout (location = 4) out vec4 GMaterialAux;

in vec2 vUV;
in float vSunlight;
in float vBlockLight;
in float vAO;
in float vNormal;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
flat in float vTintKind;
flat in float vMaterialKind;
in vec2 vTintUV;
in vec3 vWorldPos;

#ifdef RHI_TERRAIN_MDI
layout(binding = 0) uniform sampler2DArray texArray;
layout(binding = 3) uniform sampler2D uGrassColormap;
layout(binding = 4) uniform sampler2D uFoliageColormap;
layout(binding = 9) uniform sampler2D uNoiseTex;
layout(binding = 10) uniform sampler2D uRippleNormalTex;
#ifdef RHI_TERRAIN_NORMAL_MAPS
layout(binding = 11) uniform sampler2DArray uBlockNormalTex;
#endif
#ifdef RHI_TERRAIN_SPECULAR_MAPS
layout(binding = 12) uniform sampler2DArray uBlockSpecularTex;
#endif
#include "terrain_gbuffer_params.glsl"

#define uCameraPos rhiTerrainCameraAnimation.xyz
#define uAnimationTime rhiTerrainCameraAnimation.w
#define uShaderTime rhiTerrainSurfaceParams.x
#define uSurfaceWetness rhiTerrainSurfaceParams.y
#define uBlockParallaxDepth rhiTerrainSurfaceParams.z
#define uForceBaseLod rhiTerrainMaterialFlags.x
#define uHasBlockNormalMaps rhiTerrainMaterialFlags.y
#define uHasBlockSpecularMaps rhiTerrainMaterialFlags.z
#define uBlockParallaxEnabled rhiTerrainMaterialFlags.w
#define uRainWetSurfacesEnabled rhiTerrainWeatherFlags.x
#define uRainSurfaceRipplesEnabled rhiTerrainWeatherFlags.y
#else
uniform sampler2DArray texArray;
uniform sampler2DArray uBlockNormalTex;
uniform sampler2DArray uBlockSpecularTex;
uniform sampler2D uNoiseTex;
uniform sampler2D uRippleNormalTex;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform int uHasBlockNormalMaps;
uniform int uHasBlockSpecularMaps;
uniform int uBlockParallaxEnabled;
uniform float uBlockParallaxDepth;
uniform float uAnimationTime;
uniform float uShaderTime;
uniform float uSurfaceWetness;
uniform int uRainWetSurfacesEnabled;
uniform int uRainSurfaceRipplesEnabled;
uniform vec3 uCameraPos;
#endif

const int kBlockParallaxMaxSteps = 28;
const float kBlockParallaxMinViewZ = 0.10;

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
        vec3(0.25, 0.07, 0.34), vec3(0.02, 0.30, 0.30), vec3(0.32, 0.16, 0.08), vec3(0.36, 0.36, 0.36)
    );
    const vec3 highPalette[16] = vec3[16](
        vec3(1.00, 0.10, 0.02), vec3(0.08, 0.35, 1.00), vec3(0.08, 0.95, 0.18), vec3(1.00, 0.86, 0.08),
        vec3(0.78, 0.18, 1.00), vec3(0.05, 0.92, 1.00), vec3(1.00, 0.38, 0.05), vec3(0.82, 0.82, 0.82),
        vec3(1.00, 0.18, 0.42), vec3(0.35, 0.62, 1.00), vec3(0.18, 1.00, 0.62), vec3(1.00, 0.74, 0.20),
        vec3(0.82, 0.40, 1.00), vec3(0.25, 1.00, 0.92), vec3(1.00, 0.56, 0.25), vec3(1.00, 1.00, 1.00)
    );
    return mix(lowPalette[tint], highPalette[tint], power);
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

mat3 tangentFrame(vec3 normal, vec3 position, vec2 uv) {
    vec3 dpdx = dFdx(position);
    vec3 dpdy = dFdy(position);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);

    float invDet = 1.0 / (duvdx.x * duvdy.y - duvdx.y * duvdy.x);
    vec3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) * invDet);
    vec3 bitangent = normalize((dpdy * duvdx.x - dpdx * duvdy.x) * invDet);
    return mat3(tangent, bitangent, normal);
}

vec4 sampleBlockMap(sampler2DArray mapTex,
                    vec2 uv,
                    float layer,
                    bool forceBaseLod,
                    vec2 uvDx,
                    vec2 uvDy) {
    vec3 coord = vec3(uv, layer);
    return forceBaseLod
        ? textureLod(mapTex, coord, 0.0)
        : textureGrad(mapTex, coord, uvDx, uvDy);
}

#if !defined(RHI_TERRAIN_MDI) || defined(RHI_TERRAIN_NORMAL_MAPS)
float sampleLabPbrHeight(vec2 uv, float layer, bool forceBaseLod, vec2 uvDx, vec2 uvDy) {
    return sampleBlockMap(uBlockNormalTex, uv, layer, forceBaseLod, uvDx, uvDy).a;
}

vec2 composeBlockTileUv(vec2 baseUv, vec2 tileUv) {
    return floor(baseUv) + fract(tileUv);
}

vec2 applyBlockParallaxMap(vec3 geometricNormal,
                           vec3 position,
                           vec2 baseUv,
                           float layer,
                           bool forceBaseLod,
                           vec2 uvDx,
                           vec2 uvDy) {
    if (uBlockParallaxEnabled == 0 || uBlockParallaxDepth <= 0.0) {
        return baseUv;
    }

    mat3 frame = tangentFrame(geometricNormal, position, baseUv);
    vec3 viewDir = normalize(uCameraPos - position);
    vec3 tangentViewDir = transpose(frame) * viewDir;
    if (tangentViewDir.z <= 0.001) {
        return baseUv;
    }

    float viewZ = max(tangentViewDir.z, kBlockParallaxMinViewZ);
    float grazing = 1.0 - clamp(viewZ, 0.0, 1.0);
    int stepCount = int(mix(8.0, float(kBlockParallaxMaxSteps), grazing));
    float layerStep = 1.0 / float(stepCount);
    vec2 parallaxVector = (tangentViewDir.xy / viewZ) * uBlockParallaxDepth;
    vec2 uvStep = parallaxVector / float(stepCount);

    vec2 tileUv = fract(baseUv);
    vec2 currentUv = composeBlockTileUv(baseUv, tileUv);
    float currentLayerDepth = 0.0;
    float currentDepth = 1.0 - sampleLabPbrHeight(currentUv, layer, forceBaseLod, uvDx, uvDy);

    for (int i = 0; i < kBlockParallaxMaxSteps; ++i) {
        if (i >= stepCount || currentLayerDepth >= currentDepth) {
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
    float denom = afterDepth - beforeDepth;
    float weight = abs(denom) > 1e-5 ? clamp(afterDepth / denom, 0.0, 1.0) : 0.0;
    return mix(currentUv, previousUv, weight);
}
#endif

vec3 applyBlockNormalMap(vec3 geometricNormal, vec3 position, vec2 derivativeUv, vec4 normalTexel) {
    vec3 tangentNormal = normalize(normalTexel.xyz * 2.0 - 1.0);
    return normalize(tangentFrame(geometricNormal, position, derivativeUv) * tangentNormal);
}

bool hasAuthoredSpecularData(vec4 specularTexel) {
    return dot(specularTexel, vec4(1.0)) > (1.0 / 255.0);
}

void applyLabPbrSpecularMap(vec4 specularTexel,
                            int materialId,
                            float emissiveHint,
                            inout SurfaceMaterial material,
                            inout SurfaceMaterialAux aux) {
    float smoothness = clamp(specularTexel.r, 0.0, 1.0);
    material.roughness = sqr(1.0 - smoothness);
    material.f0 = clamp(specularTexel.g, 0.0, 1.0);
    material.emission = max(material.emission, derivativeEmissionHint(materialId, max(specularTexel.b, emissiveHint)));
    material.sss = max(material.sss, clamp(specularTexel.a, 0.0, 1.0));
    aux.metalness = max(aux.metalness, smoothstep(229.5 / 255.0, 230.5 / 255.0, specularTexel.g));
}

void main() {
    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
        sampledLayer += frame;
    }

    vec2 uvDx = dFdx(vUV);
    vec2 uvDy = dFdy(vUV);
    vec3 geometricNormal = decodeFaceNormal(vNormal);
    vec2 sampleUv = vUV;
#if !defined(RHI_TERRAIN_MDI) || defined(RHI_TERRAIN_NORMAL_MAPS)
    if (!isCrossVegetation && uHasBlockNormalMaps != 0) {
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
    int derivativeMaterialId = derivativeFragmentMaterialId(materialKindId(vMaterialKind));
    bool isEmissiveMaterial = isDerivativeEmissiveMaterialId(derivativeMaterialId) ||
                              derivativeMaterialId == MATERIAL_ORE ||
                              derivativeMaterialId == MATERIAL_NETHER_ORE;
    float emissiveLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float emissivePeak = max(max(albedo.r, albedo.g), albedo.b);
    float emissiveMask = smoothstep(0.34, 0.72, max(emissiveLuma, emissivePeak * 0.72));
    float emissiveHint = isEmissiveMaterial ? emissiveMask * clamp(vBlockLight * 1.25, 0.0, 1.0) : 0.0;
    SurfaceMaterial material = surfaceMaterialForKind(vMaterialKind, emissiveHint);
    SurfaceMaterialAux aux = surfaceMaterialAuxForKind(vMaterialKind);

#if !defined(RHI_TERRAIN_MDI) || defined(RHI_TERRAIN_NORMAL_MAPS)
    if (!isCrossVegetation && uHasBlockNormalMaps != 0) {
        vec4 normalTexel = sampleBlockMap(uBlockNormalTex, sampleUv, sampledLayer, forceBaseLod, uvDx, uvDy);
        normal = applyBlockNormalMap(normal, vWorldPos, vUV, normalTexel);
    }
#endif

#if !defined(RHI_TERRAIN_MDI) || defined(RHI_TERRAIN_SPECULAR_MAPS)
    if (uHasBlockSpecularMaps != 0) {
        vec4 specularTexel = sampleBlockMap(uBlockSpecularTex, sampleUv, sampledLayer, forceBaseLod, uvDx, uvDy);
        if (hasAuthoredSpecularData(specularTexel)) {
            applyLabPbrSpecularMap(specularTexel, derivativeMaterialId, emissiveHint, material, aux);
        }
    }
#endif

    bool canReceiveTerrainRain = !isCrossVegetation &&
                                 derivativeMaterialId != MATERIAL_WATER &&
                                 derivativeMaterialId != MATERIAL_ICE &&
                                 derivativeMaterialId != MATERIAL_STAINED_GLASS;
    if (canReceiveTerrainRain && uRainWetSurfacesEnabled != 0 && uSurfaceWetness > 1e-2) {
        float upwardFacing = remap(0.5, 0.9, normal.y);
        float rainWetness = ComputeRainSurfaceWetnessNoiseFromFacing(uNoiseTex, vWorldPos, uSurfaceWetness, vSunlight, upwardFacing, uShaderTime);
        float splashWetFact = ComputeRainSplashMaskFromNoise(rainWetness);
        float puddleFact = ComputeRainPuddleMaskFromNoise(rainWetness, aux.porosity);

        if (uRainSurfaceRipplesEnabled != 0 && max(splashWetFact, puddleFact) > 1e-4) {
            // DerivativeMain Terrain.frag drives visible rings with the narrow
            // splash mask, while its smooth puddle mask makes the reflective
            // interior energetic enough for those rings to read. Mecraft stores
            // the final world normal directly and filters reflections in a later
            // pass, so keep a stronger ripple floor on puddles to survive that
            // resolve without exceeding DerivativeMain's 0.5 normal blend cap.
            float rippleFact = max(splashWetFact, puddleFact * 0.75);
            vec2 rainNormal = SampleRainRippleNormal(uRippleNormalTex, vWorldPos, 1.0, uShaderTime, 0.60, 1.0);
            normal = normalize(mix(normal, vec3(rainNormal.x, 1.0, rainNormal.y), min(rippleFact * 0.65, 0.5)));
        } else if (splashWetFact > 1e-4) {
            normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), splashWetFact));
        }

        if (puddleFact > 1e-4) {
            aux.wetnessMask = max(aux.wetnessMask, puddleFact);
            float smoothness = 1.0 - sqrt(clamp(material.roughness, 0.0, 1.0));
            smoothness = mix(smoothness, 1.0, puddleFact);
            material.roughness = sqr(1.0 - smoothness);
            material.f0 = max(material.f0, 0.04 * puddleFact);
        }

        float wetAlbedoFact = ComputeRainWetAlbedoMaskFromNoise(rainWetness);
        if (wetAlbedoFact > 1e-4) {
            albedo = ApplyWetAlbedo(albedo, aux.porosity, wetAlbedoFact);
        }
    }

    GAlbedoMaterial = vec4(albedo, emissiveHint);
    GNormalAo = vec4(normal * 0.5 + 0.5, ao);
    GVoxelLight = vec4(clamp(vSunlight, 0.0, 1.0), clamp(vBlockLight, 0.0, 1.0), 0.0, 1.0);
    GMaterial = packGBufferMaterial(material);
    GMaterialAux = packGBufferMaterialAux(aux);
}
