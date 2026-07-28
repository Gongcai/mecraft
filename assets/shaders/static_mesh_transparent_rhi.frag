#version 450 core

#include "gpu_material_contract.glsl"
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
layout(binding = 7) uniform sampler2D uSpecularTexture;
layout(binding = 8) uniform sampler2D uSpecularColorTexture;
layout(binding = 9) uniform sampler2D uClearcoatTexture;
layout(binding = 10) uniform sampler2D uClearcoatRoughnessTexture;
layout(binding = 11) uniform sampler2D uClearcoatNormalTexture;
layout(binding = 12) uniform sampler2D uTransmissionTexture;
layout(binding = 13) uniform sampler2D uThicknessTexture;
layout(set = 1, binding = 0) uniform sampler2D uSceneColorTexture;
layout(set = 1, binding = 1) uniform sampler2D uOpaqueDepthTexture;
layout(set = 1, binding = 2) uniform sampler2D uSkyCaptureTexture;
layout(push_constant) uniform StaticMeshTransparentPushConstants {
    mat4 uModel;
    vec4 uReflectionParams;
};
layout(std140, binding = 5) uniform GpuMaterialParams {
    GpuMaterial uMaterial;
    uvec4 uMaterialIdentity;
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

vec3 sampleMappedNormal(sampler2D normalTexture,
                        vec2 uv,
                        float scale,
                        vec3 tangent,
                        vec3 bitangent,
                        vec3 geometricNormal) {
    vec3 tangentNormal = decodeMaterialTangentNormal(
        texture(normalTexture, uv).xyz, scale);
    return normalize(
        mat3(tangent, bitangent, geometricNormal) *
        tangentNormal);
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
            hitColor = textureLod(
                uSceneColorTexture, textureUv, 0.0).rgb;
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
    MaterialSample sampledMaterial = sampleStaticMeshMaterial(vUv);
    if (sampledMaterial.baseColor.a <= 0.0) {
        discard;
    }

    vec3 geometricNormal = normalize(vNormal);
    vec3 tangent = normalize(
        vTangent - geometricNormal * dot(vTangent, geometricNormal));
    vec3 bitangent =
        normalize(cross(geometricNormal, tangent)) * vTangentSign;
    vec3 normal = sampleMappedNormal(
        uNormalTexture, vUv, uMaterial.materialFactors.z,
        tangent, bitangent, geometricNormal);
    vec3 clearcoatNormal = normal;
    if (sampledMaterial.clearcoat > 0.0) {
        clearcoatNormal = sampleMappedNormal(
            uClearcoatNormalTexture, vUv,
            uMaterial.clearcoatFactors.z,
            tangent, bitangent, geometricNormal);
    }

    vec3 viewDirection = normalize(uCameraPosition.xyz - vWorldPosition);
    normal = faceforward(normal, -viewDirection, normal);
    vec3 lightDirection = normalize(uSunDirection.xyz);
    vec3 halfDirection = normalize(viewDirection + lightDirection);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float nDotH = max(dot(normal, halfDirection), 0.0);
    float hDotV = max(dot(halfDirection, viewDirection), 0.0);

    vec3 f0 = pbrMaterialSpecularF0(
        sampledMaterial.dielectricF0,
        sampledMaterial.baseColor.rgb,
        sampledMaterial.metalness);
    float f90 = pbrMaterialSpecularF90(
        sampledMaterial.specularF90,
        sampledMaterial.metalness);
    vec3 fresnel = pbrFresnelSchlick(hDotV, f0, f90);
    float alphaSquared = pbrPerceptualRoughnessToAlphaSquared(
        sampledMaterial.perceptualRoughness);
    vec3 specular = pbrEvaluateDirectSpecular(
        hDotV, nDotV, nDotL, nDotH, alphaSquared, f0, f90);
    vec3 diffuse = pbrDiffuseWeight(
        fresnel, sampledMaterial.metalness) *
        pbrLambertDiffuse(sampledMaterial.baseColor.rgb) * nDotL;
    vec3 color = (diffuse + specular) * uSunColor.rgb +
        sampledMaterial.baseColor.rgb * uAmbientColor.rgb *
            sampledMaterial.occlusion +
        evaluateMaterialEmission(sampledMaterial);

    vec3 clearcoatDirect = vec3(0.0);
    if (sampledMaterial.clearcoat > 0.0) {
        vec3 clearcoatHalf = normalize(viewDirection + lightDirection);
        float clearcoatNdotL = max(
            dot(clearcoatNormal, lightDirection), 0.0);
        float clearcoatNdotV = max(
            dot(clearcoatNormal, viewDirection), 0.0);
        float clearcoatNdotH = max(
            dot(clearcoatNormal, clearcoatHalf), 0.0);
        float clearcoatHdotV = max(
            dot(clearcoatHalf, viewDirection), 0.0);
        float clearcoatAlphaSquared =
            pbrPerceptualRoughnessToAlphaSquared(
                sampledMaterial.clearcoatPerceptualRoughness);
        vec3 clearcoatFresnel = pbrFresnelSchlick(
            clearcoatHdotV, vec3(0.04), 1.0);
        clearcoatDirect = sampledMaterial.clearcoat *
            pbrEvaluateDirectSpecular(
                clearcoatHdotV,
                clearcoatNdotV,
                clearcoatNdotL,
                clearcoatNdotH,
                clearcoatAlphaSquared,
                vec3(0.04),
                1.0) *
            uSunColor.rgb;
        float layerAttenuation = 1.0 - sampledMaterial.clearcoat *
            max(max(clearcoatFresnel.r, clearcoatFresnel.g),
                clearcoatFresnel.b);
        color = color * layerAttenuation + clearcoatDirect;
    }
    float distanceToCamera = length(uCameraPosition.xyz - vWorldPosition);
    color = mix(color, uFogColor.rgb, fogAmount(distanceToCamera));

    vec3 reflectedDirection = reflect(-viewDirection, normal);
    vec3 environmentReflection = sampleSkyRadianceCloudy(
        uSkyCaptureTexture, reflectedDirection);
    vec3 screenReflection;
    float screenReflectionConfidence;
    traceTransparentScreenReflection(
        vWorldPosition, reflectedDirection, normal,
        sampledMaterial.perceptualRoughness,
        screenReflection, screenReflectionConfidence);
    vec3 reflectionSource = mix(
        environmentReflection, screenReflection,
        screenReflectionConfidence);
    vec3 viewFresnel = pbrFresnelSchlick(nDotV, f0, f90);
    float roughnessEnergy = mix(
        1.0, 0.25,
        sampledMaterial.perceptualRoughness *
            sampledMaterial.perceptualRoughness);
    vec3 reflection = reflectionSource * viewFresnel *
        roughnessEnergy * clamp(uReflectionParams.x, 0.0, 1.0);

    vec3 clearcoatReflection = vec3(0.0);
    if (sampledMaterial.clearcoat > 0.0) {
        vec3 clearcoatReflectedDirection = reflect(
            -viewDirection, clearcoatNormal);
        vec3 clearcoatEnvironment = sampleSkyRadianceCloudy(
            uSkyCaptureTexture, clearcoatReflectedDirection);
        vec3 clearcoatScreen;
        float clearcoatScreenConfidence;
        traceTransparentScreenReflection(
            vWorldPosition, clearcoatReflectedDirection, clearcoatNormal,
            sampledMaterial.clearcoatPerceptualRoughness,
            clearcoatScreen, clearcoatScreenConfidence);
        vec3 clearcoatSource = mix(
            clearcoatEnvironment, clearcoatScreen,
            clearcoatScreenConfidence);
        float clearcoatNdotV = max(
            dot(clearcoatNormal, viewDirection), 0.0);
        vec3 clearcoatFresnel = pbrFresnelSchlick(
            clearcoatNdotV, vec3(0.04), 1.0);
        float clearcoatEnergy = mix(
            1.0, 0.25,
            sampledMaterial.clearcoatPerceptualRoughness *
                sampledMaterial.clearcoatPerceptualRoughness);
        clearcoatReflection = clearcoatSource * clearcoatFresnel *
            sampledMaterial.clearcoat * clearcoatEnergy *
            clamp(uReflectionParams.x, 0.0, 1.0);
        reflection *= 1.0 - sampledMaterial.clearcoat *
            max(max(clearcoatFresnel.r, clearcoatFresnel.g),
                clearcoatFresnel.b);
        reflection += clearcoatReflection;
    }

    if (sampledMaterial.transmission > 0.0) {
        vec3 refractedDirection = refract(
            -viewDirection, normal, 1.0 / sampledMaterial.ior);
        vec3 modelScale = vec3(
            length(uModel[0].xyz),
            length(uModel[1].xyz),
            length(uModel[2].xyz));
        vec3 transmissionRay = normalize(refractedDirection) *
            sampledMaterial.thickness * modelScale;
        float refractedDepth;
        vec2 refractedScreenUv = projectTransparentReflection(
            vWorldPosition + transmissionRay,
            refractedDepth);
        vec2 transmissionScreenUv = clamp(
            refractedScreenUv, vec2(0.001), vec2(0.999));
        // Match the glTF sample-viewer mapping: IOR controls how strongly
        // microsurface roughness spreads transmitted radiance across the
        // opaque-scene color pyramid.
        float iorRoughness = sampledMaterial.perceptualRoughness *
            clamp(sampledMaterial.ior * 2.0 - 2.0, 0.0, 1.0);
        float transmissionLod = iorRoughness *
            float(textureQueryLevels(uSceneColorTexture) - 1);
        vec3 transmittedScene = textureLod(
            uSceneColorTexture,
            rhiScreenUvToTextureUv(transmissionScreenUv),
            transmissionLod).rgb;
        vec3 volumeAttenuation = vec3(1.0);
        if (sampledMaterial.thickness > 0.0 &&
            sampledMaterial.attenuationEnabled) {
            volumeAttenuation = pow(
                max(sampledMaterial.attenuationColor, vec3(1e-5)),
                vec3(length(transmissionRay) /
                     sampledMaterial.attenuationDistance));
        }
        vec3 transmittedColor = transmittedScene * volumeAttenuation *
            sampledMaterial.baseColor.rgb * (vec3(1.0) - viewFresnel);
        color = mix(color, transmittedColor, sampledMaterial.transmission);
    }

    bool clearcoatOverlay =
        uMaterial.modesAndFlags.x != GPU_MATERIAL_ALPHA_BLEND &&
        sampledMaterial.transmission <= 0.0 &&
        sampledMaterial.clearcoat > 0.0;
    if (clearcoatOverlay) {
        outColor = vec4(clearcoatDirect + clearcoatReflection, 0.0);
        outReactiveMask = sampledMaterial.clearcoat;
        outTransparencyMask = 0.0;
        return;
    }

    outColor = vec4(
        color * sampledMaterial.baseColor.a + reflection,
        sampledMaterial.baseColor.a);
    float temporalMask = max(
        sampledMaterial.baseColor.a, sampledMaterial.transmission);
    outReactiveMask = temporalMask;
    outTransparencyMask = temporalMask;
}
