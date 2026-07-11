#version 450 core
layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uAtlas;
void main() {
    if (texture(uAtlas, vUv).a < 0.1) discard;
}
