#include "ShadowMatrices.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace shadow {
namespace ShadowMatrices {

namespace {
glm::vec3 normalizeOr(const glm::vec3& value, const glm::vec3& fallback) {
    return glm::dot(value, value) > 1.0e-8f ? glm::normalize(value) : fallback;
}

std::array<glm::vec3, 8> buildAabbCorners(const glm::vec3& boundsMin,
                                          const glm::vec3& boundsMax) {
    std::array<glm::vec3, 8> corners{};
    for (int corner = 0; corner < 8; ++corner) {
        corners[corner] = glm::vec3(
            (corner & 1) != 0 ? boundsMax.x : boundsMin.x,
            (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
            (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
    }
    return corners;
}

float maxAbsLightDepth(const glm::mat4& lightView,
                       const std::array<glm::vec3, 8>& corners,
                       float currentMax) {
    float depth = currentMax;
    for (const glm::vec3& corner : corners) {
        const float lightZ = (lightView * glm::vec4(corner, 1.0f)).z;
        depth = std::max(depth, std::abs(lightZ));
    }
    return depth;
}
}

std::array<Cascade, CASCADE_COUNT> buildCascades(const CameraBasis& camera,
                                                 const glm::vec3& lightDirection,
                                                 const Settings& settings) {
    std::array<Cascade, CASCADE_COUNT> cascades{};

    const float shadowDistance = std::max(64.0f, settings.shadowDistance);
    const float nearPlane = std::max(0.05f, camera.nearPlane);
    const float aspect = std::max(0.01f, camera.aspectRatio);
    const float tanHalfFovY = std::tan(glm::radians(camera.verticalFovDegrees) * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspect;
    const float resolution = static_cast<float>(std::max(1, settings.shadowResolution));

    const glm::vec3 lightDir = normalizeOr(lightDirection, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 cameraForward = normalizeOr(camera.forward, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 cameraRight = normalizeOr(camera.right, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraUp = normalizeOr(camera.up, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 upRef = std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.92f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);

    float splitNear = nearPlane;
    for (int cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        const float fraction = std::clamp(settings.splitFractions[cascade], 0.0f, 1.0f);
        const float splitFar = std::max(splitNear + 1.0f, shadowDistance * fraction);

        std::array<glm::vec3, 8> corners{};
        int cornerIndex = 0;
        for (const float dist : {splitNear, splitFar}) {
            const glm::vec3 center = camera.position + cameraForward * dist;
            const float halfX = dist * tanHalfFovX;
            const float halfY = dist * tanHalfFovY;
            corners[cornerIndex++] = center - cameraRight * halfX - cameraUp * halfY;
            corners[cornerIndex++] = center + cameraRight * halfX - cameraUp * halfY;
            corners[cornerIndex++] = center - cameraRight * halfX + cameraUp * halfY;
            corners[cornerIndex++] = center + cameraRight * halfX + cameraUp * halfY;
        }

        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners) {
            center += corner;
        }
        center /= static_cast<float>(corners.size());

        float radius = 1.0f;
        for (const glm::vec3& corner : corners) {
            radius = std::max(radius, glm::length(corner - center));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float lightDistance = shadowDistance + radius * 2.0f;
        const glm::mat4 lightView = glm::lookAt(center + lightDir * lightDistance,
                                                center,
                                                upRef);
        const float texelWorldSize = (radius * 2.0f) / resolution;
        const glm::vec3 centerLight = glm::vec3(lightView * glm::vec4(center, 1.0f));
        const float snappedX = std::floor(centerLight.x / texelWorldSize) * texelWorldSize;
        const float snappedY = std::floor(centerLight.y / texelWorldSize) * texelWorldSize;
        const glm::vec3 casterBoundsMin = camera.position - glm::vec3(shadowDistance);
        const glm::vec3 casterBoundsMax = camera.position + glm::vec3(shadowDistance);
        const std::array<glm::vec3, 8> casterCorners = buildAabbCorners(casterBoundsMin, casterBoundsMax);
        float depthExtent = shadowDistance + radius * 3.0f;
        depthExtent = maxAbsLightDepth(lightView, corners, depthExtent);
        depthExtent = maxAbsLightDepth(lightView, casterCorners, depthExtent);
        depthExtent += std::max(16.0f, texelWorldSize * 32.0f);
        const glm::mat4 projection = glm::ortho(snappedX - radius, snappedX + radius,
                                                snappedY - radius, snappedY + radius,
                                                -depthExtent, depthExtent);

        cascades[cascade].view = lightView;
        cascades[cascade].projection = projection;
        cascades[cascade].viewProj = projection * lightView;
        cascades[cascade].splitNear = splitNear;
        cascades[cascade].splitFar = splitFar;
        cascades[cascade].texelWorldSize = texelWorldSize;
        cascades[cascade].radius = radius;
        cascades[cascade].depthExtent = depthExtent;
        splitNear = splitFar;
    }

    return cascades;
}

} // namespace ShadowMatrices
} // namespace shadow
