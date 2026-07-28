#version 450 core

#include "../../assets/shaders/material_decode.glsl"
#include "../../assets/shaders/pbr_brdf.glsl"

layout(location = 0) out vec4 FragColor;

void main() {
    GpuMaterial gpuMaterial;
    gpuMaterial.baseColorFactor = vec4(0.8, 0.7, 0.6, 1.0);
    gpuMaterial.emissiveFactorAndStrength = vec4(0.1, 0.2, 0.3, 2.0);
    gpuMaterial.materialFactors = vec4(0.5, 0.4, 1.0, 0.75);
    gpuMaterial.specularGlossinessFactors = vec4(1.0);
    gpuMaterial.dielectricSpecularFactors = vec4(1.0);
    gpuMaterial.clearcoatFactors = vec4(0.0, 0.0, 1.0, 0.0);
    gpuMaterial.transmissionVolumeFactors = vec4(0.0, 0.0, 0.5, 1.5);
    gpuMaterial.attenuationColorAndDistance = vec4(1.0, 1.0, 1.0, 0.0);
    gpuMaterial.gameplaySurfaceFactors = vec4(0.0);
    gpuMaterial.modesAndFlags = uvec4(
        GPU_MATERIAL_ALPHA_OPAQUE,
        GPU_MATERIAL_WORKFLOW_METALLIC_ROUGHNESS,
        0u,
        GPU_MATERIAL_CONTRACT_VERSION);

    MaterialTextureSamples textures = defaultMaterialTextureSamples();
    textures.properties = vec4(1.0, 0.5, 0.25, 1.0);
    MaterialSample material = decodeGltfMaterial(gpuMaterial, textures);
    vec3 f0 = pbrMaterialSpecularF0(
        material.dielectricF0, material.baseColor.rgb, material.metalness);
    float f90 = pbrMaterialSpecularF90(
        material.specularF90, material.metalness);
    float alphaSquared = pbrPerceptualRoughnessToAlphaSquared(
        material.perceptualRoughness);
    vec3 specular = pbrEvaluateDirectSpecular(
        0.8, 0.7, 0.6, 0.75, alphaSquared, f0, f90);
    FragColor = vec4(
        evaluateMaterialEmission(material) + specular,
        materialPassesAlphaTest(gpuMaterial, material.baseColor.a)
            ? 1.0
            : 0.0);
}
