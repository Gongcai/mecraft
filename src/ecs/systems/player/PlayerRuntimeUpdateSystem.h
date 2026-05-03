#ifndef MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
#define MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Local-player-only runtime polish layered on top of generic ECS character physics.
class PlayerRuntimeUpdateSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, LookIntentComponent, HotbarIntentComponent, SprintFovComponent, MoveIntentComponent, PhysicsBodyComponent>,
        std::tuple<CameraStateComponent, InventoryComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
