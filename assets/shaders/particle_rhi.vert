#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aLayer;
layout(location = 3) in float aAlpha;
layout(location = 4) in float aBiomeTintFactor;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vLayer;
layout(location = 2) out float vAlpha;
layout(location = 3) out float vBiomeTintFactor;

layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    vec4 uBiomeTint;
    vec4 uScreenParams;
};

void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
    vUv = aUv;
    vLayer = aLayer;
    vAlpha = aAlpha;
    vBiomeTintFactor = aBiomeTintFactor;
}
