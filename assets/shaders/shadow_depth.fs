#version 450 core

in vec2 vUV;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vNormal;
in vec3 vWorldPos;
flat in int vMaterialKind;

uniform sampler2DArray texArray;
uniform sampler2D uNoiseTex;
uniform int uForceBaseLod;
uniform float uAnimationTime;
uniform float uTime;

// Shadow color outputs:
// layout 0 = shadowcolor0: RGB = albedo color (for colored shadows / caustics)
// layout 1 = shadowcolor1: RG = encoded normal, B = skylight, A = height/aux
layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;

const int MATERIAL_WATER = 17;

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

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
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
        // Water caustics (DerivativeMain approach)
        vec3 wavesNormal = getWaterWaveNormal(vWorldPos.xz);

        // Refract downward light through water surface
        vec3 oldPos = vWorldPos;
        vec3 newPos = oldPos + fastRefract(vec3(0.0, -1.0, 0.0), wavesNormal, 1.0 / 1.333) * 6.0;

        // Area ratio: old area / new area = caustics intensity
        float oldArea = dot(dFdx(oldPos), dFdx(oldPos)) * dot(dFdy(oldPos), dFdy(oldPos));
        float newArea = dot(dFdx(newPos), dFdx(newPos)) * dot(dFdy(newPos), dFdy(newPos));

        float caustics = inversesqrt(oldArea / newArea) * 0.3;
        caustics = clamp(caustics * caustics * 2.0, 0.0, 1.0);

        ShadowColor = vec4(vec3(caustics), 1.0);
        ShadowNormal = vec4(encodeNormal(wavesNormal), 1.0, vWorldPos.y * (1.0 / 512.0) + 0.25);
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
        if (texColor.a >= 254.0 / 255.0) {
            shadowColor = texColor.rgb;
        } else {
            shadowColor = mix(vec3(1.0), texColor.rgb, pow(clamp(texColor.a, 0.0, 1.0), 0.4));
        }
        shadowColor = srgbToLinear(shadowColor);
        ShadowColor = vec4(shadowColor, 1.0);

        vec3 worldNormal = decodeFaceNormal(vNormal);
        if (isCrossVegetation) {
            worldNormal = vec3(0.0, 1.0, 0.0);
        }
        ShadowNormal = vec4(encodeNormal(worldNormal), 1.0, 1.0);
    }
}
