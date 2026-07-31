#include "ShadowMatrices.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace shadow {
namespace ShadowMatrices {

namespace {
glm::vec3 normalizeOr(const glm::vec3& value, const glm::vec3& fallback) {
    return glm::dot(value, value) > 1.0e-8f ? glm::normalize(value) : fallback;
}

std::array<glm::vec3, 8> buildAabbCorners(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    std::array<glm::vec3, 8> corners{};
    for (int corner = 0; corner < 8; ++corner) {
        corners[corner] =
            glm::vec3((corner & 1) != 0 ? boundsMax.x : boundsMin.x, (corner & 2) != 0 ? boundsMax.y : boundsMin.y,
                      (corner & 4) != 0 ? boundsMax.z : boundsMin.z);
    }
    return corners;
}

void includeLightDepthRange(const glm::mat4& lightView, const std::array<glm::vec3, 8>& corners, float& minZ,
                            float& maxZ) {
    for (const glm::vec3& corner : corners) {
        const float lightZ = (lightView * glm::vec4(corner, 1.0f)).z;
        minZ = std::min(minZ, lightZ);
        maxZ = std::max(maxZ, lightZ);
    }
}

void snapProjectionToTexelGrid(glm::mat4& projection, const glm::mat4& lightView, float effectiveResolution) {
    const glm::mat4 viewProj = projection * lightView;
    glm::vec4 shadowOrigin = viewProj * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin *= effectiveResolution * 0.5f;

    const glm::vec2 roundedOrigin(std::round(shadowOrigin.x), std::round(shadowOrigin.y));
    const glm::vec2 roundOffset = (roundedOrigin - glm::vec2(shadowOrigin)) * (2.0f / effectiveResolution);
    projection[3][0] += roundOffset.x;
    projection[3][1] += roundOffset.y;
}
} // namespace

std::array<Cascade, CASCADE_COUNT> buildCascades(const CameraBasis& camera, const glm::vec3& lightDirection,
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

        const float resolutionScale = cascade >= 2 ? 0.5f : 1.0f;
        const float effectiveResolution = std::max(1.0f, resolution * resolutionScale);
        const float cascadeCasterDistance = std::max(shadowDistance, radius + 32.0f);
        const float lightDistance = cascadeCasterDistance + radius * 2.0f;
        const glm::mat4 lightView = glm::lookAt(center + lightDir * lightDistance, center, upRef);
        const float texelWorldSize = (radius * 2.0f) / effectiveResolution;
        const glm::vec3 casterBoundsMin = camera.position - glm::vec3(cascadeCasterDistance);
        const glm::vec3 casterBoundsMax = camera.position + glm::vec3(cascadeCasterDistance);
        const std::array<glm::vec3, 8> casterCorners = buildAabbCorners(casterBoundsMin, casterBoundsMax);
        float minLightZ = std::numeric_limits<float>::max();
        float maxLightZ = std::numeric_limits<float>::lowest();
        includeLightDepthRange(lightView, corners, minLightZ, maxLightZ);
        includeLightDepthRange(lightView, casterCorners, minLightZ, maxLightZ);
        const float depthPadding = std::max(16.0f, texelWorldSize * 32.0f);
        minLightZ -= depthPadding;
        maxLightZ += depthPadding;
        const float depthExtent = std::max(1.0f, (maxLightZ - minLightZ) * 0.5f);
        glm::mat4 projection = glm::ortho(-radius, radius, -radius, radius, -maxLightZ, -minLightZ);
        snapProjectionToTexelGrid(projection, lightView, effectiveResolution);

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
