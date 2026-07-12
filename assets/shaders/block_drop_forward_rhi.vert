#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in int aNormal;
layout(location = 5) in uint aAo;
layout(location = 6) in uint aLayer;
layout(location = 7) in uint aAnimationFrameCount;
layout(location = 8) in uint aAnimationFps;
layout(location = 9) in uint aAnimated;
layout(location = 10) in uint aTintPacked;
layout(location = 0) out vec2 vUv;
layout(location = 1) out float vAo;
layout(location = 2) out float vLayer;
layout(location = 3) out float vAnimationFrameCount;
layout(location = 4) out float vAnimationFps;
layout(location = 5) out float vAnimated;
layout(location = 6) flat out uint vTintKind;
layout(location = 7) out vec2 vTintUv;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLightingAnimation;
};
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vUv = aUv;
    vAo = float(aAo);
    vLayer = float(aLayer);
    vAnimationFrameCount = float(aAnimationFrameCount);
    vAnimationFps = float(aAnimationFps);
    vAnimated = float(aAnimated);
    vTintKind = (aTintPacked >> 14u) & 3u;
    vTintUv = (vec2(float((aTintPacked >> 4u) & 15u), float(aTintPacked & 15u)) + 0.5) / 16.0;
}
