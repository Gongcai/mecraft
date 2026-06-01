#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;
layout (location = 3) in float aWindWeight;
layout (location = 4) in float aLayer;
layout (location = 10) in uint aTintPacked;

uniform mat4 model;
uniform mat4 view;
uniform mat4 viewProj;
uniform float uWindTime;
uniform float uWindStrength;
uniform float uWindSpeed;
uniform float uWindSpatialFreq;
uniform int uVertexFormat;

out vec2 vUV;
out float vLayer;
out float vNormal;
out float vFogDist;
flat out float vTintKind;
out vec2 vTintUV;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);

    if (aWindWeight > 0.001) {
        float phase = worldPos.x * uWindSpatialFreq + worldPos.z * (uWindSpatialFreq * 1.37) + uWindTime * uWindSpeed;
        float swayX = sin(phase);
        float swayZ = cos(phase * 0.91 + 1.7);
        worldPos.xz += vec2(swayX, swayZ) * (uWindStrength * aWindWeight);
    }

    vec4 viewPos = view * worldPos;
    gl_Position = viewProj * worldPos;

    vUV = aUV;
    vLayer = aLayer;
    vNormal = aNormal;
    vFogDist = max(0.0, -viewPos.z);

    uint tintKind = (aTintPacked >> 14u) & 3u;
    uint tintU = (aTintPacked >> 4u) & 15u;
    uint tintV = aTintPacked & 15u;
    vTintKind = float(tintKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}

