#ifndef MECRAFT_ECS_CAMERA_COMPONENTS_H
#define MECRAFT_ECS_CAMERA_COMPONENTS_H

#include <glm/glm.hpp>

namespace ecs {

struct CameraStateComponent {
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fov = 75.0f;
    float sensitivity = 0.1f;
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

struct CameraInterpolationComponent {
    float previousYaw = -90.0f;
    float previousPitch = 0.0f;
    float previousFov = 75.0f;
    bool initialized = false;
};

struct SprintFovComponent {
    float walkFov = 75.0f;
    float sprintFov = 90.0f;
    float lerpSpeed = 10.0f;
};

struct ViewBobComponent {
    float amplitude = 0.25f;
    float horizontalAmplitude = 0.02f;
    float frequency = 6.0f;
    float phaseOffset = 0.0f;
    float blend = 0.0f;
    float fadeInSpeed = 10.0f;
    float fadeOutSpeed = 8.0f;
    // Computed offsets (written by ViewBobSystem, read by render code)
    float verticalOffset = 0.0f;
    float horizontalOffset = 0.0f;
};

} // namespace ecs

#endif // MECRAFT_ECS_CAMERA_COMPONENTS_H
