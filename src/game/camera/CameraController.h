#ifndef MECRAFT_CAMERA_CONTROLLER_H
#define MECRAFT_CAMERA_CONTROLLER_H

#include "../../engine/camera/Camera.h"
#include <glm/glm.hpp>

class IWorldView;

class CameraController {
public:
    enum class ViewMode { FirstPerson, ThirdPerson, ThirdPersonFront };

    void toggleViewMode();
    void setViewMode(ViewMode mode);
    [[nodiscard]] ViewMode getViewMode() const;
    [[nodiscard]] bool isFirstPerson() const;
    [[nodiscard]] bool shouldRenderPlayerModel() const;

    void setThirdPersonDistance(float distance);
    void setThirdPersonHeight(float height);
    [[nodiscard]] float getThirdPersonDistance() const;
    [[nodiscard]] float getThirdPersonHeight() const;

    /// Compute the effective render camera.
    /// In first-person mode, returns the eye camera unchanged.
    /// In third-person back mode, offsets the camera behind and above the eye position.
    /// In third-person front mode, offsets the camera in front and flips the view direction.
    [[nodiscard]] Camera computeRenderCamera(const Camera& eyeCamera, const glm::vec3& eyePosition) const;
    /// Compute the effective render camera and shorten third-person offsets
    /// when a solid block lies between the eye and desired camera position.
    [[nodiscard]] Camera computeRenderCamera(const Camera& eyeCamera, const glm::vec3& eyePosition,
                                             const IWorldView& worldView) const;

private:
    ViewMode m_viewMode = ViewMode::FirstPerson;
    float m_thirdPersonDistance = 4.0f;
    float m_thirdPersonHeight = 1.0f;
};

#endif // MECRAFT_CAMERA_CONTROLLER_H
