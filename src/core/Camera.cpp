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

PhysicsInfo Camera::getPickRay() const {
    return {m_position, m_front};
}

float Camera::getFOV() const {
    return fov;
}

float Camera::getNear() const {
    return nearPlane;
}

float Camera::getFar() const {
    return farPlane;
}

float Camera::getSensitivity() const {
    return sensitivity;
}

void Camera::setFOV(float newFov) {
    // Keep FOV within a sensible range (1..179 degrees)
    if (newFov < 1.0f) newFov = 1.0f;
    if (newFov > 179.0f) newFov = 179.0f;
    this->fov = newFov;
}

void Camera::setNear(float newNear) {
    // near must be positive and less than farPlane
    if (newNear <= 0.0f) newNear = 0.001f;
    if (newNear >= farPlane) newNear = farPlane * 0.5f;
    this->nearPlane = newNear;
}

void Camera::setFar(float newFar) {
    // far must be greater than nearPlane
    if (newFar <= nearPlane) newFar = nearPlane + 1.0f;
    this->farPlane = newFar;
}

void Camera::setSensitivity(float newSensitivity) {
    // sensitivity should be positive
    if (newSensitivity <= 0.0f) newSensitivity = 0.0001f;
    this->sensitivity = newSensitivity;
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
