#ifndef MECRAFT_STEVE_ANIMATION_SYSTEM_H
#define MECRAFT_STEVE_ANIMATION_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class SteveAnimationSystem {
public:
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_STEVE_ANIMATION_SYSTEM_H
