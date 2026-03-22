//
// Created by Caiwe on 2026/3/21.
//

#ifndef MECRAFT_PLAYER_H
#define MECRAFT_PLAYER_H
#include <glm/glm.hpp>
#include "../core/Camera.h"
#include "../core/InputManager.h"
#include "../core/InputContextManager.h" // Add this

class Player {
public:
    void init(const glm::vec3& spawnPos);
    // Update signature change: add InputContextManager
    void update(float dt, const InputSnapshot& snapshot, const InputContextManager& inputContext);

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getEyePosition() const;   // position + {0, eyeHeight, 0}
    Camera& getCamera();

private:
    glm::vec3 m_position;
    glm::vec3 m_velocity = {0, 0, 0};
    Camera    m_camera;

    float m_eyeHeight   = 1.62f;   // Minecraft 标准眼高
    float m_playerWidth = 0.6f;
    float m_playerHeight = 1.8f;

    float m_walkSpeed  = 4.317f;   // 方块/秒
    float m_sprintSpeed = 5.612f;
    float m_jumpForce  = 8.0f;

    bool  m_onGround = false;
    bool  m_sprinting = false;


    void handleMovement(float dt, const InputSnapshot &snapshot, const InputContextManager& inputContext);
    void handleMouseLook(const InputSnapshot& snapshot);
};

#endif //MECRAFT_PLAYER_H