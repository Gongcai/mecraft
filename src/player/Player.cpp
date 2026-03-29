//
// Created by Caiwe on 2026/3/21.
//

#include "Player.h"

#include "../physics/PhysicsSystem.h"

void Player::init(const glm::vec3 &spawnPos) {
    m_position = spawnPos;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false;
    m_sprinting = false;

    m_body.position = spawnPos;
    m_body.velocity = glm::vec3(0.0f);
    m_body.halfExtents = glm::vec3(m_playerWidth * 0.5f, m_playerHeight * 0.5f, m_playerWidth * 0.5f);
    m_body.colliderOffset = glm::vec3(0.0f, m_playerHeight * 0.5f, 0.0f);

    m_camera.setPosition(getEyePosition());
}

void Player::update(float dt, const InputSnapshot &snapshot, const InputContextManager &inputContext,
                    physics::PhysicsSystem &physicsSystem) {
    (void) snapshot;
    handleMouseLook(inputContext);
    handleMovement(inputContext);

    physicsSystem.updateBody(m_body, m_intent, dt);

    m_position = m_body.position;
    m_velocity = m_body.velocity;
    m_onGround = m_body.isGrounded;
    m_camera.setPosition(getEyePosition());
}

glm::vec3 Player::getPosition() const {
    return m_body.position;
}

glm::vec3 Player::getEyePosition() const {
    return m_body.position + glm::vec3(0.0f, m_eyeHeight, 0.0f);
}

Camera &Player::getCamera() {
    return m_camera;
}

bool Player::wouldOverlapBlock(const glm::ivec3& blockPos) const {
    const glm::vec3 bodyCenter = m_body.position + m_body.colliderOffset;
    const glm::vec3 bodyMin = bodyCenter - m_body.halfExtents;
    const glm::vec3 bodyMax = bodyCenter + m_body.halfExtents;

    const glm::vec3 blockMin(static_cast<float>(blockPos.x),
                             static_cast<float>(blockPos.y),
                             static_cast<float>(blockPos.z));
    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f, 1.0f, 1.0f);

    return bodyMin.x < blockMax.x && bodyMax.x > blockMin.x &&
           bodyMin.y < blockMax.y && bodyMax.y > blockMin.y &&
           bodyMin.z < blockMax.z && bodyMax.z > blockMin.z;
}

void Player::setTargetBlock(const glm::ivec3& blockPos) {
    m_targetBlock = blockPos;
    m_hasTargetBlock = true;
}

void Player::clearTargetBlock() {
    m_hasTargetBlock = false;
}

bool Player::hasTargetBlock() const {
    return m_hasTargetBlock;
}

glm::ivec3 Player::getTargetBlock() const {
    return m_targetBlock;
}

void Player::handleMovement(const InputContextManager &inputContext) {
    glm::vec3 front = m_camera.getFront();
    glm::vec3 right = m_camera.getRight();

    front.y = 0.0f;
    right.y = 0.0f;

    if (glm::length(front) > 0.001f) {
        front = glm::normalize(front);
    }
    if (glm::length(right) > 0.001f) {
        right = glm::normalize(right);
    }

    const float forwardInput = inputContext.getAxisValue(Axis::Vertical);
    const float rightInput = inputContext.getAxisValue(Axis::Horizontal);

    glm::vec3 wishDir = front * forwardInput + right * rightInput;
    if (glm::length(wishDir) > 0.001f) {
        wishDir = glm::normalize(wishDir);
    }
    if (inputContext.isActionTriggered(Action::Crouch)) {
        m_eyeHeight = m_crouchEyeHeight;
    }
    else {
        m_eyeHeight = m_standingEyeHeight;
    }


    m_sprinting = inputContext.isActionTriggered(Action::Sprint);

    // PhysicsSystem currently expects world-space X/Z intent.
    m_intent.move = glm::vec2(wishDir.x, wishDir.z);
    m_intent.wantsJump = inputContext.isActionTriggered(Action::Jump);
    m_intent.wantsSprint = m_sprinting;
}

void Player::handleMouseLook(const InputContextManager &inputContext) {
    const float deltaX = inputContext.getAxisValue(Axis::LookX);
    const float deltaY = inputContext.getAxisValue(Axis::LookY);
    m_camera.processMouseMovement(deltaX, deltaY);
}
