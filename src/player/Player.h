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

namespace physics {
class PhysicsSystem;
}

class Player {
public:
    void init(const glm::vec3& spawnPos);
    void update(float dt, const InputSnapshot& snapshot, const InputContextManager& inputContext,
                physics::PhysicsSystem& physicsSystem);

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getEyePosition() const;
    Camera& getCamera();
    [[nodiscard]] bool wouldOverlapBlock(const glm::ivec3& blockPos) const;

private:
    glm::vec3 m_position{};
    glm::vec3 m_velocity = {0.0f, 0.0f, 0.0f};
    Camera m_camera;

    float m_eyeHeight = 1.62f;
    float m_playerWidth = 0.6f;
    float m_playerHeight = 1.8f;

    bool m_onGround = false;
    bool m_sprinting = false;

    PhysicsBody m_body{};
    MoveIntent m_intent{};

    void handleMovement(const InputContextManager& inputContext);
    void handleMouseLook(const InputContextManager& inputContext);
};

#endif //MECRAFT_PLAYER_H