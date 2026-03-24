//
// Created by seawon on 2026/3/18.
//

#ifndef MECRAFT_CAMERA_H
#define MECRAFT_CAMERA_H


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../physics/Ray.h"


class Camera {
public:
    Camera(glm::vec3 position = {0, 0, 0}, float yaw = -90.0f, float pitch = 0.0f);

    void processMouseMovement(float xOffset, float yOffset);
    void setPosition(const glm::vec3& pos);

    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix(float aspect) const;

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getFront() const;
    [[nodiscard]] glm::vec3 getRight() const;
    [[nodiscard]] glm::vec3 getUp() const;

    // 射线拾取 — 返回视线方向
    [[nodiscard]] Ray getPickRay() const;

    float fov   = 90.0f;
    float nearPlane = 0.1f;
    float farPlane  = 500.0f;
    float sensitivity = 0.1f;

private:
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp = {0, 1, 0};

    float m_yaw, m_pitch;

    void updateVectors();
};

#endif //MECRAFT_CAMERA_H