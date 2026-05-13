#include "ShadowMatrices.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace shadow {
namespace ShadowMatrices {

glm::mat4 createOrthoMatrix(float halfPlaneLength, float nearPlane, float farPlane) {
    // Iris: new Matrix4f().setOrthoSymmetric(hpl*2, hpl*2, near, far)
    // glm::ortho(left, right, bottom, top, near, far) with symmetric bounds is equivalent.
    return glm::ortho(-halfPlaneLength, halfPlaneLength,
                       -halfPlaneLength, halfPlaneLength,
                       nearPlane, farPlane);
}

float computeSkyAngle(float shadowAngle) {
    // Iris ShadowMatrices.createBaselineModelViewMatrix:
    //   if (shadowAngle < 0.25f) skyAngle = shadowAngle + 0.75f;
    //   else skyAngle = shadowAngle - 0.25f;
    float normalized = shadowAngle - std::floor(shadowAngle);
    if (normalized < 0.0f) normalized += 1.0f;
    return normalized < 0.25f ? normalized + 0.75f : normalized - 0.25f;
}

void snapModelViewToGrid(glm::mat4& modelView, float intervalSize,
                          double cameraX, double cameraY, double cameraZ) {
    // Iris ShadowMatrices.snapModelViewToGrid:
    //   if (abs(intervalSize) == 0) return;
    //   offsetX = (float)cameraX % intervalSize;
    //   offsetX -= halfIntervalSize;
    //   modelView.translate(offsetX, offsetY, offsetZ);
    //
    // Java % and C++ std::fmod both preserve the sign of the dividend.
    if (std::abs(intervalSize) < 1e-6f) return;

    const float halfInterval = intervalSize * 0.5f;
    const float offsetX = std::fmod(static_cast<float>(cameraX), intervalSize) - halfInterval;
    const float offsetY = std::fmod(static_cast<float>(cameraY), intervalSize) - halfInterval;
    const float offsetZ = std::fmod(static_cast<float>(cameraZ), intervalSize) - halfInterval;

    modelView = glm::translate(modelView, glm::vec3(offsetX, offsetY, offsetZ));
}

glm::mat4 createModelViewMatrix(float shadowAngle, float intervalSize,
                                 float sunPathRotation,
                                 double cameraX, double cameraY, double cameraZ) {
    // Iris ShadowMatrices.createModelViewMatrix:
    //   createBaselineModelViewMatrix(target, shadowAngle, sunPathRotation, near, far);
    //   snapModelViewToGrid(target, intervalSize, cameraX, cameraY, cameraZ);

    const float skyAngle = computeSkyAngle(shadowAngle);

    glm::mat4 view(1.0f);
    view = glm::rotate(view, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::rotate(view, glm::radians(skyAngle * -360.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    view = glm::rotate(view, glm::radians(sunPathRotation), glm::vec3(1.0f, 0.0f, 0.0f));

    // Compute snap offset (matches old Renderer::buildShadowProjectionData exactly).
    // intervalOffset = fmod(cameraPos, intervalSize) - halfIntervalSize
    // Final translate = intervalOffset - cameraPos (single operation).
    glm::vec3 intervalOffset(0.0f);
    if (std::abs(intervalSize) > 1e-6f) {
        const float halfInterval = intervalSize * 0.5f;
        intervalOffset.x = std::fmod(static_cast<float>(cameraX), intervalSize) - halfInterval;
        intervalOffset.y = std::fmod(static_cast<float>(cameraY), intervalSize) - halfInterval;
        intervalOffset.z = std::fmod(static_cast<float>(cameraZ), intervalSize) - halfInterval;
    }
    view = glm::translate(view, intervalOffset - glm::vec3(
        static_cast<float>(cameraX), static_cast<float>(cameraY), static_cast<float>(cameraZ)));

    return view;
}

} // namespace ShadowMatrices
} // namespace shadow
