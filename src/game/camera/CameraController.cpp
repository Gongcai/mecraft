#include "CameraController.h"

#include "../../physics/PhysicsInfo.h"
#include "../../world/WorldRaycast.h"

#include <algorithm>

namespace {

constexpr float kThirdPersonCollisionPadding = 0.18f;
constexpr float kMinCameraProbeLength = 0.001f;

glm::vec3 resolveCameraCollision(const glm::vec3& eyePosition, const glm::vec3& desiredPosition,
                                 const IWorldView& worldView) {
    const glm::vec3 offset = desiredPosition - eyePosition;
    const float maxDistance = glm::length(offset);
    if (maxDistance <= kMinCameraProbeLength) {
        return desiredPosition;
    }

    const glm::vec3 direction = offset / maxDistance;
    const RayHit hit = raycastWorldView(worldView, PhysicsInfo(eyePosition, direction), maxDistance);
    if (!hit.hit) {
        return desiredPosition;
    }

    const float resolvedDistance = std::max(0.0f, hit.distance - kThirdPersonCollisionPadding);
    return eyePosition + direction * resolvedDistance;
}

} // namespace

void CameraController::toggleViewMode() {
    switch (m_viewMode) {
    case ViewMode::FirstPerson: m_viewMode = ViewMode::ThirdPerson; break;
    case ViewMode::ThirdPerson: m_viewMode = ViewMode::ThirdPersonFront; break;
    case ViewMode::ThirdPersonFront: m_viewMode = ViewMode::FirstPerson; break;
    }
}

void CameraController::setViewMode(const ViewMode mode) {
    m_viewMode = mode;
}

CameraController::ViewMode CameraController::getViewMode() const {
    return m_viewMode;
}

bool CameraController::isFirstPerson() const {
    return m_viewMode == ViewMode::FirstPerson;
}

bool CameraController::shouldRenderPlayerModel() const {
    return m_viewMode != ViewMode::FirstPerson;
}

void CameraController::setThirdPersonDistance(const float distance) {
    m_thirdPersonDistance = distance;
}

void CameraController::setThirdPersonHeight(const float height) {
    m_thirdPersonHeight = height;
}

float CameraController::getThirdPersonDistance() const {
    return m_thirdPersonDistance;
}

float CameraController::getThirdPersonHeight() const {
    return m_thirdPersonHeight;
}

Camera CameraController::computeRenderCamera(const Camera& eyeCamera, const glm::vec3& eyePosition) const {
    if (m_viewMode == ViewMode::FirstPerson) {
        return eyeCamera;
    }

    Camera renderCamera = eyeCamera;
    const glm::vec3 front = eyeCamera.getFront();
    const glm::vec3 up = eyeCamera.getUp();

    if (m_viewMode == ViewMode::ThirdPerson) {
        // Behind the player
        const glm::vec3 camPos = eyePosition - front * m_thirdPersonDistance + up * m_thirdPersonHeight;
        renderCamera.setPosition(camPos);
    } else {
        // In front of the player, looking back at them
        const glm::vec3 camPos = eyePosition + front * m_thirdPersonDistance + up * m_thirdPersonHeight;
        renderCamera.setPosition(camPos);
        renderCamera.setYawPitch(eyeCamera.getYaw() + 180.0f, -eyeCamera.getPitch());
    }

    return renderCamera;
}

Camera CameraController::computeRenderCamera(const Camera& eyeCamera, const glm::vec3& eyePosition,
                                             const IWorldView& worldView) const {
    if (m_viewMode == ViewMode::FirstPerson) {
        return eyeCamera;
    }

    Camera renderCamera = eyeCamera;
    const glm::vec3 front = eyeCamera.getFront();
    const glm::vec3 up = eyeCamera.getUp();

    if (m_viewMode == ViewMode::ThirdPerson) {
        // Behind the player
        const glm::vec3 camPos = eyePosition - front * m_thirdPersonDistance + up * m_thirdPersonHeight;
        renderCamera.setPosition(resolveCameraCollision(eyePosition, camPos, worldView));
    } else {
        // In front of the player, looking back at them
        const glm::vec3 camPos = eyePosition + front * m_thirdPersonDistance + up * m_thirdPersonHeight;
        renderCamera.setPosition(resolveCameraCollision(eyePosition, camPos, worldView));
        renderCamera.setYawPitch(eyeCamera.getYaw() + 180.0f, -eyeCamera.getPitch());
    }

    return renderCamera;
}
