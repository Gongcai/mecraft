// Shared DerivativeMain-compatible G-buffer and material contract.
// Material ids mirror DerivativeMain/block.properties after subtracting 10000
// from OptiFine/Iris mc_Entity ids.
//
// Material ID Mapping (Mecraft ↔ DerivativeMain block.properties):
// ┌─────────────────────────┬────┬──────────────────────────────────────────────────────────────────────────────────┐
// │ Category                │ ID │ Blocks (DerivativeMain block.properties)                                          │
// ├─────────────────────────┼────┼──────────────────────────────────────────────────────────────────────────────────┤
// │ Plants (animation)      │  1 │ fern, grass/short_grass, nether_sprouts, warped_roots, crimson_roots              │
// │ Crops                   │  2 │ wheat, carrots, potatoes, beetroots, seagrass                                    │
// │ Flowers                 │  3 │ dandelion, poppy, blue_orchid, allium, tulips, etc.                              │
// │ Tall plants (upper)     │  4 │ sunflower:upper, lilac:upper, tall_grass:upper, etc.                             │
// │ Tall plants (lower)     │  5 │ sunflower:lower, lilac:lower, tall_grass:lower, etc.                             │
// │ Grass-like (merged)     │  6 │ saplings, dead_bush, bamboo, sugar_cane, cactus, kelp, stems, coral blocks, etc. │
// │ Leaves                  │  7 │ oak/spruce/birch/jungle/acacia/dark_oak/cherry/mangrove leaves, vine              │
// │ Banner SSS              │  9 │ wall banners, standing banners                                                   │
// │ Snow/Ice SSS            │ 10 │ frosted_ice, blue_ice, packed_ice, snow, snow_block, powder_snow                 │
// │ Lava                    │ 15 │ lava, flowing_lava                                                               │
// │ Stained glass           │ 16 │ (stained glass variants — translucent mask)                                      │
// │ Water                   │ 17 │ water, flowing_water                                                             │
// │ Ice                     │ 18 │ ice (translucent mask)                                                           │
// │ End portal              │ 19 │ end_portal, end_gateway                                                          │
// │ Total glowing           │ 20 │ candles, froglight, lapis_block, emerald_block, end_rod                          │
// │ Torch-like              │ 21 │ torch, wall_torch, campfire, lantern, furnace, blast_furnace, smoker             │
// │ Fire                    │ 22 │ fire, lava_cauldron                                                              │
// │ Glowstone-like          │ 23 │ glowstone, magma_block, shroomlight, redstone_lamp, jack_o_lantern               │
// │ Sea lantern-like        │ 24 │ sea_lantern, warped_stem, warped_hyphae, redstone_wire                           │
// │ Redstone                │ 25 │ redstone_torch, redstone_wall_torch, repeater, comparator                        │
// │ Soul fire               │ 26 │ sea_pickle, dragon_head, spawner, enchanting_table, soul_fire, soul_torch, etc.   │
// │ Amethyst                │ 27 │ small/medium/large amethyst bud, amethyst_cluster, amethyst_block, budding_amethyst│
// │ Glowberry               │ 28 │ cave_vines_plant:berries=true, cave_vines:berries=true                           │
// │ Rails                   │ 29 │ redstone_block, powered_rail, activator_rail, detector_rail, observer             │
// │ Beacon core             │ 30 │ beacon                                                                           │
// │ Sculk                   │ 31 │ sculk, sculk_catalyst, sculk_shrieker, sculk_vein, sculk_sensor                  │
// │ Glow lichen             │ 32 │ glow_lichen                                                                      │
// │ Partial glowing         │ 33 │ weeping_vines, chorus_plant/flower, crimson/warped_fungus                        │
// │ Middle glowing          │ 34 │ candle_cake, brewing_stand                                                       │
// │ Textured                │ 35 │ (generic textured blocks)                                                        │
// │ Textured emissive       │ 36 │ (textured blocks with emission)                                                  │
// │ Ores                    │ 57 │ iron/copper/gold/redstone/lapis/emerald/diamond ore, deepslate variants, gilded  │
// │ Nether ores             │ 58 │ nether_gold_ore, nether_quartz_ore                                               │
// │ Entity skin             │ 60 │ Mecraft extension: player/mob humanoid renderer                                  │
// └─────────────────────────┴────┴──────────────────────────────────────────────────────────────────────────────────┘
//
// Roughness/F0/Emission/SSS defaults:
// - Block LabPBR _s textures can override the material channels in chunk_gbuffer.frag.
// - When a block has no authored _s contribution, Mecraft keeps per-ID material defaults.
// - SSS defaults follow DerivativeMain: grass=0.45, leaves/snow_ice=0.70, banner=0.65

#ifndef MECRAFT_GBUFFER_CONTRACT_GLSL
#define MECRAFT_GBUFFER_CONTRACT_GLSL

#include "lab_pbr_material.glsl"

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
const int MATERIAL_SKIN = 60;  // Entity skin (player/mob) — Mecraft extension
const int MATERIAL_STATIC_MESH = 61;  // glTF static mesh with colored dielectric F0
const float MATERIAL_ID_MAX = 63.0;

// DerivativeMain material IDs use a shaped proxy instead of authored LabPBR
// emission. Apply that producer-specific curve before storing unified linear
// emission in the G-buffer.
const float DERIVATIVE_EMISSIVE_HINT_CURVE = 2.2;

struct SurfaceMaterial {
    // Every geometry path stores perceptual roughness. BRDF consumers convert
    // it to linear microfacet roughness exactly once.
    float perceptualRoughness;
    // Producer-side LabPBR F0/metal encoding. This value is resolved to RGB
    // before the G-buffer is written and is never reconstructed by consumers.
    float encodedF0OrMetalId;
    float specularF90;
    // Packed emission and SSS contributions.
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
    vec3 specularF0;
    float specularF90;
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
    material.perceptualRoughness = 1.0;
    material.encodedF0OrMetalId = 0.0;
    material.specularF90 = 1.0;
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
    // Matches DerivativeMain Terrain/DH/Block per-ID values when no
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
    // Mecraft extension: entity skin subsurface scattering.
    if (materialId == MATERIAL_SKIN) {
        return 0.35;
    }
    return 0.0;
}

// DerivativeMain: when MC_SPECULAR_MAP is defined, emissiveness comes from
// specTex.a (format 0) or specTex.b (PBR format), then a 2.2 curve is applied.
// Without PBR textures, emissiveness is 0.0 — all emission comes from per-ID
// BlockLighting.glsl logic instead. Mecraft uses albedo-brightness-based
// emissiveHint as a PBR emission proxy; pass it through directly without
// the old max(hint, 1.0) clamp that forced binary behavior.
float derivativeEmissionHint(int materialId, float emissiveHint) {
    if (isDerivativeEmissiveMaterialId(materialId) || materialId == MATERIAL_ORE || materialId == MATERIAL_NETHER_ORE) {
        return pow(clamp(emissiveHint, 0.0, 1.0),
                   DERIVATIVE_EMISSIVE_HINT_CURVE);
    }
    return 0.0;
}

SurfaceMaterial surfaceMaterialForKind(float materialKind, float emissiveHint) {
    SurfaceMaterial material = defaultSurfaceMaterial();
    int materialId = derivativeFragmentMaterialId(materialKindId(materialKind));

    // Per-ID material defaults used when no block _s texture contribution is present.
    if (materialId == MATERIAL_STAINED_GLASS) {
        material.perceptualRoughness = 0.20;
        material.encodedF0OrMetalId = 0.04;
    } else if (materialId == MATERIAL_WATER) {
        material.perceptualRoughness = 0.0;
        material.encodedF0OrMetalId = 0.02;
    } else if (materialId == MATERIAL_ICE) {
        material.perceptualRoughness = 0.31622777;
        material.encodedF0OrMetalId = 0.04;
    } else if (materialId == MATERIAL_SKIN) {
        // Mecraft extension: entity skin — moderate roughness, standard dielectric.
        material.perceptualRoughness = 0.80622577;
        material.encodedF0OrMetalId = 0.04;
    }

    // SSS from DerivativeMain per-ID defaults.
    // grass=0.45, leaves/snow_ice=0.70, banner=0.65, skin=0.35
    material.sss = derivativeHardcodedSss(materialId);
    // Emission from BlockLighting.glsl per-ID logic; pass albedo-based hint through.
    material.emission = derivativeEmissionHint(materialId, emissiveHint);
    return material;
}

SurfaceMaterialAux surfaceMaterialAuxForKind(float materialKind) {
    SurfaceMaterialAux aux = defaultSurfaceMaterialAux();
    int materialId = derivativeFragmentMaterialId(materialKindId(materialKind));
    aux.materialKind = clamp(float(materialId), 0.0, MATERIAL_ID_MAX);

    // Wetness mask: translucent surfaces (water/ice/glass) are fully wet.
    if (materialId == MATERIAL_STAINED_GLASS || materialId == MATERIAL_WATER || materialId == MATERIAL_ICE) {
        aux.wetnessMask = 1.0;
    }
    // Porosity: skin has moderate porosity for wetness absorption.
    // LabPBR _s textures can raise metalness in chunk_gbuffer.frag.
    if (materialId == MATERIAL_SKIN) {
        aux.wetnessMask = 0.6;
        aux.porosity = 0.3;
    }
    return aux;
}

// --- Normal + vertex AO packing (RGB10A2) ---
// RG: octahedral-encoded world normal (10 bits per axis), B: vertex AO.
// The +Z normal encodes to (0.5, 0.5), so the legacy clear value
// (0.5, 0.5, 1.0, 1.0) still means "up-facing, fully unoccluded".
vec2 octWrapNormal(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0,
                                    v.y >= 0.0 ? 1.0 : -1.0);
}

vec4 packGBufferNormalAo(vec3 normal, float vertexAo) {
    vec3 n = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    vec2 encoded = n.z >= 0.0 ? n.xy : octWrapNormal(n.xy);
    return vec4(encoded * 0.5 + 0.5, vertexAo, 0.0);
}

vec3 unpackGBufferNormal(vec4 packedNormalAo) {
    vec2 f = packedNormalAo.rg * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

float unpackGBufferVertexAo(vec4 packedNormalAo) {
    return packedNormalAo.b;
}

vec4 packGBufferMaterial(SurfaceMaterial material) {
    return vec4(material.perceptualRoughness, material.specularF90,
                material.emission, material.sss);
}

vec4 packGBufferF0Metallic(vec3 specularF0, float metalness) {
    return vec4(clamp(specularF0, vec3(0.0), vec3(1.0)),
                clamp(metalness, 0.0, 1.0));
}

vec4 packGBufferMaterialAux(SurfaceMaterialAux aux) {
    return vec4(clamp(aux.materialKind / MATERIAL_ID_MAX, 0.0, 1.0),
                clamp(aux.wetnessMask, 0.0, 1.0),
                clamp(aux.porosity, 0.0, 1.0),
                clamp(aux.metalness, 0.0, 1.0));
}

SurfaceMaterial unpackGBufferMaterial(vec4 packedMaterial) {
    SurfaceMaterial material;
    material.perceptualRoughness = clamp(packedMaterial.r, 0.0, 1.0);
    material.encodedF0OrMetalId = 0.0;
    material.specularF90 = clamp(packedMaterial.g, 0.0, 1.0);
    material.emission = clamp(packedMaterial.b, 0.0, 1.0);
    material.sss = clamp(packedMaterial.a, 0.0, 1.0);
    return material;
}

float linearMaterialRoughness(SurfaceMaterial material) {
    float perceptual = clamp(material.perceptualRoughness, 0.0, 1.0);
    return perceptual * perceptual;
}

SurfaceMaterialAux unpackGBufferMaterialAux(vec4 packedAux) {
    SurfaceMaterialAux aux;
    aux.materialKind = round(clamp(packedAux.r, 0.0, 1.0) * MATERIAL_ID_MAX);
    aux.wetnessMask = clamp(packedAux.g, 0.0, 1.0);
    aux.porosity = clamp(packedAux.b, 0.0, 1.0);
    aux.metalness = clamp(packedAux.a, 0.0, 1.0);
    return aux;
}

GBufferSurface unpackGBufferSurface(vec4 albedoMaterial,
                                    vec4 normalAo,
                                    vec4 voxelLight,
                                    vec4 packedMaterial,
                                    vec4 packedMaterialAux,
                                    vec4 packedF0Metallic) {
    GBufferSurface surface;
    surface.albedo = albedoMaterial.rgb;
    surface.emissiveHint = albedoMaterial.a;
    surface.normal = unpackGBufferNormal(normalAo);
    surface.vertexAo = mix(0.72, 1.0, unpackGBufferVertexAo(normalAo));
    surface.voxelLight = voxelLight.rg;
    surface.material = unpackGBufferMaterial(packedMaterial);
    surface.aux = unpackGBufferMaterialAux(packedMaterialAux);
    surface.specularF0 = clamp(packedF0Metallic.rgb, vec3(0.0), vec3(1.0));
    surface.aux.metalness = clamp(packedF0Metallic.a, 0.0, 1.0);
    surface.specularF90 = surface.material.specularF90;
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
