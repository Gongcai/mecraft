//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_CAMERA_H
#define MECRAFT_CAMERA_H


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "physics/PhysicsInfo.h"


class Camera {
public:
    Camera(glm::vec3 position = {0, 0, 0}, float yaw = -90.0f, float pitch = 0.0f);

    void processMouseMovement(float xOffset, float yOffset);
    void setPosition(const glm::vec3& pos);

    /// Replaces the camera transform with one explicit orthonormal view pose.
    /// @param position World-space camera position.
    /// @param forward World-space viewing direction; length need not be one.
    /// @param up Requested world-space up direction; length need not be one.
    /// @param verticalFovDegrees Vertical field of view inside the open range (1, 179).
    /// @return True when every value is finite and the view basis is non-degenerate.
    [[nodiscard]] bool setViewPose(const glm::vec3& position,
                                   const glm::vec3& forward,
                                   const glm::vec3& up,
                                   float verticalFovDegrees);

    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix(float aspect) const;

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getFront() const;
    [[nodiscard]] glm::vec3 getRight() const;
    [[nodiscard]] glm::vec3 getUp() const;
    [[nodiscard]] float getYaw() const;
    [[nodiscard]] float getPitch() const;


    // 射线拾取 — 返回视线方向
    [[nodiscard]] PhysicsInfo getPickRay() const;


    //参数配置
    [[nodiscard]] float getFOV() const;
    [[nodiscard]] float getNear() const;
    [[nodiscard]] float getFar() const;
    [[nodiscard]] float getSensitivity() const;

    void setFOV(float fov);
    void setNear(float near);
    void setFar(float far);
    void setSensitivity(float sensitivity);
    void setYawPitch(float yaw, float pitch);




private:
    float fov   = 75.0f;
    float nearPlane = 0.1f;
    float farPlane  = 500.0f;
    float sensitivity = 0.1f;


    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp = {0, 1, 0};

    float m_yaw, m_pitch;

    void updateVectors();
};

#endif //MECRAFT_CAMERA_H
