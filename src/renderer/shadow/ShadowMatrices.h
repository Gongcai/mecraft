#ifndef MECRAFT_SHADOW_MATRICES_H
#define MECRAFT_SHADOW_MATRICES_H

#include <glm/glm.hpp>

// Shadow matrix creation — mirrors Iris ShadowMatrices.java.
//
// Iris reference: Iris-26.1/.../shadows/ShadowMatrices.java
// Mecraft source: Renderer::buildShadowProjectionData (Renderer.cpp:2399-2438)

namespace shadow {

namespace ShadowMatrices {

    // Iris ShadowMatrices.NEAR / FAR
    constexpr float NEAR = -100.05f;
    constexpr float FAR  =  156.0f;

    // Create orthographic projection for shadow map.
    // Iris: new Matrix4f().setOrthoSymmetric(hpl*2, hpl*2, near, far)
    // Mecraft uses glm::ortho which is equivalent for symmetric bounds.
    glm::mat4 createOrthoMatrix(float halfPlaneLength, float nearPlane, float farPlane);

    // Create shadow model-view matrix.
    // Iris: createBaselineModelViewMatrix + snapModelViewToGrid
    //
    // Rotation order (Iris PoseStack mulPose is post-multiply):
    //   1. XP(90°)           — align Y-up world to light space
    //   2. ZP(skyAngle*-360°) — rotate by sun/moon angle
    //   3. XP(sunPathRotation) — apply sun path tilt
    //   4. translate(snapOffset) — grid snap to reduce shimmer
    //
    // shadowAngle: day progress [0,1), 0.5 added for moon shadow.
    //   Normalized to [0,1) before sky angle conversion.
    glm::mat4 createModelViewMatrix(float shadowAngle, float intervalSize,
                                     float sunPathRotation,
                                     double cameraX, double cameraY, double cameraZ);

    // Compute sky angle from shadow angle.
    // Iris: if (shadowAngle < 0.25f) skyAngle = shadowAngle + 0.75f
    //       else skyAngle = shadowAngle - 0.25f
    float computeSkyAngle(float shadowAngle);

    // Snap model-view matrix to shadow interval grid.
    // Iris: offsetX = (float)cameraX % intervalSize - halfIntervalSize
    // Applied as translate(offsetX, offsetY, offsetZ) after camera-to-origin.
    //
    // Note: Java % and C++ std::fmod both preserve the sign of the dividend,
    // so negative camera coordinates produce the same snap offset.
    void snapModelViewToGrid(glm::mat4& modelView, float intervalSize,
                              double cameraX, double cameraY, double cameraZ);

} // namespace ShadowMatrices
} // namespace shadow

#endif // MECRAFT_SHADOW_MATRICES_H
