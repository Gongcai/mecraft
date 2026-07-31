#version 450 core

#include "gpu_material_contract.glsl"
#include "clustered_light_evaluation.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vTangentSign;
layout(location = 4) in vec3 vWorldPosition;

layout(location = 0) out vec4 outRadiance;

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
    uvec4 uMaterialIdentity;
};
layout(set = 1, binding = 0, std140) uniform ProbeCaptureFrameParams {
    mat4 uProbeViewProjection;
    vec4 uProbePosition;
    vec4 uSunDirection;
    vec4 uSunColor;
    vec4 uAmbientColor;
    uvec4 uLightCount;
};
layout(set = 1, binding = 1, std430) readonly buffer ProbeCaptureLightBuffer {
    GpuLight uProbeLights[];
};

#include "static_mesh_material.glsl"

vec3 sampleProbeMappedNormal(sampler2D normalTexture,
                             vec2 uv,
                             float scale,
                             vec3 tangent,
                             vec3 bitangent,
                             vec3 geometricNormal) {
    vec3 tangentNormal = decodeMaterialTangentNormal(
        texture(normalTexture, uv).xyz, scale);
    return normalize(mat3(tangent, bitangent, geometricNormal) *
                     tangentNormal);
}

void main() {
    MaterialSample sampledMaterial = sampleStaticMeshMaterial(vUv);
    if (!materialPassesAlphaTest(
            uMaterial, sampledMaterial.baseColor.a)) {
        discard;
    }

    vec3 geometricNormal = normalize(vNormal);
    vec3 tangent = normalize(
        vTangent - geometricNormal * dot(vTangent, geometricNormal));
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) *
                     vTangentSign;
    vec3 normal = sampleProbeMappedNormal(
        uNormalTexture, vUv, uMaterial.materialFactors.z,
        tangent, bitangent, geometricNormal);
    vec3 viewDirection = normalize(uProbePosition.xyz - vWorldPosition);
    normal = faceforward(normal, -viewDirection, normal);

    vec3 f0 = pbrMaterialSpecularF0(
        sampledMaterial.dielectricF0,
        sampledMaterial.baseColor.rgb,
        sampledMaterial.metalness);
    float f90 = pbrMaterialSpecularF90(
        sampledMaterial.specularF90,
        sampledMaterial.metalness);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float alphaSquared = pbrPerceptualRoughnessToAlphaSquared(
        sampledMaterial.perceptualRoughness);

    vec3 lightDirection = normalize(uSunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float lDotH = max(dot(lightDirection, halfDirection), 0.0);
    vec3 fresnel = pbrFresnelSchlick(lDotH, f0, f90);
    vec3 diffuseWeight = pbrDiffuseWeight(
        fresnel, sampledMaterial.metalness);
    vec3 radiance =
        (diffuseWeight *
             pbrLambertDiffuse(sampledMaterial.baseColor.rgb) * nDotL +
         pbrEvaluateDirectSpecular(
             lDotH, nDotV, nDotL, nDotH, alphaSquared, f0, f90)) *
        uSunColor.rgb;

    vec3 localDiffuse = vec3(0.0);
    vec3 localSpecular = vec3(0.0);
    vec3 probeRelativeSurface = vWorldPosition - uProbePosition.xyz;
    for (uint lightIndex = 0u;
         lightIndex < uLightCount.x;
         ++lightIndex) {
        GpuLightSurfaceContribution contribution = evaluateGpuLight(
            uProbeLights[lightIndex], probeRelativeSurface,
            normal, viewDirection, f0, f90, nDotV, alphaSquared);
        localDiffuse += contribution.diffuse;
        localSpecular += contribution.specular;
    }
    radiance += sampledMaterial.baseColor.rgb *
                    (1.0 - sampledMaterial.metalness) * localDiffuse +
                localSpecular;
    radiance += sampledMaterial.baseColor.rgb * uAmbientColor.rgb *
                sampledMaterial.occlusion;
    radiance += evaluateMaterialEmission(sampledMaterial);

    float outputAlpha = sampledMaterial.baseColor.a *
        (1.0 - sampledMaterial.transmission);
    bool opticalLayer =
        uMaterial.modesAndFlags.x == GPU_MATERIAL_ALPHA_BLEND ||
        uMaterial.modesAndFlags.x == GPU_MATERIAL_ALPHA_TRANSMISSION ||
        sampledMaterial.transmission > 0.0;
    outRadiance = opticalLayer
        ? vec4(radiance * outputAlpha, outputAlpha)
        : vec4(radiance, 1.0);
}
