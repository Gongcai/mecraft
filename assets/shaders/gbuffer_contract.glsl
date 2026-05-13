// Shared DerivativeMain-compatible G-buffer and material contract.
// Material ids mirror DerivativeMain/block.properties after subtracting 10000
// from OptiFine/Iris mc_Entity ids.

#ifndef MECRAFT_GBUFFER_CONTRACT_GLSL
#define MECRAFT_GBUFFER_CONTRACT_GLSL

const int MATERIAL_DEFAULT = 0;
const int MATERIAL_GRASS = 1;
const int MATERIAL_WHEAT = 2;
const int MATERIAL_FLOWER = 3;
const int MATERIAL_GRASS_UPPER = 4;
const int MATERIAL_GRASS_LOWER = 5;
const int MATERIAL_GRASS_LIKE = 6;
const int MATERIAL_LEAVES = 7;
const int MATERIAL_BANNER_SSS = 9;
const int MATERIAL_SNOW_ICE_SSS = 10;
const int MATERIAL_LAVA = 15;
const int MATERIAL_STAINED_GLASS = 16;
const int MATERIAL_WATER = 17;
const int MATERIAL_ICE = 18;
const int MATERIAL_END_PORTAL = 19;
const int MATERIAL_TOTAL_GLOWING = 20;
const int MATERIAL_TORCH_LIKE = 21;
const int MATERIAL_FIRE = 22;
const int MATERIAL_GLOWSTONE_LIKE = 23;
const int MATERIAL_SEA_LANTERN_LIKE = 24;
const int MATERIAL_REDSTONE = 25;
const int MATERIAL_SOUL_FIRE = 26;
const int MATERIAL_AMETHYST = 27;
const int MATERIAL_GLOWBERRY = 28;
const int MATERIAL_RAILS = 29;
const int MATERIAL_BEACON_CORE = 30;
const int MATERIAL_SCULK = 31;
const int MATERIAL_GLOW_LICHEN = 32;
const int MATERIAL_PARTIAL_GLOWING = 33;
const int MATERIAL_MIDDLE_GLOWING = 34;
const int MATERIAL_TEXTURED = 35;
const int MATERIAL_TEXTURED_EMISSIVE = 36;
const int MATERIAL_ORE = 57;
const int MATERIAL_NETHER_ORE = 58;
const float MATERIAL_ID_MAX = 63.0;

// DerivativeMain Material.inc:12 — EMISSION_CURVE shapes the PBR emissiveness channel.
// pow(x, 2.2) makes partial emission values dimmer, creating a steeper falloff.
const float EMISSIVE_CURVE = 2.2;

struct SurfaceMaterial {
    // DerivativeMain GetMaterialData(vec2): roughness=specTex.r,
    // f0=specTex.g when MC_SPECULAR_MAP is disabled.
    float roughness;
    float f0;
    // Packed SSS/emission hints from DerivativeMain's specTex.ba fallback.
    float emission;
    float sss;
};

struct SurfaceMaterialAux {
    float materialKind;
    float wetnessMask;
    float porosity;
    float metalness;
};

struct GBufferSurface {
    vec3 albedo;
    float emissiveHint;
    vec3 normal;
    float vertexAo;
    vec2 voxelLight;
    SurfaceMaterial material;
    SurfaceMaterialAux aux;
};

int materialKindId(float materialKind) {
    return int(round(materialKind));
}

int derivativeFragmentMaterialId(int materialId) {
    // DerivativeMain Terrain.vert keeps ids 1..5 only for plant animation, then
    // writes them as material 6 for the G-buffer/lighting contract.
    return (materialId > 0 && materialId < MATERIAL_GRASS_LIKE) ? MATERIAL_GRASS_LIKE : materialId;
}

bool isMaterialKind(float materialKind, int expectedKind) {
    return derivativeFragmentMaterialId(materialKindId(materialKind)) == expectedKind;
}

bool isDerivativeEmissiveMaterialId(int materialId) {
    return materialId == MATERIAL_LAVA ||
           (materialId >= MATERIAL_END_PORTAL && materialId <= MATERIAL_MIDDLE_GLOWING) ||
           materialId == MATERIAL_TEXTURED_EMISSIVE;
}

bool isDerivativeSssMaterialId(int materialId) {
    return materialId == MATERIAL_GRASS_LIKE ||
           materialId == MATERIAL_LEAVES ||
           materialId == MATERIAL_BANNER_SSS ||
           materialId == MATERIAL_SNOW_ICE_SSS;
}

SurfaceMaterial defaultSurfaceMaterial() {
    SurfaceMaterial material;
    material.roughness = 1.0;
    material.f0 = 0.0;
    material.emission = 0.0;
    material.sss = 0.0;
    return material;
}

SurfaceMaterialAux defaultSurfaceMaterialAux() {
    SurfaceMaterialAux aux;
    aux.materialKind = float(MATERIAL_DEFAULT);
    aux.wetnessMask = 0.0;
    aux.porosity = 0.0;
    aux.metalness = 0.0;
    return aux;
}

float derivativeHardcodedSss(int materialId) {
    // Matches DerivativeMain Terrain/DH/Block hardcoded fallback when no
    // resource-pack specular map is present: specularData.a carries SSS.
    if (materialId == MATERIAL_GRASS_LIKE) {
        return 0.45;
    }
    if (materialId == MATERIAL_LEAVES || materialId == MATERIAL_SNOW_ICE_SSS) {
        return 0.70;
    }
    if (materialId == MATERIAL_BANNER_SSS) {
        return 0.65;
    }
    return 0.0;
}

// DerivativeMain: when MC_SPECULAR_MAP is defined, emissiveness comes from specTex.a
// (format 0) or specTex.b (PBR format), then pow(x, EMISSIVE_CURVE) is applied.
// Without PBR textures, emissiveness is 0.0 — all emission comes from per-ID
// BlockLighting.glsl logic instead. Mecraft uses albedo-brightness-based
// emissiveHint as a PBR emission proxy; pass it through directly without
// the old max(hint, 1.0) clamp that forced binary behavior.
float derivativeEmissionHint(int materialId, float emissiveHint) {
    if (isDerivativeEmissiveMaterialId(materialId) || materialId == MATERIAL_ORE || materialId == MATERIAL_NETHER_ORE) {
        return emissiveHint;
    }
    return 0.0;
}

SurfaceMaterial surfaceMaterialForKind(float materialKind, float emissiveHint) {
    SurfaceMaterial material = defaultSurfaceMaterial();
    int materialId = derivativeFragmentMaterialId(materialKindId(materialKind));

    if (materialId == MATERIAL_STAINED_GLASS) {
        material.roughness = 0.04;
        material.f0 = 0.04;
    } else if (materialId == MATERIAL_WATER) {
        material.roughness = 0.0;
        material.f0 = 0.02;
    } else if (materialId == MATERIAL_ICE) {
        material.roughness = 0.10;
        material.f0 = 0.04;
    }

    material.sss = derivativeHardcodedSss(materialId);
    material.emission = derivativeEmissionHint(materialId, emissiveHint);
    return material;
}

SurfaceMaterialAux surfaceMaterialAuxForKind(float materialKind) {
    SurfaceMaterialAux aux = defaultSurfaceMaterialAux();
    int materialId = derivativeFragmentMaterialId(materialKindId(materialKind));
    aux.materialKind = clamp(float(materialId), 0.0, MATERIAL_ID_MAX);

    if (materialId == MATERIAL_STAINED_GLASS || materialId == MATERIAL_WATER || materialId == MATERIAL_ICE) {
        aux.wetnessMask = 1.0;
    }
    return aux;
}

vec4 packGBufferMaterial(SurfaceMaterial material) {
    // colortex3Out.zw in DerivativeMain pack specularData.rg/ba. Our target is
    // expanded RGBA: roughness/f0/emission/sss.
    return vec4(material.roughness, material.f0, material.emission, material.sss);
}

vec4 packGBufferMaterialAux(SurfaceMaterialAux aux) {
    return vec4(clamp(aux.materialKind / MATERIAL_ID_MAX, 0.0, 1.0),
                clamp(aux.wetnessMask, 0.0, 1.0),
                clamp(aux.porosity, 0.0, 1.0),
                clamp(aux.metalness, 0.0, 1.0));
}

SurfaceMaterial unpackGBufferMaterial(vec4 packedMaterial) {
    SurfaceMaterial material;
    material.roughness = clamp(packedMaterial.r, 0.0, 1.0);
    material.f0 = clamp(packedMaterial.g, 0.0, 1.0);
    // DerivativeMain Material.inc:35 — material.emissiveness = pow(emissiveness, EMISSIVE_CURVE)
    material.emission = pow(clamp(packedMaterial.b, 0.0, 1.0), EMISSIVE_CURVE);
    material.sss = clamp(packedMaterial.a, 0.0, 1.0);
    return material;
}

SurfaceMaterialAux unpackGBufferMaterialAux(vec4 packedAux) {
    SurfaceMaterialAux aux;
    aux.materialKind = round(clamp(packedAux.r, 0.0, 1.0) * MATERIAL_ID_MAX);
    aux.wetnessMask = clamp(packedAux.g, 0.0, 1.0);
    aux.porosity = clamp(packedAux.b, 0.0, 1.0);
    aux.metalness = clamp(packedAux.a, 0.0, 1.0);
    return aux;
}

GBufferSurface unpackGBufferSurface(vec4 albedoMaterial, vec4 normalAo, vec4 voxelLight, vec4 packedMaterial) {
    GBufferSurface surface;
    surface.albedo = albedoMaterial.rgb;
    surface.emissiveHint = albedoMaterial.a;
    surface.normal = normalize(normalAo.rgb * 2.0 - 1.0);
    surface.vertexAo = mix(0.72, 1.0, normalAo.a);
    surface.voxelLight = voxelLight.rg;
    surface.material = unpackGBufferMaterial(packedMaterial);
    surface.aux = defaultSurfaceMaterialAux();
    return surface;
}

GBufferSurface unpackGBufferSurface(vec4 albedoMaterial, vec4 normalAo, vec4 voxelLight, vec4 packedMaterial, vec4 packedMaterialAux) {
    GBufferSurface surface = unpackGBufferSurface(albedoMaterial, normalAo, voxelLight, packedMaterial);
    surface.aux = unpackGBufferMaterialAux(packedMaterialAux);
    return surface;
}

struct TranslucentMask {
    bool isWater;
    bool isIce;
    bool isGlass;
    bool isStainedGlass;
    bool isTranslucent;
    vec3 stainedGlassTint;
};

TranslucentMask decodeTranslucentMask(float materialKind) {
    TranslucentMask mask;
    int materialId = derivativeFragmentMaterialId(materialKindId(materialKind));
    mask.isWater = (materialId == MATERIAL_WATER);
    mask.isIce = (materialId == MATERIAL_ICE);
    mask.isStainedGlass = (materialId == MATERIAL_STAINED_GLASS);
    mask.isGlass = mask.isStainedGlass;
    mask.isTranslucent = mask.isWater || mask.isIce || mask.isStainedGlass;
    mask.stainedGlassTint = vec3(1.0);
    return mask;
}

#endif
