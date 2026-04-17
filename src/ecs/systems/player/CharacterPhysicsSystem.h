#ifndef MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H
#define MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace physics { class PhysicsSystem; }

namespace ecs {

class CharacterPhysicsSystem {
public:
    /// Generic character locomotion system driven entirely by ECS components.
    static void update(GameplayRegistry& registry, physics::PhysicsSystem& physicsSystem, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H
