//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_PLAYER_H
#define MECRAFT_PLAYER_H
#include <glm/glm.hpp>
#include "../core/Camera.h"
#include "../core/InputManager.h"
#include "../core/InputContextManager.h" // Add this
#include "../physics/PhysicsInfo.h"
#include "Inventory.h"

namespace physics {
class PhysicsSystem;
}

class Player {
public:
    void init(const glm::vec3& spawnPos);
    Inventory& getInventory();
    [[nodiscard]] const Inventory& getInventory() const;
    void update(float dt, const InputSnapshot& snapshot, const InputContextManager& inputContext,
                physics::PhysicsSystem& physicsSystem);

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getEyePosition() const;
    [[nodiscard]] float getEyeHeight() const;
    [[nodiscard]] float getEyeBobAmplitude() const;
    [[nodiscard]] float getEyeBobHorizontalAmplitude() const;
    [[nodiscard]] float getEyeBobFrequency() const;
    [[nodiscard]] float getEyeBobPhaseOffset() const;
    Camera& getCamera();
    void setEyeBobAmplitude(float amplitude);
    void setEyeBobHorizontalAmplitude(float amplitude);
    void setEyeBobFrequency(float frequency);
    void setEyeBobPhaseOffset(float radians);
    [[nodiscard]] bool wouldOverlapBlock(const glm::ivec3& blockPos) const;
    void setTargetBlock(const glm::ivec3& blockPos);
    void clearTargetBlock();
    [[nodiscard]] bool hasTargetBlock() const;
    [[nodiscard]] glm::ivec3 getTargetBlock() const;
    [[nodiscard]] bool isMoving() const;
    [[nodiscard]] bool isSprinting() const;
    [[nodiscard]] bool isJustLanded() const;
    [[nodiscard]] bool isFullySubmerged() const;
    [[nodiscard]] bool isEyesInWater() const;

private:
    glm::vec3 m_position{};
    glm::vec3 m_velocity = {0.0f, 0.0f, 0.0f};
    Camera m_camera;

    float m_eyeHeightBase = 1.62f;
    float m_eyeHeight = 1.62f;
    float m_eyeHeightStand = 1.62f;
    float m_eyeHeightCrouch = 1.f;
    float m_eyeBobAmplitude = 0.25f;
    float m_eyeBobHorizontalAmplitude = 0.02f;
    float m_eyeBobFrequency = 6.0f;
    float m_eyeBobPhaseOffset = 0.0f;
    float m_eyeBobBlend = 0.0f;
    float m_eyeBobFadeInSpeed = 10.0f;
    float m_eyeBobFadeOutSpeed = 8.0f;
    float m_playerWidth = 0.6f;
    float m_playerHeight = 1.8f;
    bool m_onGround = false;
    bool m_onGroundLastFrame = false;
    bool m_justLanded = false;

    bool m_sprinting = false;

    bool m_moving = true;
    PhysicsBody m_body{};
    MoveIntent m_intent{};

    bool m_hasTargetBlock = false;
    glm::ivec3 m_targetBlock{};
    Inventory m_inventory;

    void handleMovement(const InputContextManager& inputContext);
    void handleMouseLook(const InputContextManager& inputContext);
    void applyViewBob(float dt);
};

#endif //MECRAFT_PLAYER_H