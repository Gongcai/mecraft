#include "CameraController.h"

void CameraController::toggleViewMode() {
    switch (m_viewMode) {
        case ViewMode::FirstPerson:      m_viewMode = ViewMode::ThirdPerson;      break;
        case ViewMode::ThirdPerson:      m_viewMode = ViewMode::ThirdPersonFront; break;
        case ViewMode::ThirdPersonFront: m_viewMode = ViewMode::FirstPerson;      break;
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

Camera CameraController::computeRenderCamera(const Camera& eyeCamera,
                                              const glm::vec3& eyePosition) const {
    if (m_viewMode == ViewMode::FirstPerson) {
        return eyeCamera;
    }

    Camera renderCamera = eyeCamera;
    const glm::vec3 front = eyeCamera.getFront();
    const glm::vec3 up = eyeCamera.getUp();

    if (m_viewMode == ViewMode::ThirdPerson) {
        // Behind the player
        const glm::vec3 camPos = eyePosition
                                 - front * m_thirdPersonDistance
                                 + up * m_thirdPersonHeight;
        renderCamera.setPosition(camPos);
    } else {
        // In front of the player, looking back at them
        const glm::vec3 camPos = eyePosition
                                 + front * m_thirdPersonDistance
                                 + up * m_thirdPersonHeight;
        renderCamera.setPosition(camPos);
        renderCamera.setYawPitch(eyeCamera.getYaw() + 180.0f, -eyeCamera.getPitch());
    }

    return renderCamera;
}
