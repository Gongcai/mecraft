#ifndef MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
#define MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Read InputFrameState from registry context, compute world-space intent,
/// and write into the local player entity's intent components.
class PlayerIntentBuildSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, CameraStateComponent>,
        std::tuple<MoveIntentComponent, LookIntentComponent, HotbarIntentComponent, BlockActionIntentComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_INTENT_BUILD_SYSTEM_H
