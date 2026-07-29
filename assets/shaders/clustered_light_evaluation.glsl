#ifndef MECRAFT_CLUSTERED_LIGHT_EVALUATION_GLSL
#define MECRAFT_CLUSTERED_LIGHT_EVALUATION_GLSL

#include "gpu_light_contract.glsl"
#include "pbr_brdf.glsl"

const float GPU_LIGHT_LUMINOUS_EFFICACY = 683.0;

struct ClusteredSurfaceLighting {
    vec3 diffuse;
    vec3 specular;
    uint shadowLightId;
    uint shadowMetadataIndex;
    vec3 shadowResourceCoordinate;
    float shadowVisibility;
    float shadowWeight;
};

float gpuLightFiniteRangeAttenuation(float distanceToLight, float range) {
    float normalizedDistance = distanceToLight / range;
    float window = max(1.0 - normalizedDistance * normalizedDistance *
        normalizedDistance * normalizedDistance, 0.0);
    return window * window / max(distanceToLight * distanceToLight, 1.0e-4);
}

ClusteredSurfaceLighting evaluateGpuLight(
    GpuLight light,
    vec3 cameraRelativeSurface,
    vec3 normal,
    vec3 viewDirection,
    vec3 specularF0,
    float specularF90,
    float perceptualRoughness) {
    ClusteredSurfaceLighting result;
    result.diffuse = vec3(0.0);
    result.specular = vec3(0.0);
    result.shadowLightId = GPU_LIGHT_INVALID_RESOURCE_INDEX;
    result.shadowMetadataIndex = GPU_LIGHT_INVALID_RESOURCE_INDEX;
    result.shadowResourceCoordinate = vec3(0.0);
    result.shadowVisibility = 1.0;
    result.shadowWeight = 0.0;

    uint lightType = gpuLightType(light);
    vec3 lightDirection;
    float illuminance;
    if (lightType == GPU_LIGHT_TYPE_DIRECTIONAL) {
        lightDirection = normalize(-light.direction.xyz);
        illuminance = light.colorAndIntensity.w / GPU_LIGHT_LUMINOUS_EFFICACY;
    } else {
        vec3 surfaceToLight = light.positionAndRange.xyz -
            cameraRelativeSurface;
        float distanceToLight = length(surfaceToLight);
        if (distanceToLight >= light.positionAndRange.w ||
            distanceToLight <= 1.0e-5) {
            return result;
        }
        lightDirection = surfaceToLight / distanceToLight;
        float angularAttenuation = 1.0;
        if (lightType == GPU_LIGHT_TYPE_SPOT) {
            float coneCosine = dot(
                normalize(light.direction.xyz), -lightDirection);
            angularAttenuation = smoothstep(
                light.spotCosinesAndRectSize.y,
                light.spotCosinesAndRectSize.x,
                coneCosine);
        } else if (lightType == GPU_LIGHT_TYPE_RECT) {
            float emitterCosine = max(dot(
                normalize(light.direction.xyz), -lightDirection), 0.0);
            float emitterArea = light.spotCosinesAndRectSize.z *
                light.spotCosinesAndRectSize.w;
            angularAttenuation = emitterCosine * emitterArea;
        }
        illuminance = light.colorAndIntensity.w /
            GPU_LIGHT_LUMINOUS_EFFICACY * angularAttenuation *
            gpuLightFiniteRangeAttenuation(
                distanceToLight, light.positionAndRange.w);
    }

    float nDotL = max(dot(normal, lightDirection), 0.0);
    if (nDotL <= 0.0 || illuminance <= 0.0) {
        return result;
    }
    vec3 incident = light.colorAndIntensity.rgb * illuminance;
    if (gpuLightContributes(light, GPU_LIGHT_CONTRIBUTION_DIFFUSE)) {
        result.diffuse = incident * nDotL * PBR_RECIPROCAL_PI;
    }
    if (gpuLightContributes(light, GPU_LIGHT_CONTRIBUTION_SPECULAR)) {
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float nDotV = max(dot(normal, viewDirection), 0.0);
        float nDotH = max(dot(normal, halfDirection), 0.0);
        float lDotH = max(dot(lightDirection, halfDirection), 0.0);
        float alphaSquared =
            pbrPerceptualRoughnessToAlphaSquared(perceptualRoughness);
        result.specular = incident * pbrEvaluateDirectSpecular(
            lDotH, nDotV, nDotL, nDotH, alphaSquared,
            specularF0, specularF90);
    }
    return result;
}

#endif
