#ifndef MECRAFT_ECS_STEVE_COMPONENTS_H
#define MECRAFT_ECS_STEVE_COMPONENTS_H

#include <cstdint>

#include <glm/glm.hpp>

namespace ecs {

struct SkinTypeComponent {
    enum class Type : uint8_t { Player, Mob };
    Type type = Type::Player;
};

struct MobAIComponent {
    float wanderTimer = 0.0f;
    float wanderInterval = 3.0f;
    glm::vec2 wanderDir{0.0f};
    float wanderSpeed = 1.0f;
    float yaw = 0.0f;
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
