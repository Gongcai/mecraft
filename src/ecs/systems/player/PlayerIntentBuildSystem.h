#ifndef MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
#define MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H

#include "../../GameplayRegistry.h"
#include "../../components/Components.h"

namespace ecs {

class PlayerIntentBuildSystem {
public:
    /// Read InputFrameState from registry context, compute world-space intent,
    /// and write into the local player entity's intent components.
    static void update(GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
