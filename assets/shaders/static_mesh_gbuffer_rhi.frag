#version 450 core

#include "gbuffer_contract.glsl"

layout(location = 0) out vec4 gAlbedoMaterial;
layout(location = 1) out vec4 gNormalAo;
layout(location = 2) out vec4 gVoxelLight;
layout(location = 3) out vec4 gMaterial;
layout(location = 4) out vec4 gMaterialAux;
layout(location = 5) out vec2 gVelocity;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vTangentSign;
layout(location = 4) in vec2 vVelocity;

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
layout(std140, binding = 5) uniform StaticMeshMaterialParams {
    vec4 uBaseColorFactor;
    vec4 uEmissiveAlphaCutoff;
    vec4 uMaterialFactors;
    vec4 uWorkflowFactors;
    vec4 uSpecularFactors;
    vec4 uClearcoatFactors;
    vec4 uTransmissionVolumeFactors;
    vec4 uAttenuationColorDistance;
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

void main() {
    StaticMeshMaterialSample sampledMaterial = sampleStaticMeshMaterial(vUv);
    vec4 baseColor = sampledMaterial.baseColor;
    if (uMaterialFlags.x != 0 && baseColor.a < uEmissiveAlphaCutoff.w) {
        discard;
    }

    vec3 geometricNormal = normalize(vNormal);
    vec3 tangent = normalize(vTangent - geometricNormal * dot(vTangent, geometricNormal));
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * vTangentSign;
    vec3 tangentNormal = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uMaterialFactors.z;
    vec3 worldNormal = normalize(mat3(tangent, bitangent, geometricNormal) * normalize(tangentNormal));

    float roughness = sampledMaterial.roughness;
    float metalness = sampledMaterial.metalness;
    float occlusionSample = texture(uOcclusionTexture, vUv).r;
    float occlusion = mix(1.0, occlusionSample, uMaterialFactors.w);
    vec3 emissive = texture(uEmissiveTexture, vUv).rgb * uEmissiveAlphaCutoff.rgb;
    float emissiveStrength = max(max(emissive.r, emissive.g), emissive.b);

    SurfaceMaterialAux aux = defaultSurfaceMaterialAux();
    aux.materialKind = float(MATERIAL_STATIC_MESH);
    aux.porosity = sampledMaterial.specularF90;
    aux.metalness = metalness;

    gAlbedoMaterial = vec4(baseColor.rgb, emissiveStrength);
    gNormalAo = packGBufferNormalAo(worldNormal, occlusion);
    gVoxelLight = vec4(clamp(uVoxelLight.xy, 0.0, 1.0), 0.0, 1.0);
    gMaterial = vec4(roughness, sampledMaterial.dielectricF0);
    gMaterialAux = packGBufferMaterialAux(aux);
    gVelocity = vVelocity;
}
