#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in uint aColor;

layout(push_constant) uniform DashboardPushConstants {
    vec2 scale;
    vec2 translate;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = vec4(aPosition * pc.scale + pc.translate, 0.0, 1.0);
    vUv = aUv;
    vColor = unpackUnorm4x8(aColor);
}
