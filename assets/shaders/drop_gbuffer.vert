// Block drop GBuffer vertex shader — Mecraft Phase 5.3 extension.
// Renders dropped block items into the same GBuffer as terrain and entities
// so they receive deferred lighting, CSM shadows, SSAO, and volumetric fog.
// Vertex layout matches BlockVertex: pos(3f), uv(2f), normal(1b), sunlight, blockLight,
// ao, layer, animFrameCount, animFps, animated, tintPacked.
// Based on chunk_gbuffer.vs with model matrix always applied (drops are in local space).

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

uniform mat4 viewProj;
uniform mat4 model;
uniform mat4 prevModel;
uniform mat4 prevViewProj;

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
out vec2 vVelocity;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
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

    // Per-object velocity for TAA/motion blur.
    vec4 prevClip = prevViewProj * prevModel * vec4(aPos, 1.0);
    vec2 curNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.00001);
    vVelocity = (curNdc - prevNdc) * 0.5;
}
