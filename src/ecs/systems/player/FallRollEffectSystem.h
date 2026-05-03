#ifndef MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H
#define MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H

#include "../../GameplayRegistry.h"

class Player;

namespace ecs {

class FallRollEffectSystem {
public:
    /// Consume classic hurt effect trigger and update fall-roll animation state.
    static void update(GameplayRegistry& registry, Player& player, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H
