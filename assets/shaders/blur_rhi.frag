#version 450 core

#include "rhi_screen_coordinates.glsl"

layout(binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform RhiPushConstants {
    vec4 uDirection;
};

layout(location = 0) in vec2 vScreenUv;
layout(location = 0) out vec4 FragColor;

void main() {
    float weights[7] = float[](0.1964, 0.1742, 0.1222, 0.0678, 0.0298, 0.0104, 0.0029);

    vec2 direction = uDirection.xy;
    vec2 textureUv = rhiScreenUvToTextureUv(vScreenUv);
    vec3 result = texture(uTexture, textureUv).rgb * weights[0];
    for (int i = 1; i < 7; ++i) {
        vec2 offset = direction * float(i) * 2.0;
        result += texture(uTexture, textureUv + offset).rgb * weights[i];
        result += texture(uTexture, textureUv - offset).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
