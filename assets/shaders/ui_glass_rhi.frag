#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(set = 0, binding = 0) uniform sampler2D uBackdrop;
layout(location = 0) in vec2 vBackdropUv;

layout(push_constant) uniform UiGlassPushConstants {
    vec4 screenRect;
    vec4 backdropSizeOpacity;
    vec4 tint;
    vec4 appearance;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 screenUv = rhiScreenUvToClipUv(vBackdropUv);
    vec3 backdrop = texture(uBackdrop, rhiScreenUvToTextureUv(screenUv)).rgb;
    float luma = dot(backdrop, vec3(0.299, 0.587, 0.114));
    vec3 softened = mix(vec3(luma), backdrop, pc.appearance.x) * pc.appearance.y;
    vec3 tinted = mix(softened, pc.tint.rgb, clamp(pc.tint.a, 0.0, 1.0));
    outColor = vec4(tinted, clamp(pc.backdropSizeOpacity.w, 0.0, 1.0));
}
