#ifndef MECRAFT_ECS_SOIL_TILLING_SYSTEM_H
#define MECRAFT_ECS_SOIL_TILLING_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Handles hoe interactions that convert tillable soil into farmland.
class SoilTillingSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, BlockActionIntentComponent, TransformComponent, BlockTargetComponent>,
        std::tuple<InventoryComponent, InventoryDataComponent, BlockInteractionRuntimeComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_SOIL_TILLING_SYSTEM_H
