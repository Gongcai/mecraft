#version 450 core

#include "gpu_material_contract.glsl"

layout(location = 0) in vec2 vUv;
layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(std140, binding = 5) uniform GpuMaterialParams {
    GpuMaterial uMaterial;
};

void main() {
    float alpha = texture(uBaseColorTexture, vUv).a *
                  uMaterial.baseColorFactor.a;
    if (uMaterial.modesAndFlags.x == GPU_MATERIAL_ALPHA_MASK &&
        alpha < uMaterial.transmissionVolumeFactors.z) {
        discard;
    }
}
