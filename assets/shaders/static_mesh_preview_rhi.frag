#version 450 core

#include "gpu_material_contract.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vTangentSign;
layout(location = 4) in vec3 vWorldPosition;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(binding = 1) uniform sampler2D uMetallicRoughnessTexture;
layout(binding = 2) uniform sampler2D uNormalTexture;
layout(binding = 3) uniform sampler2D uOcclusionTexture;
layout(binding = 4) uniform sampler2D uEmissiveTexture;
layout(binding = 7) uniform sampler2D uSpecularTexture;
layout(binding = 8) uniform sampler2D uSpecularColorTexture;
layout(binding = 9) uniform sampler2D uClearcoatTexture;
layout(binding = 10) uniform sampler2D uClearcoatRoughnessTexture;
layout(binding = 11) uniform sampler2D uClearcoatNormalTexture;
layout(binding = 12) uniform sampler2D uTransmissionTexture;
layout(binding = 13) uniform sampler2D uThicknessTexture;
layout(std140, binding = 5) uniform GpuMaterialParams {
    GpuMaterial uMaterial;
};
layout(std140, binding = 6) uniform StaticMeshFrameParams {
    vec4 uPreviewLight;
};

#include "static_mesh_material.glsl"

void main() {
    StaticMeshMaterialSample sampledMaterial = sampleStaticMeshMaterial(vUv);
    vec4 baseColor = sampledMaterial.baseColor;
    if (uMaterial.modesAndFlags.x == GPU_MATERIAL_ALPHA_MASK &&
        baseColor.a < uMaterial.transmissionVolumeFactors.z) {
        discard;
    }
    vec3 normal = normalize(vNormal);
    vec3 tangent = normalize(vTangent - normal * dot(vTangent, normal));
    vec3 bitangent = normalize(cross(normal, tangent)) * vTangentSign;
    vec3 tangentNormal = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uMaterial.materialFactors.z;
    normal = normalize(mat3(tangent, bitangent, normal) * normalize(tangentNormal));

    float roughness = sampledMaterial.roughness;
    float metalness = sampledMaterial.metalness;
    float occlusion = mix(
        1.0, texture(uOcclusionTexture, vUv).r,
        uMaterial.materialFactors.w);
    vec3 emissive = texture(uEmissiveTexture, vUv).rgb *
                    uMaterial.emissiveFactorAndStrength.rgb *
                    uMaterial.emissiveFactorAndStrength.w;
    vec3 lightDirection = normalize(vec3(-0.45, 0.8, 0.35));
    vec3 viewDirection = normalize(vec3(4.0, 3.0, 6.0) - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specularPower = mix(96.0, 8.0, roughness);
    float specular = pow(max(dot(normal, halfDirection), 0.0), specularPower);
    vec3 f0 = mix(
        sampledMaterial.dielectricF0, baseColor.rgb, metalness);
    vec3 diffuseColor = baseColor.rgb * (1.0 - metalness);
    vec3 color = diffuseColor * (0.12 + diffuse * 0.88) * occlusion;
    color += f0 * specular * (1.0 - roughness * 0.5);
    if (sampledMaterial.clearcoat > 0.0) {
        vec3 clearcoatNormalSample = texture(
            uClearcoatNormalTexture, vUv).xyz * 2.0 - 1.0;
        clearcoatNormalSample.xy *= uMaterial.clearcoatFactors.z;
        vec3 clearcoatNormal = normalize(
            mat3(tangent, bitangent, normalize(vNormal)) *
            normalize(clearcoatNormalSample));
        float clearcoatPower = mix(
            128.0, 12.0, sampledMaterial.clearcoatRoughness);
        float clearcoatSpecular = pow(
            max(dot(clearcoatNormal, halfDirection), 0.0),
            clearcoatPower);
        color += vec3(0.04) * clearcoatSpecular *
            sampledMaterial.clearcoat;
    }
    color += emissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    float previewAlpha = baseColor.a *
        (1.0 - sampledMaterial.transmission * 0.85);
    bool transparentPreview =
                              uMaterial.modesAndFlags.x ==
                                  GPU_MATERIAL_ALPHA_BLEND ||
                              uMaterial.modesAndFlags.x ==
                                  GPU_MATERIAL_ALPHA_TRANSMISSION ||
                              sampledMaterial.transmission > 0.0;
    outColor = transparentPreview
        ? vec4(color * previewAlpha, previewAlpha)
        : vec4(color, baseColor.a);
}
