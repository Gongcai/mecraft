#version 450 core

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform UiImagePushConstants {
    vec4 screenRect;
    vec4 extent;
    vec4 uvRect;
    vec4 tint;
} pc;

void main() {
    vec4 sampled = texture(uTexture, vUv);
    if (sampled.a < 0.1) {
        discard;
    }
    FragColor = sampled * pc.tint;
}
