#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 0) out vec2 vUv;

layout(push_constant) uniform UiImagePushConstants {
    vec4 screenRect;
    vec4 extent;
    vec4 uvRect;
    vec4 tint;
} pc;

void main() {
    vec2 position = pc.screenRect.zw + aPos * pc.extent.xy;
    vec2 ndc = (position / pc.screenRect.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUv = mix(pc.uvRect.xy, pc.uvRect.zw, aPos);
}
