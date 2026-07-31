#ifndef MECRAFT_ECS_BLOCK_PLACE_SYSTEM_H
#define MECRAFT_ECS_BLOCK_PLACE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Processes block placement: cooldown, placement-state resolution, inventory consume.
class BlockPlaceSystem : public ISystem {
public:
    using Dependencies =
        SystemDependency<std::tuple<LocalPlayerTag, BlockActionIntentComponent, MoveIntentComponent, TransformComponent,
                                    PhysicsBodyComponent, CameraStateComponent, BlockTargetComponent>,
                         std::tuple<InventoryComponent, InventoryDataComponent, BlockInteractionRuntimeComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_PLACE_SYSTEM_H
