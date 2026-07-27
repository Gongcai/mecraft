#version 450 core

layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(std140, binding = 5) uniform StaticMeshMaterialParams {
    vec4 uBaseColorFactor;
    vec4 uEmissiveAlphaCutoff;
    vec4 uMaterialFactors;
    ivec4 uMaterialFlags;
};

void main() {
    float alpha = texture(uBaseColorTexture, vUv).a * uBaseColorFactor.a;
    if (uMaterialFlags.x != 0 && alpha < uEmissiveAlphaCutoff.w) {
        discard;
    }
}
