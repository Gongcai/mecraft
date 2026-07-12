#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 0) out vec2 vLocal;

layout(push_constant) uniform UiCapsulePushConstants {
    vec4 screenRect;
    vec4 rectRadius;
    vec4 color;
} pc;

void main() {
    vec2 position = pc.screenRect.zw + aPos * pc.rectRadius.xy;
    vec2 ndc = (position / pc.screenRect.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vLocal = aPos * pc.rectRadius.xy;
}
