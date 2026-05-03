#ifndef MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
#define MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Read InputFrameState from registry context, compute world-space intent,
/// and write into the local player entity's intent components.
class PlayerIntentBuildSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
