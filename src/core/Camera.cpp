//
// Created by seawon on 2026/3/18.
//

#include "Camera.h"

#include <algorithm>

Camera::Camera(glm::vec3 position, float yaw, float pitch)
    : m_position(position), m_front(0.0f, 0.0f, -1.0f), m_up(0.0f, 1.0f, 0.0f), m_right(1.0f, 0.0f, 0.0f),
      m_yaw(yaw), m_pitch(pitch) {
    updateVectors();
}

void Camera::processMouseMovement(float xOffset, float yOffset) {
    m_yaw += xOffset * sensitivity;
    m_pitch += yOffset * sensitivity;

    // Clamp pitch to avoid gimbal-lock-like flips when looking straight up/down.
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
    updateVectors();
}

void Camera::setPosition(const glm::vec3& pos) {
    m_position = pos;
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

glm::vec3 Camera::getPosition() const {
    return m_position;
}

glm::vec3 Camera::getFront() const {
    return m_front;
}

glm::vec3 Camera::getRight() const {
    return m_right;
}

glm::vec3 Camera::getUp() const {
    return m_up;
}

Ray Camera::getPickRay() const {
    return {m_position, m_front};
}

void Camera::updateVectors() {
    const glm::vec3 front = {
        cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch)),
        sin(glm::radians(m_pitch)),
        sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch))
    };

    m_front = glm::normalize(front);
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}
