#version 450 core

#include "material_decode.glsl"

layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(std140, binding = 5) uniform GpuMaterialParams {
    GpuMaterial uMaterial;
    uvec4 uMaterialIdentity;
};

void main() {
    float alpha = texture(uBaseColorTexture, vUv).a *
                  uMaterial.baseColorFactor.a;
    if (!materialPassesAlphaTest(uMaterial, alpha)) {
        discard;
    }
}
