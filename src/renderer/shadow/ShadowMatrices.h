#ifndef MECRAFT_SHADOW_MATRICES_H
#define MECRAFT_SHADOW_MATRICES_H

#include <glm/glm.hpp>

#include <array>

// Mecraft CSM matrix creation.
//
// The formal shadow path is a linear cascaded shadow map designed around
// Mecraft's greedy terrain meshes. Iris/Derivative radial warp is intentionally
// left out of this module; that path is historical debug compatibility only.

namespace shadow {

namespace ShadowMatrices {

constexpr int CASCADE_COUNT = 4;

struct CameraBasis {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float nearPlane = 0.05f;
    float verticalFovDegrees = 70.0f;
    float aspectRatio = 1.0f;
};

struct Cascade {
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    float splitNear = 0.0f;
    float splitFar = 1.0f;
    float texelWorldSize = 1.0f;
    float radius = 1.0f;
    float depthExtent = 1.0f;
};

struct Settings {
    float shadowDistance = 192.0f;
    int shadowResolution = 2048;
    std::array<float, CASCADE_COUNT> splitFractions = {0.10f, 0.24f, 0.52f, 1.0f};
};

std::array<Cascade, CASCADE_COUNT> buildCascades(const CameraBasis& camera, const glm::vec3& lightDirection,
                                                 const Settings& settings);

} // namespace ShadowMatrices
} // namespace shadow

#endif // MECRAFT_SHADOW_MATRICES_H
