#version 450 core

in vec2 vUV;
in float vLayer;
in float vAnimationFrameCount;
in float vAnimationFps;
in float vAnimated;
in float vNormal;

uniform sampler2DArray texArray;
uniform int uForceBaseLod;
uniform float uAnimationTime;

// Shadow color outputs:
// layout 0 = shadowcolor0: RGB = albedo color (for colored shadows / caustics)
// layout 1 = shadowcolor1: RG = encoded normal (for shadow normal / SSS)
layout(location = 0) out vec4 ShadowColor;
layout(location = 1) out vec4 ShadowNormal;

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

    vec3 shadowColor = srgbToLinear(texColor.rgb);
    shadowColor = mix(vec3(1.0), shadowColor, pow(clamp(texColor.a, 0.0, 1.0), 0.45));
    ShadowColor = vec4(shadowColor, 1.0);

    vec3 worldNormal = decodeFaceNormal(vNormal);
    if (isCrossVegetation) {
        worldNormal = vec3(0.0, 1.0, 0.0);
    }
    ShadowNormal = vec4(encodeNormal(worldNormal), 0.0, 1.0);
}
