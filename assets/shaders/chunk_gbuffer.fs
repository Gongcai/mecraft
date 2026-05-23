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

uniform sampler2DArray texArray;
uniform sampler2D uNoiseTex;
uniform sampler2D uRippleNormalTex;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform float uAnimationTime;
uniform float uShaderTime;
uniform float uSurfaceWetness;
uniform int uRainWetSurfacesEnabled;
uniform int uRainSurfaceRipplesEnabled;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
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
    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
        sampledLayer += frame;
    }

    vec4 texColor = forceBaseLod
        ? textureLod(texArray, vec3(vUV, sampledLayer), 0.0)
        : texture(texArray, vec3(vUV, sampledLayer));
    if (texColor.a < 0.1) {
        discard;
    }

    vec3 albedo = srgbToLinear(texColor.rgb);
    if (vTintKind > 0.5 && vTintKind < 1.5) {
        albedo *= srgbToLinear(texture(uGrassColormap, vTintUV).rgb);
    } else if (vTintKind > 1.5 && vTintKind < 2.5) {
        albedo *= srgbToLinear(texture(uFoliageColormap, vTintUV).rgb);
    }

    vec3 normal = decodeFaceNormal(vNormal);
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

    bool canReceiveTerrainRain = !isCrossVegetation &&
                                 derivativeMaterialId != MATERIAL_WATER &&
                                 derivativeMaterialId != MATERIAL_ICE &&
                                 derivativeMaterialId != MATERIAL_STAINED_GLASS;
    if (canReceiveTerrainRain && uRainWetSurfacesEnabled != 0 && uSurfaceWetness > 1e-2) {
        float upwardFacing = remap(0.5, 0.9, normal.y);
        float rainWetness = ComputeRainSurfaceWetnessNoiseFromFacing(uNoiseTex, vWorldPos, uSurfaceWetness, vSunlight, upwardFacing, uShaderTime);
        float splashWetFact = ComputeRainSplashMaskFromNoise(rainWetness);

        if (uRainSurfaceRipplesEnabled != 0 && splashWetFact > 1e-4) {
            vec2 rainNormal = SampleRainRippleNormal(uRippleNormalTex, vWorldPos, 1.0, uShaderTime, 0.60, 1.0);
            normal = normalize(mix(normal, vec3(rainNormal.x, 1.0, rainNormal.y), splashWetFact * 0.5));
        } else if (splashWetFact > 1e-4) {
            normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), splashWetFact));
        }

        float wetFact = ComputeRainPuddleMaskFromNoise(rainWetness, aux.porosity);
        if (wetFact > 1e-4) {
            aux.wetnessMask = max(aux.wetnessMask, wetFact);
            float smoothness = 1.0 - sqrt(clamp(material.roughness, 0.0, 1.0));
            smoothness = mix(smoothness, 1.0, wetFact);
            material.roughness = sqr(1.0 - smoothness);
            material.f0 = max(material.f0, 0.04 * wetFact);
            albedo = ApplyWetAlbedo(albedo, aux.porosity, wetFact);
        }
    }

    GAlbedoMaterial = vec4(albedo, emissiveHint);
    GNormalAo = vec4(normal * 0.5 + 0.5, ao);
    GVoxelLight = vec4(clamp(vSunlight, 0.0, 1.0), clamp(vBlockLight, 0.0, 1.0), 0.0, 1.0);
    GMaterial = packGBufferMaterial(material);
    GMaterialAux = packGBufferMaterialAux(aux);
}
