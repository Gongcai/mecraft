#version 450 core
layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uTexture;
void main() {
    if (texture(uTexture, vUv).a < 0.1) discard;
}
