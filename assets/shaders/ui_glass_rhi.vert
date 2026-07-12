#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 0) out vec2 vBackdropUv;

layout(push_constant) uniform UiGlassPushConstants {
    vec4 screenRect;
    vec4 backdropSizeOpacity;
    vec4 tint;
    vec4 appearance;
} pc;

void main() {
    vec2 position = pc.screenRect.zw + aPos * pc.backdropSizeOpacity.xy;
    vec2 ndc = (position / pc.screenRect.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vBackdropUv = position / max(pc.screenRect.xy, vec2(1.0));
}
