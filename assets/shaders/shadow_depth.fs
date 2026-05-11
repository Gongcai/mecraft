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
    // Octahedral encoding (simplified for 2-component output)
    n = normalize(n);
    float p = sqrt(n.z * 8.0 + 8.0);
    return n.xy / p * 0.5 + 0.5;
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

    // Output albedo color for colored shadows
    ShadowColor = vec4(texColor.rgb, 1.0);

    // Output encoded normal (fallback: up vector for surfaces without normal mapping)
    // vNormal is passed as a flat float encoding; for now output up vector as placeholder.
    // Future: decode actual normal from vertex data when shadow pass has proper TBN.
    vec3 worldNormal = vec3(0.0, 1.0, 0.0);
    ShadowNormal = vec4(encodeNormal(worldNormal), 0.0, 1.0);
}
