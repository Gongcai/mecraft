#version 450 core

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vTangentSign;
layout(location = 4) in vec3 vWorldPosition;

layout(location = 0) out vec4 outColor;
layout(location = 1) out float outReactiveMask;
layout(location = 2) out float outTransparencyMask;

layout(binding = 0) uniform sampler2D uBaseColorTexture;
layout(binding = 1) uniform sampler2D uMetallicRoughnessTexture;
layout(binding = 2) uniform sampler2D uNormalTexture;
layout(binding = 3) uniform sampler2D uOcclusionTexture;
layout(binding = 4) uniform sampler2D uEmissiveTexture;
layout(std140, binding = 5) uniform StaticMeshMaterialParams {
    vec4 uBaseColorFactor;
    vec4 uEmissiveAlphaCutoff;
    vec4 uMaterialFactors;
    vec4 uWorkflowFactors;
    ivec4 uMaterialFlags;
};
layout(std140, binding = 6) uniform StaticMeshFrameParams {
    vec4 uVoxelLight;
    mat4 uViewProj;
    mat4 uPreviousViewProj;
    vec4 uCameraPosition;
    vec4 uSunDirection;
    vec4 uSunColor;
    vec4 uAmbientColor;
    vec4 uFogColor;
    vec4 uFogParams;
};

#include "static_mesh_material.glsl"

const float PI = 3.14159265358979323846;

float distributionGgx(float nDotH, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 1e-6);
}

float geometrySchlickGgx(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    return nDotV / max(nDotV * (1.0 - k) + k, 1e-6);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

float fogAmount(float distanceToCamera) {
    int mode = int(round(uFogParams.w)) - 1;
    if (mode < 0) {
        return 0.0;
    }
    if (mode == 0) {
        return clamp((distanceToCamera - uFogParams.x) /
                     max(uFogParams.y - uFogParams.x, 1e-4), 0.0, 1.0);
    }
    float densityDistance = uFogParams.z * distanceToCamera;
    float transmittance = mode == 1
        ? exp(-densityDistance)
        : exp(-densityDistance * densityDistance);
    return clamp(1.0 - transmittance, 0.0, 1.0);
}

void main() {
    StaticMeshMaterialSample sampledMaterial = sampleStaticMeshMaterial(vUv);
    if (sampledMaterial.baseColor.a <= 0.0) {
        discard;
    }

    vec3 geometricNormal = normalize(vNormal);
    vec3 tangent = normalize(
        vTangent - geometricNormal * dot(vTangent, geometricNormal));
    vec3 bitangent =
        normalize(cross(geometricNormal, tangent)) * vTangentSign;
    vec3 tangentNormal = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uMaterialFactors.z;
    vec3 normal = normalize(
        mat3(tangent, bitangent, geometricNormal) * normalize(tangentNormal));

    vec3 viewDirection = normalize(uCameraPosition.xyz - vWorldPosition);
    vec3 lightDirection = normalize(uSunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float hDotV = max(dot(halfDirection, viewDirection), 0.0);

    vec3 f0 = mix(
        vec3(0.04), sampledMaterial.baseColor.rgb,
        sampledMaterial.metalness);
    vec3 fresnel = fresnelSchlick(hDotV, f0);
    float distribution = distributionGgx(nDotH, sampledMaterial.roughness);
    float geometry = geometrySchlickGgx(nDotV, sampledMaterial.roughness) *
                     geometrySchlickGgx(nDotL, sampledMaterial.roughness);
    vec3 specular = distribution * geometry * fresnel /
                    max(4.0 * nDotV * nDotL, 1e-5);
    vec3 diffuseWeight =
        (1.0 - fresnel) * (1.0 - sampledMaterial.metalness);
    float occlusion = mix(
        1.0, texture(uOcclusionTexture, vUv).r, uMaterialFactors.w);
    vec3 emissive =
        texture(uEmissiveTexture, vUv).rgb * uEmissiveAlphaCutoff.rgb;
    vec3 color =
        (diffuseWeight * sampledMaterial.baseColor.rgb / PI + specular) *
            uSunColor.rgb * nDotL +
        sampledMaterial.baseColor.rgb * uAmbientColor.rgb * occlusion +
        emissive;
    float distanceToCamera = length(uCameraPosition.xyz - vWorldPosition);
    color = mix(color, uFogColor.rgb, fogAmount(distanceToCamera));

    outColor = vec4(color, sampledMaterial.baseColor.a);
    outReactiveMask = sampledMaterial.baseColor.a;
    outTransparencyMask = sampledMaterial.baseColor.a;
}
