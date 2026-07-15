#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(set = 0, binding = 0) uniform sampler2D uUiTexture;

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uUiTexture, rhiScreenUvToTextureUv(vScreenUv));
}
