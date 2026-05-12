#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;
layout (location = 3) in float aSunlight;
layout (location = 4) in float aBlockLight;
layout (location = 5) in float aAO;
layout (location = 6) in float aLayer;
layout (location = 7) in float aAnimationFrameCount;
layout (location = 8) in float aAnimationFps;
layout (location = 9) in float aAnimated;
layout (location = 10) in uint aTintPacked;

uniform mat4 view;
uniform mat4 viewProj;
uniform mat4 model;
uniform int uUseModel;

out vec2 vUV;
out float vSunlight;
out float vBlockLight;
out float vAO;
out float vNormal;
out float vLayer;
out float vAnimationFrameCount;
out float vAnimationFps;
out float vAnimated;
flat out float vTintKind;
flat out float vMaterialKind;
out vec2 vTintUV;
out vec3 vWorldPos;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;
    gl_Position = viewProj * worldPos;

    vUV = aUV;
    vSunlight = aSunlight;
    vBlockLight = aBlockLight;
    vAO = aAO;
    vNormal = aNormal;
    vLayer = aLayer;
    vAnimationFrameCount = aAnimationFrameCount;
    vAnimationFps = aAnimationFps;
    vAnimated = aAnimated;
    vWorldPos = worldPos.xyz;

    uint tintKind = (aTintPacked >> 14u) & 3u;
    uint materialKind = (aTintPacked >> 8u) & 63u;
    uint tintU = (aTintPacked >> 4u) & 15u;
    uint tintV = aTintPacked & 15u;
    vTintKind = float(tintKind);
    vMaterialKind = float(materialKind);
    vTintUV = (vec2(float(tintU), float(tintV)) + vec2(0.5)) / 16.0;
}
