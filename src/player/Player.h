//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_PLAYER_H
#define MECRAFT_PLAYER_H
#include <glm/glm.hpp>
#include "../core/Camera.h"
#include "../core/InputManager.h"
#include "../core/InputContextManager.h"
#include "../physics/PhysicsInfo.h"
#include "../ecs/Components.h"
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

    void updateFromECS(float dt, const ecs::MoveIntentComponent& moveIntent,
                       const ecs::LookIntentComponent& lookIntent,
                       physics::PhysicsSystem& physicsSystem);

    void syncFromECS(const ecs::TransformComponent& transform,
                     const ecs::PhysicsBodyComponent& physicsBody,
                     const ecs::CameraStateComponent& camera,
                     const ecs::MoveIntentComponent& moveIntent,
                     const ecs::BlockTargetComponent& target,
                     const ecs::BlockBreakComponent& blockBreak,
                     const ecs::LandingStateComponent& landing,
                     const ecs::InventoryComponent& inventory,
                     float dt);

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getEyePosition() const;
    [[nodiscard]] float getEyeHeight() const;
    [[nodiscard]] float getEyeBobAmplitude() const;
    [[nodiscard]] float getEyeBobHorizontalAmplitude() const;
    [[nodiscard]] float getEyeBobFrequency() const;
    [[nodiscard]] float getEyeBobPhaseOffset() const;
    [[nodiscard]] const PhysicsBody& getPhysicsBody() const;
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
    void setBlockBreakProgress(const glm::ivec3& blockPos, float progress01);
    void clearBlockBreakProgress();
    [[nodiscard]] bool hasBlockBreakProgress() const;
    [[nodiscard]] glm::ivec3 getBreakTargetBlock() const;
    [[nodiscard]] float getBlockBreakProgress() const;
    [[nodiscard]] bool isMoving() const;
    [[nodiscard]] bool isSprinting() const;
    [[nodiscard]] bool isJustLanded() const;
    [[nodiscard]] float getLandingImpactSpeed() const;
    [[nodiscard]] bool isFullySubmerged() const;
    [[nodiscard]] bool isEyesInWater() const;
    void triggerClassicHurtEffect();
    [[nodiscard]] bool consumeClassicHurtEffect();

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
    float m_SprintFOV = 90.0f;
    float m_WalkFOV = 75.0f;
    bool m_onGround = false;
    bool m_onGroundLastFrame = false;
    bool m_justLanded = false;
    float m_lastLandingImpactSpeed = 0.0f;
    bool m_classicHurtEffectPending = false;

    bool m_sprinting = false;
    bool m_crouching = false;
    bool m_moving = true;
    PhysicsBody m_body{};
    MoveIntent m_intent{};

    bool m_hasTargetBlock = false;
    glm::ivec3 m_targetBlock{};
    bool m_hasBreakTargetBlock = false;
    glm::ivec3 m_breakTargetBlock{};
    float m_blockBreakProgress = 0.0f;
    Inventory m_inventory;

    void handleMovement(const InputContextManager& inputContext);
    void handleMouseLook(const InputContextManager& inputContext);
    void applyViewBob(float dt);
};

#endif //MECRAFT_PLAYER_H
