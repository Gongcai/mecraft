#ifndef MECRAFT_ECS_BUCKET_USE_SYSTEM_H
#define MECRAFT_ECS_BUCKET_USE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Handles bucket item interactions with source water and water placement.
class BucketUseSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, BlockActionIntentComponent, TransformComponent, BlockTargetComponent>,
        std::tuple<InventoryComponent, InventoryDataComponent, BlockInteractionRuntimeComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BUCKET_USE_SYSTEM_H
