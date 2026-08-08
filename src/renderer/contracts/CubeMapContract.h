#ifndef MECRAFT_CUBE_MAP_CONTRACT_H
#define MECRAFT_CUBE_MAP_CONTRACT_H

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>

namespace renderer::contracts {

inline constexpr uint32_t kCubeMapFaceCount = 6u;

// Face order and basis vectors follow the Vulkan cubemap sampling contract:
// +X, -X, +Y, -Y, +Z, -Z. The RHI stores render targets with a negative
// viewport height, so capture projections invert only clip-space Y before
// rasterization; this preserves the hardware face's horizontal mapping.
inline constexpr std::array<glm::vec3, kCubeMapFaceCount> kCubeMapFaceDirections{
    glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)};

inline constexpr std::array<glm::vec3, kCubeMapFaceCount> kCubeMapFaceUpVectors{
    glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)};

/// Builds the view matrix for one Vulkan cubemap face.
/// @param positionWorldMeters Cubemap capture origin in world space.
/// @param face Face index in the fixed six-face order.
/// @return Right-handed view matrix matching samplerCube face coordinates.
[[nodiscard]] inline glm::mat4 cubeMapFaceView(const glm::vec3& positionWorldMeters, const uint32_t face) {
    return glm::lookAt(positionWorldMeters, positionWorldMeters + kCubeMapFaceDirections[face],
                       kCubeMapFaceUpVectors[face]);
}

/// Builds a 90-degree projection compatible with the RHI's negative viewport.
/// @param nearPlaneMeters Positive capture near plane.
/// @param farPlaneMeters Capture far plane greater than the near plane.
/// @return Perspective projection with clip-space Y inverted exactly once.
[[nodiscard]] inline glm::mat4 cubeMapFaceProjection(const float nearPlaneMeters, const float farPlaneMeters) {
    glm::mat4 projection = glm::perspective(glm::half_pi<float>(), 1.0f, nearPlaneMeters, farPlaneMeters);
    for (uint32_t column = 0u; column < 4u; ++column) {
        projection[column][1] = -projection[column][1];
    }
    return projection;
}

/// Builds the complete view-projection matrix used while rasterizing a face.
/// @param positionWorldMeters Cubemap capture origin in world space.
/// @param face Face index in the fixed six-face order.
/// @param nearPlaneMeters Positive capture near plane.
/// @param farPlaneMeters Capture far plane greater than the near plane.
/// @return Face matrix whose rasterized rows match samplerCube lookup rows.
[[nodiscard]] inline glm::mat4 cubeMapFaceViewProjection(const glm::vec3& positionWorldMeters,
                                                         const uint32_t face, const float nearPlaneMeters,
                                                         const float farPlaneMeters) {
    return cubeMapFaceProjection(nearPlaneMeters, farPlaneMeters) * cubeMapFaceView(positionWorldMeters, face);
}

} // namespace renderer::contracts

#endif // MECRAFT_CUBE_MAP_CONTRACT_H
