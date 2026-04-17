#ifndef MECRAFT_ECS_PLAYER_FACADE_SYNC_SYSTEM_H
#define MECRAFT_ECS_PLAYER_FACADE_SYNC_SYSTEM_H

#include "../../GameplayRegistry.h"

class Player;

namespace ecs {

class PlayerFacadeSyncSystem {
public:
    /// Mirror ECS runtime state back into the legacy Player facade for UI/Renderer compatibility.
    static void update(GameplayRegistry& registry, Player& player, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_FACADE_SYNC_SYSTEM_H
