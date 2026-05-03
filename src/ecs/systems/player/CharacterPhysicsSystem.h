#ifndef MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H
#define MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Generic character locomotion system driven entirely by ECS components.
class CharacterPhysicsSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_CHARACTER_PHYSICS_SYSTEM_H
