#ifndef MECRAFT_GPU_MATERIAL_CONTRACT_GLSL
#define MECRAFT_GPU_MATERIAL_CONTRACT_GLSL

const uint GPU_MATERIAL_CONTRACT_VERSION = 1u;

const uint GPU_MATERIAL_TEXTURE_BASE_COLOR = 0u;
const uint GPU_MATERIAL_TEXTURE_METALLIC_ROUGHNESS_OR_SPECULAR_GLOSSINESS = 1u;
const uint GPU_MATERIAL_TEXTURE_NORMAL = 2u;
const uint GPU_MATERIAL_TEXTURE_OCCLUSION = 3u;
const uint GPU_MATERIAL_TEXTURE_EMISSIVE = 4u;
const uint GPU_MATERIAL_TEXTURE_SPECULAR_WEIGHT = 5u;
const uint GPU_MATERIAL_TEXTURE_SPECULAR_COLOR = 6u;
const uint GPU_MATERIAL_TEXTURE_CLEARCOAT = 7u;
const uint GPU_MATERIAL_TEXTURE_CLEARCOAT_ROUGHNESS = 8u;
const uint GPU_MATERIAL_TEXTURE_CLEARCOAT_NORMAL = 9u;
const uint GPU_MATERIAL_TEXTURE_TRANSMISSION = 10u;
const uint GPU_MATERIAL_TEXTURE_THICKNESS = 11u;

const uint GPU_MATERIAL_ALPHA_OPAQUE = 0u;
const uint GPU_MATERIAL_ALPHA_MASK = 1u;
const uint GPU_MATERIAL_ALPHA_BLEND = 2u;
const uint GPU_MATERIAL_ALPHA_TRANSMISSION = 3u;
const uint GPU_MATERIAL_ALPHA_ADDITIVE = 4u;

const uint GPU_MATERIAL_WORKFLOW_METALLIC_ROUGHNESS = 0u;
const uint GPU_MATERIAL_WORKFLOW_SPECULAR_GLOSSINESS = 1u;
const uint GPU_MATERIAL_WORKFLOW_LAB_PBR = 2u;

const uint GPU_MATERIAL_FLAG_DOUBLE_SIDED = 1u << 0u;
const uint GPU_MATERIAL_FLAG_SPECULAR = 1u << 1u;
const uint GPU_MATERIAL_FLAG_IOR = 1u << 2u;
const uint GPU_MATERIAL_FLAG_CLEARCOAT = 1u << 3u;
const uint GPU_MATERIAL_FLAG_TRANSMISSION = 1u << 4u;
const uint GPU_MATERIAL_FLAG_VOLUME = 1u << 5u;
const uint GPU_MATERIAL_FLAG_INFINITE_ATTENUATION_DISTANCE = 1u << 6u;

// The field order and array sizes mirror renderer::contracts::GpuMaterial.
// std140 and the C++ alignas(16) record both produce an exact 256-byte block.
struct GpuMaterial {
    vec4 baseColorFactor;
    vec4 emissiveFactorAndStrength;
    vec4 materialFactors;
    vec4 specularGlossinessFactors;
    vec4 dielectricSpecularFactors;
    vec4 clearcoatFactors;
    vec4 transmissionVolumeFactors;
    vec4 attenuationColorAndDistance;
    vec4 gameplaySurfaceFactors;
    uvec4 textureIndices[3];
    uvec4 samplerIndices[3];
    uvec4 modesAndFlags;
};

bool gpuMaterialHas(in GpuMaterial material, uint flag) {
    return (material.modesAndFlags.z & flag) != 0u;
}

#endif // MECRAFT_GPU_MATERIAL_CONTRACT_GLSL
