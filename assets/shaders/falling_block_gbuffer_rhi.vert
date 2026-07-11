#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in int aNormal;
layout(location = 3) in float aSunlight;
layout(location = 4) in float aBlockLight;
layout(location = 5) in uint aAo;
layout(location = 6) in uint aLayer;
layout(location = 7) in uint aAnimationFrameCount;
layout(location = 8) in uint aAnimationFps;
layout(location = 9) in uint aAnimated;
layout(location = 10) in uint aTintPacked;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vVertexData;
layout(location = 2) out vec4 vAnimationData;
layout(location = 3) flat out uvec2 vMaterialTint;
layout(location = 4) out vec2 vTintUv;
layout(location = 5) out vec3 vWorldPosition;
layout(location = 6) out vec2 vVelocity;

layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    mat4 uModel;
    mat4 uPreviousModel;
    vec4 uLightAnimation;
};

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    gl_Position = uViewProj * worldPosition;
    vUv = aUv;
    vVertexData = vec4(aSunlight, aBlockLight, float(aAo), float(aNormal));
    vAnimationData = vec4(float(aLayer), float(aAnimationFrameCount),
                          float(aAnimationFps), float(aAnimated));
    vMaterialTint = uvec2((aTintPacked >> 8u) & 63u, (aTintPacked >> 14u) & 3u);
    vTintUv = (vec2(float((aTintPacked >> 4u) & 15u), float(aTintPacked & 15u)) + 0.5) / 16.0;
    vWorldPosition = worldPosition.xyz;
    vec4 previousClip = uPreviousViewProj * uPreviousModel * vec4(aPosition, 1.0);
    vec2 currentNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.00001);
    vVelocity = (currentNdc - previousNdc) * 0.5;
}
