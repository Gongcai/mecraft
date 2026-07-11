#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;

layout(location = 0) out vec2 vUv;

layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uPrecipColorStrength;
    vec4 uAlphaScreenDepth;
    ivec4 uControls;
};

void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
    vUv = aUv;
}
