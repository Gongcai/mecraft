#ifndef MECRAFT_ECS_PHYSICS_COMPONENTS_H
#define MECRAFT_ECS_PHYSICS_COMPONENTS_H

#include <glm/glm.hpp>

#include "../../physics/PhysicsInfo.h"

namespace ecs {

struct PhysicsBodyComponent {
    PhysicsBody body{};
};

struct CharacterControllerComponent {
    PhysicsTuning tuning{};
    float standEyeHeight = 1.62f;
    float crouchEyeHeight = 1.0f;
    float eyeHeightLerpSpeed = 15.0f;
    bool crouchChangesEyeHeight = true;
};

struct FlightStateComponent {
    bool isFlying = false;
};

struct VelocityComponent {
    glm::vec3 velocity{0.0f};
};

struct BoundsComponent {
    glm::vec3 halfExtents{0.0f};
};

struct GroundedStateComponent {
    bool grounded = false;
};

} // namespace ecs

#endif // MECRAFT_ECS_PHYSICS_COMPONENTS_H
