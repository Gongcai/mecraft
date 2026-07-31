//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_PHYSICS_INFO_H
#define MECRAFT_PHYSICS_INFO_H

#include <glm/glm.hpp>

enum class RayHitKind { None, Block, Fluid };

struct PhysicsInfo {
    glm::vec3 origin; // Ray start position in world space.
    glm::vec3 direction; // Normalized ray direction in world space.

    PhysicsInfo(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d)) {}
};

struct RayHit {
    bool hit = false;
    RayHitKind kind = RayHitKind::None;
    glm::ivec3 blockPos{};
    glm::ivec3 normal{}; // Surface normal used to compute adjacent placement positions.
    glm::vec3 position{};
    float distance = 0.0f;
};

struct PhysicsBody {
    glm::vec3 position{}; // Body origin in world space.
    glm::vec3 velocity{}; // Body velocity in meters per second.
    glm::vec3 halfExtents{0.3f, 0.9f, 0.3f};
    glm::vec3 colliderOffset{}; // Collision box center offset from position.
    float eyeOffsetY = 1.62f; // Eye height relative to position.

    bool isGrounded = false;
    float landingImpactSpeed = 0.0f; // Absolute vertical speed at latest ground contact.
    bool isInWater = false;
    bool hitWall = false;
    bool isFullySubmerged = false;
    bool isEyesInWater = false;
};

struct MoveIntent {
    glm::vec2 move{}; // x=strafe, y=forward, both in [-1, 1].
    bool wantsJump = false;
    bool wantsSprint = false;
    bool wantsCrouch = false;
    bool isFlying = false;
};

struct PhysicsTuning {
    float gravity = 20.0f;
    float jumpSpeed = 8.5f;
    float moveSpeed = 4.5f;
    float stepHeight = 0.6f;
    float sprintMultiplier = 1.3f;
    float groundAcceleration = 45.0f;
    float airAcceleration = 14.0f;
    float waterAcceleration = 18.0f;
    float flyingAcceleration = 40.0f;
    float airControl = 0.35f;
    float groundFriction = 10.0f;
    float groundDamping = 0.0f;
    float airDrag = 1.0f;
    float flyingDrag = 4.0f;
    float terminalVelocity = 30.0f;

    float waterGravityScale = 0.25f;
    float waterDrag = 6.0f;
    float swimSpeed = 3.2f;
    float swimUpAccel = 10.0f;
    float waterLedgeStepHeight = 1.1f;
    float waterFlowPush = 18.0f;
};

#endif // MECRAFT_PHYSICS_INFO_H
