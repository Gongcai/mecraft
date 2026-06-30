#ifndef MECRAFT_ECS_STEVE_COMPONENTS_H
#define MECRAFT_ECS_STEVE_COMPONENTS_H

#include <cstdint>
#include <string>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include "../entity/EntitySkinLayout.h"

namespace ecs {

struct MobAIComponent {
    enum class State : uint8_t {
        Idle,
        Wander,
        Pursue,
        Attack
    };

    State state = State::Idle;
    entt::entity target = entt::null;
    float wanderTimer = 0.0f;
    float wanderInterval = 3.0f;
    glm::vec2 wanderDir{0.0f};
    float wanderSpeed = 0.45f;
    float pursueSpeed = 0.85f;
    float acquisitionRange = 14.0f;
    float loseTargetRange = 20.0f;
    float attackRange = 1.35f;
    float attackCooldownSeconds = 1.1f;
    float attackCooldownRemaining = 0.0f;
    int attackDamage = 3;
    float yaw = 0.0f;
    bool targetsPlayers = true;
    bool retaliates = true;
    float lineOfSightMemorySeconds = 3.0f;
    float targetMemoryRemaining = 0.0f;
    float hearingRange = 16.0f;
    float stuckTimer = 0.0f;
    float stuckJumpThresholdSeconds = 0.35f;
    float jumpCooldownSeconds = 0.8f;
    float jumpCooldownRemaining = 0.0f;
    glm::vec3 lastPosition{0.0f};
    glm::vec2 avoidanceDir{0.0f};
    float avoidanceTimer = 0.0f;
    float avoidanceSeconds = 0.4f;
    float avoidanceStrength = 0.55f;
    uint64_t lastDamageSourceTickHandled = UINT64_MAX;
};

struct MobVisualComponent {
    std::string model = "humanoid";
    std::string textureKey = "zombie";
    EntitySkinLayoutKind skinLayout = EntitySkinLayoutKind::Steve64x64;
    float scale = 1.0f;
};

struct EntityModelComponent {
    std::string modelId;
    std::string animationId;
    std::string yawPartName;
};

struct EntityModelPartComponent {
    std::string partName;
};

enum class StevePartType : uint8_t {
    Torso,
    Head,
    RightArm,
    LeftArm,
    RightLeg,
    LeftLeg
};

struct StevePartComponent {
    StevePartType partType = StevePartType::Torso;
};

struct SteveAnimationStateComponent {
    float walkCyclePhase = 0.0f;
    float walkCycleSpeed = 8.0f;
    bool isWalking = false;
    bool isOnGround = true;
    glm::vec3 lastPosition{0.0f};
};

} // namespace ecs

#endif // MECRAFT_ECS_STEVE_COMPONENTS_H
