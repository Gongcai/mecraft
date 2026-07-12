#version 450 core

layout(location = 0) in vec2 aPos;

layout(push_constant) uniform CrosshairPushConstants {
    vec4 screenAndOffset;
    vec4 color;
} pc;

void main() {
    vec2 worldPos = aPos + pc.screenAndOffset.zw;
    vec2 ndc = (worldPos / pc.screenAndOffset.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
