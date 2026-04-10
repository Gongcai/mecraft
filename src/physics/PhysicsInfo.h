//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_PHYSICS_INFO_H
#define MECRAFT_PHYSICS_INFO_H

#include <glm/glm.hpp>

struct PhysicsInfo {
    glm::vec3 origin;    // 射线的起点
    glm::vec3 direction; // 射线方向（会归一化）

    PhysicsInfo(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d)) {}
};

struct RayHit {
    bool hit = false;
    glm::ivec3 blockPos{};
    glm::ivec3 normal{};   // 命中面的法线，用于计算放置位置
    float distance = 0.0f;
};

struct PhysicsBody {
    glm::vec3 position{};       // 世界坐标
    glm::vec3 velocity{};       // m/s
    glm::vec3 halfExtents{0.3f, 0.9f, 0.3f};
    glm::vec3 colliderOffset{}; // 相对 position 的碰撞盒偏移
    float eyeOffsetY = 1.62f;   // 相对 position 的眼睛高度

    bool isGrounded = false;
    bool isInWater = false;
    bool hitWall = false;
    bool isFullySubmerged = false;
    bool isEyesInWater = false;
};

struct MoveIntent {
    glm::vec2 move{}; // x=左右, y=前后, [-1, 1]
    bool wantsJump = false;
    bool wantsSprint = false;
    bool wantsCrouch = false;
};

struct PhysicsTuning {
    float gravity = 20.0f;
    float jumpSpeed = 8.5f;
    float moveSpeed = 4.5f;
    float sprintMultiplier = 1.3f; // wantsSprint 生效倍率
    float airControl = 0.35f;
    float groundFriction = 10.0f;
    float airDrag = 1.0f;
    float terminalVelocity = 30.0f;

    float waterGravityScale = 0.25f;
    float waterDrag = 6.0f;
    float swimSpeed = 3.2f;
    float swimUpAccel = 10.0f;
};

#endif // MECRAFT_PHYSICS_INFO_H
