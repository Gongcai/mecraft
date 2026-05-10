#version 450 core

layout (location = 0) out vec4 GAlbedoMaterial;
layout (location = 1) out vec4 GNormalAo;
layout (location = 2) out vec4 GVoxelLight;
layout (location = 3) out vec4 GMaterial;

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
uniform sampler2D uGrassColormap;
uniform sampler2D uFoliageColormap;
uniform int uForceBaseLod;
uniform float uAnimationTime;

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

vec4 vanillaMaterialParams(float materialKind, float emissiveHint) {
    float roughness = 0.86;
    float f0 = 0.035;
    float emission = 0.0;
    float sss = 0.0;
    int kind = int(round(materialKind));

    if (kind == 1) {          // stone
        roughness = 0.92;
        f0 = 0.040;
    } else if (kind == 2) {   // dirt
        roughness = 0.98;
        f0 = 0.025;
    } else if (kind == 3) {   // grass
        roughness = 0.94;
        f0 = 0.030;
        sss = 0.14;
    } else if (kind == 4) {   // wood
        roughness = 0.76;
        f0 = 0.040;
    } else if (kind == 5) {   // leaves
        roughness = 0.88;
        f0 = 0.030;
        sss = 0.42;
    } else if (kind == 6) {   // plant
        roughness = 0.90;
        f0 = 0.025;
        sss = 0.55;
    } else if (kind == 7) {   // sand
        roughness = 1.00;
        f0 = 0.020;
    } else if (kind == 8) {   // glass
        roughness = 0.08;
        f0 = 0.060;
    } else if (kind == 9) {   // water
        roughness = 0.03;
        f0 = 0.020;
    } else if (kind == 10) {  // ore
        roughness = 0.50;
        f0 = 0.090;
    } else if (kind == 11) {  // emissive
        roughness = 0.52;
        f0 = 0.050;
        emission = emissiveHint;
    } else if (kind == 12) {  // metal-ish vanilla blocks
        roughness = 0.38;
        f0 = 0.180;
    }

    return vec4(roughness, f0, emission, sss);
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
    bool isEmissiveMaterial = int(round(vMaterialKind)) == 11;
    float emissiveLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float emissivePeak = max(max(albedo.r, albedo.g), albedo.b);
    float emissiveMask = smoothstep(0.34, 0.72, max(emissiveLuma, emissivePeak * 0.72));
    float emissiveHint = isEmissiveMaterial ? emissiveMask * clamp(vBlockLight * 1.25, 0.0, 1.0) : 0.0;

    GAlbedoMaterial = vec4(albedo, emissiveHint);
    GNormalAo = vec4(normal * 0.5 + 0.5, ao);
    GVoxelLight = vec4(clamp(vSunlight, 0.0, 1.0), clamp(vBlockLight, 0.0, 1.0), 0.0, 1.0);
    GMaterial = vanillaMaterialParams(vMaterialKind, emissiveHint);
}
