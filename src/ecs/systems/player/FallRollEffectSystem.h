#ifndef MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H
#define MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Consume classic hurt effect trigger and update fall-roll animation state.
class FallRollEffectSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_FALL_ROLL_EFFECT_SYSTEM_H
