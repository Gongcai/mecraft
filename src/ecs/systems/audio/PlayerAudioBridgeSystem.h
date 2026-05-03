#ifndef MECRAFT_ECS_PLAYER_AUDIO_BRIDGE_SYSTEM_H
#define MECRAFT_ECS_PLAYER_AUDIO_BRIDGE_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class PlayerAudioBridgeSystem {
public:
    /// Transitional bridge system:
    /// keeps legacy movement/landing audio behavior while logic migrates out of GameplayState.
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_AUDIO_BRIDGE_SYSTEM_H
