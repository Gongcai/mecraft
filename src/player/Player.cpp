//
// Created by Caiwe on 2026/3/21.
//

#include "Player.h"
#include <iostream>

void Player::init(const glm::vec3 &spawnPos) {
    m_position = spawnPos;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false; // Initially in air
    m_camera.setPosition(getEyePosition());
}

void Player::update(float dt, const InputSnapshot &snapshot, const InputContextManager& inputContext) {
    // Rely on Game loop to ensure we only update when appropriate context is active
    // OR verify context ourselves.
    // If we are called, we should check input. Context Manager handles if input is valid for us.
    handleMouseLook(inputContext);
    handleMovement(dt, inputContext);
}

glm::vec3 Player::getPosition() const {
    return m_position;
}

glm::vec3 Player::getEyePosition() const {
    return m_position + glm::vec3(0.0f, m_eyeHeight, 0.0f);
}

Camera &Player::getCamera() {
    return m_camera;
}

void Player::handleMovement(float dt, const InputContextManager& inputContext) {

    // 基础移动方向：基于摄像机水平朝向
    glm::vec3 front = m_camera.getFront();
    glm::vec3 right = m_camera.getRight();

    // 扁平化到XZ平面（忽略Y轴，除非是飞行模式，但这里模拟行走）
    front.y = 0.0f;
    right.y = 0.0f;
    if (glm::length(front) > 0.001f) front = glm::normalize(front);
    if (glm::length(right) > 0.001f) right = glm::normalize(right);

    // 一步到位抓取二维意图方向，再也不用一堆 If-Else
    float forwardInput = inputContext.getAxisValue(Axis::Vertical);
    float rightInput   = inputContext.getAxisValue(Axis::Horizontal);

    glm::vec3 moveDir = front * forwardInput + right * rightInput;

    if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);

    // 简单的速度控制
    m_sprinting = inputContext.isActionTriggered(Action::Sprint);
    float currentSpeed = m_sprinting ? m_sprintSpeed : m_walkSpeed;

    // 目前没有 World 类处理碰撞和物理， temporarily implement simple free-cam flight for debugging
    // This allows moving up/down with Space/Shift
    // Using Jump action for Up, Sprint or Crouch for Down?
    // Current code used space/left_shift.
    // Jump -> Space. Crouch -> Shift? OR Sprint -> Control.
    // Binding for Crouch? I didn't add Crouch binding to default list.
    // Let's assume Jump is Up. And I need a Down action.
    // "Crouch" action exists in enum. I should bind it.

    if (inputContext.isActionTriggered(Action::Jump)) {
        m_position.y += currentSpeed * dt;
    }
    // Shift was used for down. Sprint is usually shift in many games, but here it was Control.
    // Let's bind Crouch to Shift and use it for Down.
    if (inputContext.isActionTriggered(Action::Crouch)) {
        m_position.y -= currentSpeed * dt;
    }

    m_position += moveDir * currentSpeed * dt;

    // 同步摄像机位置
    m_camera.setPosition(getEyePosition());
}

void Player::handleMouseLook(const InputContextManager &inputContext) {
    float deltaX = inputContext.getAxisValue(Axis::LookX);
    float deltaY = inputContext.getAxisValue(Axis::LookY);
    // 使用 snapshot 中的 mouseDelta
    // 注意：InputManager 应该负责处理灵敏度，或者我们在 Camera 中处理
    // Camera::processMouseMovement 接受 offset
    m_camera.processMouseMovement(deltaX, -deltaY);
}
