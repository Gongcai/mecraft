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
uniform sampler2DArray uBlockNormalArray;
uniform sampler2DArray uBlockSpecularArray;
uniform sampler2D uNoiseTex;
uniform sampler2D uRippleNormalTex;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform int uBlockNormalMapsEnabled;
uniform int uBlockSpecularMapsEnabled;
uniform int uBlockParallaxEnabled;
uniform float uBlockParallaxDepth;
uniform vec3 uCameraPos;
uniform float uAnimationTime;
uniform float uShaderTime;
uniform float uSurfaceWetness;
uniform int uRainWetSurfacesEnabled;
uniform int uRainSurfaceRipplesEnabled;

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

vec2 tileLocalUv(vec2 uv) {
    return uv - floor(uv);
}

vec2 clampTileUv(vec2 uv, sampler2DArray arraySampler) {
    vec2 texel = 0.5 / vec2(textureSize(arraySampler, 0).xy);
    return clamp(uv, texel, vec2(1.0) - texel);
}

vec4 sampleBlockArray(sampler2DArray arraySampler, vec2 uv, float layer, bool forceBaseLod) {
    return forceBaseLod
        ? textureLod(arraySampler, vec3(uv, layer), 0.0)
        : textureGrad(arraySampler, vec3(uv, layer), dFdx(vUV), dFdy(vUV));
}

vec3 orthogonalTangent(vec3 normal) {
    vec3 referenceAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(referenceAxis, normal));
}

mat3 buildSurfaceTbn(vec3 worldPos, vec2 uv, vec3 geometricNormal) {
    vec3 dpdx = dFdx(worldPos);
    vec3 dpdy = dFdy(worldPos);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);

    vec3 tangent = dpdx * duvdy.y - dpdy * duvdx.y;
    vec3 bitangent = -dpdx * duvdy.x + dpdy * duvdx.x;

    if (dot(tangent, tangent) < 1e-8 || dot(bitangent, bitangent) < 1e-8) {
        tangent = orthogonalTangent(geometricNormal);
        bitangent = normalize(cross(geometricNormal, tangent));
        return mat3(tangent, bitangent, geometricNormal);
    }

    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    bitangent = normalize(bitangent - geometricNormal * dot(geometricNormal, bitangent));
    if (dot(cross(tangent, bitangent), geometricNormal) < 0.0) {
        bitangent = -bitangent;
    }
    return mat3(tangent, bitangent, geometricNormal);
}

float sampleBlockHeight(vec2 uv, float layer) {
    return textureLod(uBlockNormalArray, vec3(uv, layer), 0.0).a;
}

vec2 applyBlockParallax(vec2 baseUv, vec2 surfaceUv, float layer, vec3 geometricNormal) {
    float initialHeight = sampleBlockHeight(baseUv, layer);
    if (initialHeight <= (1.0 / 255.0) || uBlockParallaxDepth <= 1e-5) {
        return baseUv;
    }

    mat3 tbn = buildSurfaceTbn(vWorldPos, surfaceUv, geometricNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 tangentViewDir = transpose(tbn) * viewDir;
    vec2 parallaxDir = tangentViewDir.xy / max(abs(tangentViewDir.z), 0.22);
    float parallaxDirLength = length(parallaxDir);
    if (parallaxDirLength > 3.0) {
        parallaxDir *= 3.0 / parallaxDirLength;
    }

    const int parallaxSteps = 12;
    const float layerDepth = 1.0 / float(parallaxSteps);
    vec2 stepUv = parallaxDir * (uBlockParallaxDepth / float(parallaxSteps));

    vec2 currentUv = baseUv;
    vec2 previousUv = currentUv;
    float currentDepth = 0.0;
    float currentHeight = initialHeight;
    float previousHeight = currentHeight;

    for (int stepIndex = 0; stepIndex < parallaxSteps && currentDepth < currentHeight; ++stepIndex) {
        previousUv = currentUv;
        previousHeight = currentHeight;
        currentUv = clampTileUv(currentUv - stepUv, uBlockNormalArray);
        currentDepth += layerDepth;
        currentHeight = sampleBlockHeight(currentUv, layer);
    }

    float previousDepth = max(currentDepth - layerDepth, 0.0);
    float beforeDelta = previousHeight - previousDepth;
    float afterDelta = currentHeight - currentDepth;
    float blendDenominator = beforeDelta - afterDelta;
    if (abs(blendDenominator) <= 1e-5) {
        return currentUv;
    }
    float blend = clamp(beforeDelta / blendDenominator, 0.0, 1.0);
    return clampTileUv(mix(previousUv, currentUv, blend), uBlockNormalArray);
}

vec3 applyBlockNormalMap(vec3 geometricNormal, vec2 materialUv, vec2 surfaceUv, vec4 normalTex) {
    vec3 tangentNormal = normalize(normalTex.rgb * 2.0 - 1.0);
    mat3 tbn = buildSurfaceTbn(vWorldPos, surfaceUv, geometricNormal);
    return normalize(tbn * tangentNormal);
}

void applyLabPbrSpecular(vec4 specTex,
                         int materialId,
                         inout SurfaceMaterial material,
                         inout SurfaceMaterialAux aux) {
    float smoothness = clamp(specTex.r, 0.0, 1.0);
    material.roughness = sqr(1.0 - smoothness);

    if (specTex.g > (229.5 / 255.0)) {
        aux.metalness = 1.0;
        material.f0 = 0.91;
    } else {
        aux.metalness = 0.0;
        material.f0 = clamp(specTex.g, 0.0, 1.0);
    }

    if (isDerivativeSssMaterialId(materialId)) {
        material.sss = max(material.sss, clamp(specTex.b, 0.0, 1.0));
    } else {
        aux.porosity = clamp(specTex.b, 0.0, 1.0);
    }

    float emissiveness = specTex.a >= 1.0 ? 0.0 : clamp(specTex.a, 0.0, 1.0);
    material.emission = max(material.emission, emissiveness);
}

void main() {
    bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
    bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
    float sampledLayer = vLayer;
    if (vAnimated > 0.5 && vAnimationFrameCount > 1.0 && vAnimationFps > 0.0) {
        float frame = mod(floor(uAnimationTime * vAnimationFps), vAnimationFrameCount);
        sampledLayer += frame;
    }

    vec3 normal = decodeFaceNormal(vNormal);
    vec2 materialUv = tileLocalUv(vUV);
    bool materialNormalMapsEnabled = uBlockNormalMapsEnabled != 0 && !isCrossVegetation;
    if (uBlockParallaxEnabled != 0 && materialNormalMapsEnabled && !forceBaseLod) {
        materialUv = applyBlockParallax(materialUv, vUV, sampledLayer, normal);
    }

    vec4 texColor = sampleBlockArray(texArray, materialUv, sampledLayer, forceBaseLod);
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

    if (uBlockSpecularMapsEnabled != 0) {
        vec4 specTex = sampleBlockArray(uBlockSpecularArray, materialUv, sampledLayer, forceBaseLod);
        applyLabPbrSpecular(specTex, derivativeMaterialId, material, aux);
    }

    if (materialNormalMapsEnabled) {
        vec4 normalTex = sampleBlockArray(uBlockNormalArray, materialUv, sampledLayer, forceBaseLod);
        normal = applyBlockNormalMap(normal, materialUv, vUV, normalTex);
    }

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
