#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform RhiPushConstants {
    vec4 screenSize;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    vec2 ndc = (aPosition / pc.screenSize.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
}
