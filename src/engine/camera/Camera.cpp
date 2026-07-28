//
// Created by seawon on 2026/3/18.
//

#include "Camera.h"

#include <algorithm>
#include <cmath>

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

bool Camera::setViewPose(const glm::vec3& position,
                         const glm::vec3& forward,
                         const glm::vec3& up,
                         const float verticalFovDegrees) {
    const auto finiteVector = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    constexpr float kMinimumLengthSquared = 1.0e-12f;
    if (!finiteVector(position) || !finiteVector(forward) ||
        !finiteVector(up) || !std::isfinite(verticalFovDegrees) ||
        verticalFovDegrees <= 1.0f || verticalFovDegrees >= 179.0f) {
        return false;
    }
    const float forwardLengthSquared = glm::dot(forward, forward);
    const float upLengthSquared = glm::dot(up, up);
    if (!std::isfinite(forwardLengthSquared) ||
        !std::isfinite(upLengthSquared) ||
        forwardLengthSquared <= kMinimumLengthSquared ||
        upLengthSquared <= kMinimumLengthSquared) {
        return false;
    }
    const glm::vec3 normalizedForward =
        forward / std::sqrt(forwardLengthSquared);
    const glm::vec3 normalizedUp = up / std::sqrt(upLengthSquared);
    glm::vec3 right = glm::cross(normalizedForward, normalizedUp);
    const float rightLengthSquared = glm::dot(right, right);
    if (!std::isfinite(rightLengthSquared) ||
        rightLengthSquared <= kMinimumLengthSquared) {
        return false;
    }
    right /= std::sqrt(rightLengthSquared);
    const glm::vec3 correctedUp = glm::normalize(
        glm::cross(right, normalizedForward));
    if (!finiteVector(correctedUp)) {
        return false;
    }

    m_position = position;
    m_front = normalizedForward;
    m_right = right;
    m_up = correctedUp;
    m_yaw = glm::degrees(std::atan2(m_front.z, m_front.x));
    m_pitch = glm::degrees(std::atan2(
        m_front.y,
        std::sqrt(m_front.x * m_front.x + m_front.z * m_front.z)));
    fov = verticalFovDegrees;
    return true;
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

float Camera::getYaw() const {
    return m_yaw;
}

float Camera::getPitch() const {
    return m_pitch;
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

void Camera::setYawPitch(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
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
