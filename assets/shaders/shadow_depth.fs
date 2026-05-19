#version 450 core
#include "derivative_shadow.glsl"

in vec2 vUV;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vNormal;
in vec3 vWorldPos;
flat in int vMaterialKind;
in float vSkylight;
flat in float vTintKind;
in vec2 vTintUV;

uniform sampler2DArray texArray;
uniform sampler2D uNoiseTex;
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform float uAnimationTime;
uniform float uTime;
uniform int uShadowPassMode; // 0 = opaque-only (existing), 1 = transparent/all (water+opaque)

// Shadow color outputs:
// layout 0 = shadowcolor0: RGB = albedo color (for colored shadows / caustics),
//                           A = 0.0 for transparent casters (water/glass), 1.0 for opaque
// layout 1 = shadowcolor1: RG = encoded normal, B = skylight, A = height/aux
layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;

const int MATERIAL_WATER = 17;
const int MATERIAL_STAINED_GLASS = 16;

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

float getWaterWaveHeight(vec2 pos) {
    // Multi-octave noise for water waves
    vec2 p = pos * 0.05 + uTime * vec2(0.04, -0.03);
    float h = 0.0;
    float w = 0.5;
    for (int i = 0; i < 4; ++i) {
        h += texture(uNoiseTex, p).r * w;
        p = p * 2.1 + vec2(1.3, -0.7);
        w *= 0.5;
    }
    return h;
}

vec3 getWaterWaveNormal(vec2 worldXZ) {
    // Finite differences to compute normal from wave height
    float eps = 0.1;
    float h0 = getWaterWaveHeight(worldXZ);
    float hx = getWaterWaveHeight(worldXZ + vec2(eps, 0.0));
    float hz = getWaterWaveHeight(worldXZ + vec2(0.0, eps));
    vec3 dx = vec3(eps, hx - h0, 0.0);
    vec3 dz = vec3(0.0, hz - h0, eps);
    return normalize(cross(dz, dx));
}

vec3 fastRefract(vec3 dir, vec3 normal, float eta) {
    float NdotD = dot(normal, dir);
    float k = 1.0 - eta * eta * (1.0 - NdotD * NdotD);
    if (k < 0.0) return vec3(0.0);
    return dir * eta - normal * (sqrt(k) + NdotD * eta);
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
        vec3 waveNormal = getWaterWaveNormal(vWorldPos.xz);
        float waveH = getWaterWaveHeight(vWorldPos.xz);
        // shadowcolor0: RGB = water tint (blue-ish), A = 0.0 (transparent marker)
        ShadowColor = vec4(0.2, 0.5, 0.8, 0.0);
        // shadowcolor1: RG = encoded normal, B = skylight, A = water height
        // DerivativeMain: shadowcolor1.w = surfaceY / 512 + 0.25
        ShadowNormal = vec4(encodeNormal(waveNormal), vSkylight, vWorldPos.y / 512.0 + 0.25);
    } else {
        // Non-water blocks: existing behavior
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
        // All other non-water materials (including cutout like leaves/grass) cast hard shadows.
        ShadowColor = vec4(shadowColor, shadowAlpha);

        vec3 worldNormal = decodeFaceNormal(vNormal);
        if (isCrossVegetation) {
            worldNormal = vec3(0.0, 1.0, 0.0);
        }
        ShadowNormal = vec4(encodeNormal(worldNormal), vSkylight, 1.0);
    }
}
