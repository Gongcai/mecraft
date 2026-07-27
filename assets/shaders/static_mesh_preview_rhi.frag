#version 450 core

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
layout(std140, binding = 5) uniform StaticMeshMaterialParams {
    vec4 uBaseColorFactor;
    vec4 uEmissiveAlphaCutoff;
    vec4 uMaterialFactors;
    ivec4 uMaterialFlags;
};
layout(std140, binding = 6) uniform StaticMeshFrameParams {
    vec4 uPreviewLight;
};

void main() {
    vec4 baseColor = texture(uBaseColorTexture, vUv) * uBaseColorFactor;
    if (uMaterialFlags.x != 0 && baseColor.a < uEmissiveAlphaCutoff.w) {
        discard;
    }
    vec3 normal = normalize(vNormal);
    vec3 tangent = normalize(vTangent - normal * dot(vTangent, normal));
    vec3 bitangent = normalize(cross(normal, tangent)) * vTangentSign;
    vec3 tangentNormal = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uMaterialFactors.z;
    normal = normalize(mat3(tangent, bitangent, normal) * normalize(tangentNormal));

    vec4 metallicRoughness = texture(uMetallicRoughnessTexture, vUv);
    float roughness = clamp(metallicRoughness.g * uMaterialFactors.y, 0.04, 1.0);
    float metalness = clamp(metallicRoughness.b * uMaterialFactors.x, 0.0, 1.0);
    float occlusion = mix(1.0, texture(uOcclusionTexture, vUv).r, uMaterialFactors.w);
    vec3 emissive = texture(uEmissiveTexture, vUv).rgb * uEmissiveAlphaCutoff.rgb;
    vec3 lightDirection = normalize(vec3(-0.45, 0.8, 0.35));
    vec3 viewDirection = normalize(vec3(4.0, 3.0, 6.0) - vWorldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specularPower = mix(96.0, 8.0, roughness);
    float specular = pow(max(dot(normal, halfDirection), 0.0), specularPower);
    vec3 dielectric = vec3(0.04);
    vec3 f0 = mix(dielectric, baseColor.rgb, metalness);
    vec3 diffuseColor = baseColor.rgb * (1.0 - metalness);
    vec3 color = diffuseColor * (0.12 + diffuse * 0.88) * occlusion;
    color += f0 * specular * (1.0 - roughness * 0.5);
    color += emissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, baseColor.a);
}
