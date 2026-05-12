// Shared G-buffer and material contract for the built-in shader preset.
// Keep these ids mirrored with BlockMaterialKinds in src/world/Block.h.

#ifndef MECRAFT_GBUFFER_CONTRACT_GLSL
#define MECRAFT_GBUFFER_CONTRACT_GLSL

const int MATERIAL_DEFAULT = 0;
const int MATERIAL_STONE = 1;
const int MATERIAL_DIRT = 2;
const int MATERIAL_GRASS = 3;
const int MATERIAL_WOOD = 4;
const int MATERIAL_LEAVES = 5;
const int MATERIAL_PLANT = 6;
const int MATERIAL_SAND = 7;
const int MATERIAL_GLASS = 8;
const int MATERIAL_WATER = 9;
const int MATERIAL_ORE = 10;
const int MATERIAL_EMISSIVE = 11;
const int MATERIAL_METAL = 12;
const int MATERIAL_ICE = 13;
const int MATERIAL_STAINED_GLASS = 14;

struct SurfaceMaterial {
    float roughness;
    float f0;
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

bool isMaterialKind(float materialKind, int expectedKind) {
    return materialKindId(materialKind) == expectedKind;
}

SurfaceMaterial defaultSurfaceMaterial() {
    SurfaceMaterial material;
    material.roughness = 0.84;
    material.f0 = 0.040;
    material.emission = 0.0;
    material.sss = 0.0;
    return material;
}

SurfaceMaterialAux defaultSurfaceMaterialAux() {
    SurfaceMaterialAux aux;
    aux.materialKind = float(MATERIAL_DEFAULT);
    aux.wetnessMask = 0.0;
    aux.porosity = 0.65;
    aux.metalness = 0.0;
    return aux;
}

SurfaceMaterial surfaceMaterialForKind(float materialKind, float emissiveHint) {
    SurfaceMaterial material = defaultSurfaceMaterial();
    int kind = materialKindId(materialKind);

    if (kind == MATERIAL_STONE) {
        material.roughness = 0.78;
        material.f0 = 0.055;
    } else if (kind == MATERIAL_DIRT) {
        material.roughness = 0.96;
        material.f0 = 0.030;
    } else if (kind == MATERIAL_GRASS) {
        material.roughness = 0.88;
        material.f0 = 0.035;
        material.sss = 0.26;
    } else if (kind == MATERIAL_WOOD) {
        material.roughness = 0.68;
        material.f0 = 0.050;
    } else if (kind == MATERIAL_LEAVES) {
        material.roughness = 0.74;
        material.f0 = 0.040;
        material.sss = 0.72;
    } else if (kind == MATERIAL_PLANT) {
        material.roughness = 0.82;
        material.f0 = 0.032;
        material.sss = 0.78;
    } else if (kind == MATERIAL_SAND) {
        material.roughness = 0.92;
        material.f0 = 0.026;
    } else if (kind == MATERIAL_GLASS) {
        material.roughness = 0.08;
        material.f0 = 0.060;
    } else if (kind == MATERIAL_WATER) {
        material.roughness = 0.03;
        material.f0 = 0.020;
    } else if (kind == MATERIAL_ORE) {
        material.roughness = 0.42;
        material.f0 = 0.120;
    } else if (kind == MATERIAL_EMISSIVE) {
        material.roughness = 0.44;
        material.f0 = 0.060;
        material.emission = pow(emissiveHint, 1.35);
    } else if (kind == MATERIAL_METAL) {
        material.roughness = 0.30;
        material.f0 = 0.260;
    }

    return material;
}

SurfaceMaterialAux surfaceMaterialAuxForKind(float materialKind) {
    SurfaceMaterialAux aux = defaultSurfaceMaterialAux();
    int kind = materialKindId(materialKind);
    aux.materialKind = clamp(float(kind), 0.0, 15.0);

    if (kind == MATERIAL_STONE) {
        aux.wetnessMask = 0.62;
        aux.porosity = 0.44;
    } else if (kind == MATERIAL_DIRT) {
        aux.wetnessMask = 0.88;
        aux.porosity = 0.82;
    } else if (kind == MATERIAL_GRASS) {
        aux.wetnessMask = 0.78;
        aux.porosity = 0.70;
    } else if (kind == MATERIAL_WOOD) {
        aux.wetnessMask = 0.48;
        aux.porosity = 0.58;
    } else if (kind == MATERIAL_LEAVES) {
        aux.wetnessMask = 0.68;
        aux.porosity = 0.34;
    } else if (kind == MATERIAL_PLANT) {
        aux.wetnessMask = 0.72;
        aux.porosity = 0.52;
    } else if (kind == MATERIAL_SAND) {
        aux.wetnessMask = 0.55;
        aux.porosity = 0.90;
    } else if (kind == MATERIAL_GLASS) {
        aux.wetnessMask = 0.18;
        aux.porosity = 0.02;
    } else if (kind == MATERIAL_WATER) {
        aux.wetnessMask = 1.0;
        aux.porosity = 0.0;
    } else if (kind == MATERIAL_ORE) {
        aux.wetnessMask = 0.38;
        aux.porosity = 0.18;
    } else if (kind == MATERIAL_EMISSIVE) {
        aux.wetnessMask = 0.15;
        aux.porosity = 0.10;
    } else if (kind == MATERIAL_METAL) {
        aux.wetnessMask = 0.20;
        aux.porosity = 0.04;
        aux.metalness = 1.0;
    }

    return aux;
}

vec4 packGBufferMaterial(SurfaceMaterial material) {
    return vec4(material.roughness, material.f0, material.emission, material.sss);
}

vec4 packGBufferMaterialAux(SurfaceMaterialAux aux) {
    return vec4(clamp(aux.materialKind / 15.0, 0.0, 1.0),
                clamp(aux.wetnessMask, 0.0, 1.0),
                clamp(aux.porosity, 0.0, 1.0),
                clamp(aux.metalness, 0.0, 1.0));
}

SurfaceMaterial unpackGBufferMaterial(vec4 packedMaterial) {
    SurfaceMaterial material;
    material.roughness = clamp(packedMaterial.r, 0.03, 1.0);
    material.f0 = clamp(packedMaterial.g, 0.02, 0.35);
    material.emission = clamp(packedMaterial.b, 0.0, 1.0);
    material.sss = clamp(packedMaterial.a, 0.0, 1.0);
    return material;
}

SurfaceMaterialAux unpackGBufferMaterialAux(vec4 packedAux) {
    SurfaceMaterialAux aux;
    aux.materialKind = round(clamp(packedAux.r, 0.0, 1.0) * 15.0);
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

//----------------------------------------------------------------------------//
// Translucent mask — classifies translucent material types for composite passes.
// Matches DerivativeMain lib/Head/Mask.inc TranslucentMask semantics.
//----------------------------------------------------------------------------//

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
    int kind = int(round(materialKind));
    mask.isWater = (kind == MATERIAL_WATER);
    mask.isIce = (kind == MATERIAL_ICE);
    mask.isGlass = (kind == MATERIAL_GLASS);
    mask.isStainedGlass = (kind == MATERIAL_STAINED_GLASS);
    mask.isTranslucent = mask.isWater || mask.isIce || mask.isGlass || mask.isStainedGlass;
    mask.stainedGlassTint = vec3(1.0);
    return mask;
}

#endif
