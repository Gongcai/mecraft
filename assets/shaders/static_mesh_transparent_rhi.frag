#version 450 core

#include "rhi_screen_coordinates.glsl"

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
layout(set = 1, binding = 0) uniform sampler2D uSceneColorTexture;
layout(set = 1, binding = 1) uniform sampler2D uOpaqueDepthTexture;
layout(set = 1, binding = 2) uniform sampler2D uSkyCaptureTexture;
layout(push_constant) uniform StaticMeshTransparentPushConstants {
    mat4 uModel;
    vec4 uReflectionParams;
};
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
#include "render_contract.glsl"

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

vec2 projectTransparentReflection(
    vec3 worldPosition,
    out float projectedDepth) {
    vec4 clip = uViewProj * vec4(worldPosition, 1.0);
    if (clip.w <= 0.00001) {
        projectedDepth = -1.0;
        return vec2(-1.0);
    }
    vec3 ndc = clip.xyz / clip.w;
    projectedDepth = ndc.z * 0.5 + 0.5;
    return rhiScreenUvToClipUv(ndc.xy * 0.5 + 0.5);
}

bool traceTransparentScreenReflection(
    vec3 worldPosition,
    vec3 reflectedDirection,
    vec3 normal,
    float roughness,
    out vec3 hitColor,
    out float hitConfidence) {
    hitColor = vec3(0.0);
    hitConfidence = 0.0;
    float maxDistance = mix(48.0, 12.0, roughness);
    int stepCount = int(mix(28.0, 12.0, roughness));
    float stepLength = maxDistance / float(stepCount);
    vec3 rayOrigin = worldPosition + normal * 0.025 +
                     reflectedDirection * 0.1;

    for (int stepIndex = 1; stepIndex <= 28; ++stepIndex) {
        if (stepIndex > stepCount) {
            break;
        }
        float progress = float(stepIndex) / float(stepCount);
        vec3 samplePosition = rayOrigin + reflectedDirection *
            (float(stepIndex) * stepLength);
        float rayDepth;
        vec2 screenUv = projectTransparentReflection(
            samplePosition, rayDepth);
        if (screenUv.x <= 0.001 || screenUv.x >= 0.999 ||
            screenUv.y <= 0.001 || screenUv.y >= 0.999 ||
            rayDepth <= 0.0 || rayDepth >= 1.0) {
            return false;
        }

        vec2 textureUv = rhiScreenUvToTextureUv(screenUv);
        float sceneDepth = texture(uOpaqueDepthTexture, textureUv).r;
        if (sceneDepth >= 0.9999) {
            continue;
        }
        float thickness = mix(0.00025, 0.005, progress) *
                          mix(1.0, 1.8, roughness);
        if (rayDepth >= sceneDepth &&
            rayDepth - sceneDepth < thickness) {
            vec2 edgeDistance = min(screenUv, 1.0 - screenUv);
            float edgeConfidence = smoothstep(
                0.0, 0.08, min(edgeDistance.x, edgeDistance.y));
            hitConfidence = edgeConfidence *
                mix(1.0, 0.35, roughness * roughness);
            hitColor = texture(uSceneColorTexture, textureUv).rgb;
            return true;
        }
    }
    return false;
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
    normal = faceforward(normal, -viewDirection, normal);
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

    vec3 reflectedDirection = reflect(-viewDirection, normal);
    vec3 environmentReflection = sampleSkyRadianceCloudy(
        uSkyCaptureTexture, reflectedDirection);
    vec3 screenReflection;
    float screenReflectionConfidence;
    traceTransparentScreenReflection(
        vWorldPosition, reflectedDirection, normal,
        sampledMaterial.roughness,
        screenReflection, screenReflectionConfidence);
    vec3 reflectionSource = mix(
        environmentReflection, screenReflection,
        screenReflectionConfidence);
    vec3 viewFresnel = fresnelSchlick(nDotV, f0);
    float roughnessEnergy = mix(
        1.0, 0.25,
        sampledMaterial.roughness * sampledMaterial.roughness);
    vec3 reflection = reflectionSource * viewFresnel *
        roughnessEnergy * clamp(uReflectionParams.x, 0.0, 1.0);

    outColor = vec4(
        color * sampledMaterial.baseColor.a + reflection,
        sampledMaterial.baseColor.a);
    outReactiveMask = sampledMaterial.baseColor.a;
    outTransparencyMask = sampledMaterial.baseColor.a;
}
