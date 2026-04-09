//
// Created by Caiwe on 2026/3/21.
//

#include "Player.h"

#include <algorithm>
#include <cmath>

#include "../core/Time.h"
#include "../physics/PhysicsSystem.h"

namespace {
    constexpr float kPi = 3.14159265358979323846f;

    float Lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    glm::vec3 Lerp(glm::vec3 a, glm::vec3 b, float t) {
        return a + (b - a) * t;
    }

    float WrapRadians(float radians) {
        const float twoPi = 2.0f * kPi;
        float wrapped = std::fmod(radians + kPi, twoPi);
        if (wrapped < 0.0f) {
            wrapped += twoPi;
        }
        return wrapped - kPi;
    }
}



void Player::init(const glm::vec3 &spawnPos) {
    m_position = spawnPos;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false;
    m_sprinting = false;
    m_eyeHeightBase = m_eyeHeightStand;
    m_eyeHeight = m_eyeHeightBase;
    m_eyeBobBlend = 0.0f;

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

    m_onGroundLastFrame = m_onGround;

    m_position = m_body.position;
    m_velocity = m_body.velocity;
    m_onGround = m_body.isGrounded;

    if (m_onGround && inputContext.isActionTriggered(Action::Crouch)) {
        m_eyeHeightBase = Lerp(m_eyeHeightBase, m_eyeHeightCrouch, dt * 5);
    }
    else {
        m_eyeHeightBase = Lerp(m_eyeHeightBase, m_eyeHeightStand, dt * 5);
    }
    applyViewBob(dt);

    m_justLanded = m_onGround && !m_onGroundLastFrame;

}

void Player::applyViewBob(float dt) {
    const bool shouldBob = m_moving && m_onGround;

    const float targetBlend = shouldBob ? 1.0f : 0.0f;
    const float blendSpeed = shouldBob ? m_eyeBobFadeInSpeed : m_eyeBobFadeOutSpeed;
    m_eyeBobBlend = std::clamp(Lerp(m_eyeBobBlend, targetBlend, dt * blendSpeed), 0.0f, 1.0f);

    const float phase = static_cast<float>(Time::getGameTime()) * m_eyeBobFrequency;

    const float verticalBob = m_eyeBobAmplitude * static_cast<float>(std::sin(phase)) * m_eyeBobBlend;
    const float horizontalBob = m_eyeBobHorizontalAmplitude * static_cast<float>(std::cos(phase + m_eyeBobPhaseOffset)) *
        m_eyeBobBlend;

    m_eyeHeight = m_eyeHeightBase + std::pow(verticalBob, 2.0f);

    glm::vec3 cameraPos = getEyePosition();
    glm::vec3 right = m_camera.getRight();
    right.y = 0.0f;
    if (glm::length(right) > 0.001f) {
        right = glm::normalize(right);
    } else {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    cameraPos += right * horizontalBob;
    m_camera.setPosition(cameraPos);
}

glm::vec3 Player::getPosition() const {
    return m_body.position;
}

glm::vec3 Player::getEyePosition() const {
    return m_body.position + glm::vec3(0.0f, m_eyeHeight, 0.0f);
}

float Player::getEyeHeight() const {
    return m_eyeHeight;
}

float Player::getEyeBobAmplitude() const {
    return m_eyeBobAmplitude;
}

float Player::getEyeBobHorizontalAmplitude() const {
    return m_eyeBobHorizontalAmplitude;
}

float Player::getEyeBobFrequency() const {
    return m_eyeBobFrequency;
}

float Player::getEyeBobPhaseOffset() const {
    return m_eyeBobPhaseOffset;
}

Camera &Player::getCamera() {
    return m_camera;
}

void Player::setEyeBobAmplitude(float amplitude) {
    m_eyeBobAmplitude = amplitude < 0.0f ? 0.0f : amplitude;
}

void Player::setEyeBobHorizontalAmplitude(float amplitude) {
    m_eyeBobHorizontalAmplitude = amplitude < 0.0f ? 0.0f : amplitude;
}

void Player::setEyeBobFrequency(float frequency) {
    m_eyeBobFrequency = frequency < 0.0f ? 0.0f : frequency;
}

void Player::setEyeBobPhaseOffset(float radians) {
    m_eyeBobPhaseOffset = WrapRadians(radians);
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

bool Player::isMoving() const {
    return m_moving;
}

bool Player::isSprinting() const {
    return m_sprinting;
}

bool Player::isJustLanded() const  {
    return m_justLanded;
}

bool Player::isFullySubmerged() const {
    return m_body.isFullySubmerged;
}

Inventory& Player::getInventory() {
    return m_inventory;
}

const Inventory& Player::getInventory() const {
    return m_inventory;
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
    if ((forwardInput != 0.0f || rightInput != 0.0f) && m_onGround) {
        m_moving = true;
    }
    else {
        m_moving = false;
    }
    glm::vec3 wishDir = front * forwardInput + right * rightInput;
    if (glm::length(wishDir) > 0.001f) {
        wishDir = glm::normalize(wishDir);
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
