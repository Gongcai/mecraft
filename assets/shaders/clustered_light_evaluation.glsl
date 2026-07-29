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

struct GpuLightSurfaceContribution {
    vec3 diffuse;
    vec3 specular;
};

float gpuLightFiniteRangeAttenuation(
    float distanceSquared,
    float normalizedDistanceSquared) {
    float window = max(
        1.0 - normalizedDistanceSquared * normalizedDistanceSquared, 0.0);
    return window * window / max(distanceSquared, 1.0e-4);
}

GpuLightSurfaceContribution evaluateGpuLight(
    GpuLight light,
    vec3 cameraRelativeSurface,
    vec3 normal,
    vec3 viewDirection,
    vec3 specularF0,
    float specularF90,
    float nDotV,
    float alphaSquared) {
    GpuLightSurfaceContribution result;
    result.diffuse = vec3(0.0);
    result.specular = vec3(0.0);

    uint lightType = gpuLightType(light);
    vec3 lightDirection;
    float distanceSquared = 0.0;
    float normalizedDistanceSquared = 0.0;
    if (lightType == GPU_LIGHT_TYPE_DIRECTIONAL) {
        lightDirection = -light.direction.xyz;
    } else {
        vec3 surfaceToLight = light.positionAndRange.xyz -
            cameraRelativeSurface;
        distanceSquared = dot(surfaceToLight, surfaceToLight);
        normalizedDistanceSquared =
            distanceSquared * gpuLightInverseRangeSquared(light);
        if (normalizedDistanceSquared >= 1.0 ||
            distanceSquared <= 1.0e-10) {
            return result;
        }
        lightDirection = surfaceToLight * inversesqrt(distanceSquared);
    }

    float nDotL = max(dot(normal, lightDirection), 0.0);
    if (nDotL <= 0.0) {
        return result;
    }

    float illuminance;
    if (lightType == GPU_LIGHT_TYPE_DIRECTIONAL) {
        illuminance = light.colorAndIntensity.w / GPU_LIGHT_LUMINOUS_EFFICACY;
    } else {
        float angularAttenuation = 1.0;
        if (lightType == GPU_LIGHT_TYPE_SPOT) {
            float coneCosine = dot(
                light.direction.xyz, -lightDirection);
            angularAttenuation = smoothstep(
                light.spotCosinesAndRectSize.y,
                light.spotCosinesAndRectSize.x,
                coneCosine);
        } else if (lightType == GPU_LIGHT_TYPE_RECT) {
            float emitterCosine = max(dot(
                light.direction.xyz, -lightDirection), 0.0);
            float emitterArea = light.spotCosinesAndRectSize.z *
                light.spotCosinesAndRectSize.w;
            angularAttenuation = emitterCosine * emitterArea;
        }
        illuminance = light.colorAndIntensity.w /
            GPU_LIGHT_LUMINOUS_EFFICACY * angularAttenuation *
            gpuLightFiniteRangeAttenuation(
                distanceSquared, normalizedDistanceSquared);
    }

    if (illuminance <= 0.0) {
        return result;
    }
    vec3 incident = light.colorAndIntensity.rgb * illuminance;
    if (gpuLightContributes(light, GPU_LIGHT_CONTRIBUTION_DIFFUSE)) {
        result.diffuse = incident * nDotL * PBR_RECIPROCAL_PI;
    }
    if (gpuLightContributes(light, GPU_LIGHT_CONTRIBUTION_SPECULAR)) {
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float nDotH = max(dot(normal, halfDirection), 0.0);
        float lDotH = max(dot(lightDirection, halfDirection), 0.0);
        result.specular = incident * pbrEvaluateDirectSpecular(
            lDotH, nDotV, nDotL, nDotH, alphaSquared,
            specularF0, specularF90);
    }
    return result;
}

#endif
