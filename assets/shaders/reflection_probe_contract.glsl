#ifndef MECRAFT_REFLECTION_PROBE_CONTRACT_GLSL
#define MECRAFT_REFLECTION_PROBE_CONTRACT_GLSL

const uint REFLECTION_PROBE_CONTRACT_VERSION = 1u;
const uint REFLECTION_PROBE_INVALID_CUBEMAP_INDEX = 0xffffffffu;
const uint REFLECTION_PROBE_BLEND_COUNT = 4u;

struct GpuReflectionProbe {
    vec4 positionAndExposure;
    vec4 influenceMinAndBlendDistance;
    vec4 influenceMaxAndValidity;
    vec4 boxProjectionMin;
    vec4 boxProjectionMax;
    uvec4 resourcesAndIdentity;
};

bool reflectionProbeContainsInclusive(
    vec3 minimumValue,
    vec3 maximumValue,
    vec3 point) {
    return all(lessThanEqual(minimumValue, point)) &&
        all(lessThanEqual(point, maximumValue));
}

float reflectionProbeInfluenceWeight(
    GpuReflectionProbe probe,
    vec3 surfacePosition,
    vec3 surfaceNormal) {
    vec3 influenceMinimum = probe.influenceMinAndBlendDistance.xyz;
    vec3 influenceMaximum = probe.influenceMaxAndValidity.xyz;
    if (!reflectionProbeContainsInclusive(
            influenceMinimum, influenceMaximum, surfacePosition)) {
        return 0.0;
    }

    float normalLengthSquared = dot(surfaceNormal, surfaceNormal);
    if (normalLengthSquared <= 0.0 || isnan(normalLengthSquared) ||
        isinf(normalLengthSquared)) {
        return 0.0;
    }

    vec3 distanceToBoundary = min(
        surfacePosition - influenceMinimum,
        influenceMaximum - surfacePosition);
    float boundaryDistance = min(
        distanceToBoundary.x,
        min(distanceToBoundary.y, distanceToBoundary.z));
    float boundaryWeight = clamp(
        boundaryDistance / probe.influenceMinAndBlendDistance.w,
        0.0, 1.0);

    vec3 probePosition = probe.positionAndExposure.xyz;
    vec3 probeOffset = surfacePosition - probePosition;
    vec3 directionalExtent = vec3(
        probeOffset.x >= 0.0
            ? influenceMaximum.x - probePosition.x
            : probePosition.x - influenceMinimum.x,
        probeOffset.y >= 0.0
            ? influenceMaximum.y - probePosition.y
            : probePosition.y - influenceMinimum.y,
        probeOffset.z >= 0.0
            ? influenceMaximum.z - probePosition.z
            : probePosition.z - influenceMinimum.z);
    vec3 normalizedOffset = abs(probeOffset) / directionalExtent;
    float normalizedDistance = max(
        normalizedOffset.x,
        max(normalizedOffset.y, normalizedOffset.z));
    float distanceWeight = 1.0 - clamp(normalizedDistance, 0.0, 1.0);

    float probeDistanceSquared = dot(probeOffset, probeOffset);
    float facingWeight = 1.0;
    if (probeDistanceSquared > 0.0) {
        vec3 normalizedSurfaceNormal =
            surfaceNormal * inversesqrt(normalLengthSquared);
        vec3 surfaceToProbe =
            -probeOffset * inversesqrt(probeDistanceSquared);
        facingWeight = max(dot(normalizedSurfaceNormal, surfaceToProbe), 0.0);
    }

    return clamp(
        boundaryWeight * distanceWeight * facingWeight *
            probe.influenceMaxAndValidity.w,
        0.0, 1.0);
}

bool reflectionProbeBoxProject(
    GpuReflectionProbe probe,
    vec3 surfacePosition,
    vec3 reflectionDirection,
    out vec3 correctedDirection) {
    vec3 projectionMinimum = probe.boxProjectionMin.xyz;
    vec3 projectionMaximum = probe.boxProjectionMax.xyz;
    float directionLengthSquared =
        dot(reflectionDirection, reflectionDirection);
    if (!reflectionProbeContainsInclusive(
            projectionMinimum, projectionMaximum, surfacePosition) ||
        directionLengthSquared <= 0.0 || isnan(directionLengthSquared) ||
        isinf(directionLengthSquared)) {
        correctedDirection = vec3(0.0);
        return false;
    }

    vec3 direction = reflectionDirection *
        inversesqrt(directionLengthSquared);
    vec3 exitDistances = vec3(3.402823466e+38);
    if (direction.x > 0.0) {
        exitDistances.x =
            (projectionMaximum.x - surfacePosition.x) / direction.x;
    } else if (direction.x < 0.0) {
        exitDistances.x =
            (projectionMinimum.x - surfacePosition.x) / direction.x;
    }
    if (direction.y > 0.0) {
        exitDistances.y =
            (projectionMaximum.y - surfacePosition.y) / direction.y;
    } else if (direction.y < 0.0) {
        exitDistances.y =
            (projectionMinimum.y - surfacePosition.y) / direction.y;
    }
    if (direction.z > 0.0) {
        exitDistances.z =
            (projectionMaximum.z - surfacePosition.z) / direction.z;
    } else if (direction.z < 0.0) {
        exitDistances.z =
            (projectionMinimum.z - surfacePosition.z) / direction.z;
    }

    float exitDistance = min(
        exitDistances.x, min(exitDistances.y, exitDistances.z));
    vec3 hitPosition = surfacePosition + direction * exitDistance;
    vec3 corrected = hitPosition - probe.positionAndExposure.xyz;
    float correctedLengthSquared = dot(corrected, corrected);
    if (exitDistance < 0.0 || isnan(exitDistance) || isinf(exitDistance) ||
        correctedLengthSquared <= 0.0 || isnan(correctedLengthSquared) ||
        isinf(correctedLengthSquared)) {
        correctedDirection = vec3(0.0);
        return false;
    }
    correctedDirection = corrected * inversesqrt(correctedLengthSquared);
    return true;
}

#endif // MECRAFT_REFLECTION_PROBE_CONTRACT_GLSL
