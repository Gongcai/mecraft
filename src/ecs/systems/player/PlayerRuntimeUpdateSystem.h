#ifndef MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
#define MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class PlayerRuntimeUpdateSystem {
public:
    /// Local-player-only runtime polish layered on top of generic ECS character physics.
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
