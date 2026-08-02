#version 450 core
#include "derivative_shadow.glsl"
#include "terrain_material_sampling.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vLayer;
layout(location = 2) in float vAnimationFrameCount;
layout(location = 3) in float vAnimationFps;
layout(location = 4) in float vAnimated;
layout(location = 5) in float vNormal;
layout(location = 6) in vec3 vWorldPos;
layout(location = 7) flat in int vMaterialKind;
layout(location = 8) in float vSkylight;
layout(location = 9) flat in float vTintKind;
layout(location = 10) in vec2 vTintUV;

#ifdef RHI_TERRAIN_SHADOW_MDI
layout(set = 1, binding = 0) uniform sampler2DArray texArray;
layout(set = 1, binding = 1) uniform sampler2D uNoiseTex;
layout(set = 1, binding = 2) uniform sampler2D uGrassColormap;
layout(set = 1, binding = 3) uniform sampler2D uFoliageColormap;
#include "terrain_shadow_params.glsl"
#else
uniform sampler2DArray texArray;
uniform sampler2D uNoiseTex;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform float uAnimationTime;
uniform float uTime;
uniform vec3 uShadowLightDirection;
uniform mat4 uShadowModelView;
uniform int uShadowPassMode; // 0 = opaque-only, 1 = transparent shadow (DepthAll + Color)
#endif

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

// Shadow color outputs:
// layout 0 = shadowcolor0: RGB = albedo color (for colored shadows / caustics),
//                           A = 0.0 for transparent casters (water/glass), 1.0 for opaque
// layout 1 = shadowcolor1: RG = encoded normal, B = skylight, A = height/aux
#ifndef RHI_TERRAIN_SHADOW_DEPTH_ONLY
layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;
#endif

const int MATERIAL_WATER = 17;
const int MATERIAL_STAINED_GLASS = 16;
const int MATERIAL_LEAVES = 7;
const float WATER_CAUSTIC_PROJECTION_DISTANCE = 6.0;

vec2 encodeNormal(vec3 n) {
    n = normalize(n);
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    vec2 enc = n.xy;
    if (n.z < 0.0) {
        enc = (vec2(1.0) - abs(enc.yx)) * vec2(enc.x >= 0.0 ? 1.0 : -1.0,
                                               enc.y >= 0.0 ? 1.0 : -1.0);
    }
    return enc * 0.5 + 0.5;
}

vec3 decodeFaceNormal(float face) {
    if (face > -2.5 && face < -0.5) {
        return vec3(0.0, 1.0, 0.0);
    }
    int idx = int(round(face));
    if (idx == 0) return vec3(0.0, 1.0, 0.0);
    if (idx == 1) return vec3(0.0, -1.0, 0.0);
    if (idx == 2) return vec3(0.0, 0.0, 1.0);
    if (idx == 3) return vec3(0.0, 0.0, -1.0);
    if (idx == 4) return vec3(-1.0, 0.0, 0.0);
    return vec3(1.0, 0.0, 0.0);
}

// ---- Water caustics (ported from DerivativeMain Shadow.frag) ----

vec2 curve2(vec2 x) {
    return x * x * (3.0 - 2.0 * x);
}

float textureSmooth(vec2 coord) {
    coord += 0.5;
    vec2 whole = floor(coord);
    vec2 part = curve2(coord - whole);
    coord = whole + part - 0.5;
    return texture(uNoiseTex, coord * (1.0 / 256.0)).r;
}

float getWaterWaveHeight(vec2 pos) {
    // DerivativeMain/lib/Water/WaterWave.glsl-style layered textureSmooth waves.
    float wavesTime = uTime * 0.6;
    pos.y *= 0.8;
    float wave = 0.0;
    wave += textureSmooth((pos + vec2(0.0, pos.x - wavesTime)) * 0.8);
    wave += textureSmooth((pos - vec2(-wavesTime, pos.x)) * 1.6) * 0.5;
    wave += textureSmooth((pos + vec2(wavesTime * 0.6, pos.x - wavesTime)) * 2.4) * 0.2;
    wave += textureSmooth((pos - vec2(wavesTime * 0.6, pos.x - wavesTime)) * 3.6) * 0.1;
    return wave;
}

vec3 getWaterWaveNormal(vec2 worldXZ) {
    float h0 = getWaterWaveHeight(worldXZ);
    float hx = getWaterWaveHeight(worldXZ + vec2(0.04, 0.0));
    float hz = getWaterWaveHeight(worldXZ + vec2(0.0, 0.04));
    vec2 waveNormal = vec2(h0 - hx, h0 - hz);
    return normalize(vec3(waveNormal.x, 0.5, waveNormal.y));
}

vec3 fastRefract(vec3 dir, vec3 normal, float eta) {
    float NdotD = dot(normal, dir);
    float k = 1.0 - eta * eta * (1.0 - NdotD * NdotD);
    if (k < 0.0) return vec3(0.0);
    return dir * eta - normal * (sqrt(k) + NdotD * eta);
}

float waterCausticStrength(vec3 normal) {
    vec3 oldPos = mat3(uShadowModelView) * vWorldPos + uShadowModelView[3].xyz;
    vec3 incidentLight = -normalize(uShadowLightDirection);
    vec3 viewNormal = normalize(mat3(uShadowModelView) * normal);
    vec3 newPos = oldPos + fastRefract(vec3(0.0, 0.0, -1.0), viewNormal, 1.0 / 1.33) * WATER_CAUSTIC_PROJECTION_DISTANCE;
    float oldArea = dot(dFdx(oldPos), dFdx(oldPos)) * dot(dFdy(oldPos), dFdy(oldPos));
    float newArea = dot(dFdx(newPos), dFdx(newPos)) * dot(dFdy(newPos), dFdy(newPos));
    float caustics = inversesqrt(max(oldArea / max(newArea, 1.0e-6), 1.0e-6)) * 0.3;
    float lightBand = 0.65 + 0.35 * clamp(dot(normalize(normal), -normalize(uShadowLightDirection)), 0.0, 1.0);
    float banded = caustics * lightBand;
    return clamp(banded, 0.25, 2.5);
}

// ---- Main ----

void main() {
    if (vMaterialKind == MATERIAL_WATER) {
        if (uShadowPassMode == 0) {
            // Opaque pass: water must not block direct light
            discard;
        }
        // Transparent pass: write water depth + caustics data
        // (DerivativeMain shadowtex0/shadowcolor0/shadowcolor1 for water)
#ifndef RHI_TERRAIN_SHADOW_DEPTH_ONLY
        vec3 waveNormal = getWaterWaveNormal(vWorldPos.xz - vWorldPos.y);
        float caustics = waterCausticStrength(waveNormal);
        // shadowcolor0: RGB = water caustic/tint, A = 0.0 (transparent marker)
        ShadowColor = vec4(vec3(sqrt(sqrt(caustics))), 0.0);
        // shadowcolor1: RG = encoded normal, B = skylight, A = water height
        // DerivativeMain: shadowcolor1.w = surfaceY / 512 + 0.25
        ShadowNormal = vec4(encodeNormal(waveNormal), vSkylight, vWorldPos.y / 512.0 + 0.25);
#endif
    } else {
        if (uShadowPassMode != 0 && vMaterialKind != MATERIAL_STAINED_GLASS) {
            discard;
        }

        // Non-water blocks: existing behavior
        bool isCrossVegetation = (vNormal > -2.5 && vNormal < -0.5);
        bool forceBaseLod = (uForceBaseLod != 0) || isCrossVegetation;
        float sampledLayer = terrainAnimatedTextureLayer(vLayer, vAnimationFrameCount, vAnimationFps, vAnimated,
                                                          uAnimationTime);
        vec4 texColor = forceBaseLod
            ? textureLod(texArray, vec3(vUV, sampledLayer), 0.0)
            : texture(texArray, vec3(vUV, sampledLayer));
        // Leaves intentionally remain solid shadow casters; every other terrain cutout uses the shared alpha test.
        bool solidFoliageCaster = (vMaterialKind == MATERIAL_LEAVES);
        if (!solidFoliageCaster && !terrainAlphaTestPasses(texColor.a)) {
            discard;
        }
        if (solidFoliageCaster) {
            texColor.a = 1.0;
        }

        vec3 shadowColor = texColor.rgb;
        // DerivativeMain Shadow.frag:75 — alpha > 254/255 (strict greater-than)
        if (texColor.a > 254.0 / 255.0) {
            shadowColor = texColor.rgb;
        } else {
            shadowColor = mix(vec3(1.0), texColor.rgb, pow(clamp(texColor.a, 0.0, 1.0), 0.4));
        }

        // DerivativeMain Shadow.frag:76,78 — shadowcolor0Out = albedo.rgb * tint
        // Mecraft applies biome tint via colormap lookup (grass/foliage).
        // The tint color is in sRGB space; shadowColor is also sRGB (no linear conversion).
        if (vTintKind > 0.5 && vTintKind < 1.5) {
            shadowColor *= texture(uGrassColormap, vTintUV).rgb;
        } else if (vTintKind > 1.5 && vTintKind < 2.5) {
            shadowColor *= texture(uFoliageColormap, vTintUV).rgb;
        } else if (vTintKind > 2.5 && vTintKind < 3.5) {
            shadowColor *= redstoneTintSrgb(vTintUV);
        }

        // DerivativeMain writes sRGB values directly; PCF reader applies pow4() decode
        // which assumes sRGB-space input. Do NOT convert to linear here.

        // Colored shadow alpha semantics (replaces DerivativeMain's dual-depth detection):
        //   a = 0.0 → transparent caster: light passes through with color tint (water, stained glass)
        //   a = 1.0 → opaque caster: blocks light completely (hard shadow)
        //
        // DerivativeMain uses shadowtex0 (all) vs shadowtex1 (opaque-only) to detect
        // transparent casters. Leaves/grass are in BOTH textures → never trigger colored shadows.
        // Previous code marked any texColor.a < 254/255 as transparent, which incorrectly
        // treated cutout materials (leaves, grass, flowers) as colored shadow casters,
        // causing pow4(leaf_color) * sampleLit ≈ 0.01 instead of 1.0 on lit surfaces.
        float shadowAlpha = 1.0;
        if (vMaterialKind == MATERIAL_STAINED_GLASS) {
            if (uShadowPassMode == 0) {
                discard;
            }
            // Transparent pass: stained glass writes colored shadow
            shadowAlpha = 0.0; // marks as transparent caster
        }
#ifndef RHI_TERRAIN_SHADOW_DEPTH_ONLY
        // All other non-water materials (including cutout like leaves/grass) cast hard shadows.
        ShadowColor = vec4(shadowColor, shadowAlpha);

        vec3 worldNormal = decodeFaceNormal(vNormal);
        if (isCrossVegetation) {
            worldNormal = vec3(0.0, 1.0, 0.0);
        }
        ShadowNormal = vec4(encodeNormal(worldNormal), vSkylight, 1.0);
#endif
    }
}
