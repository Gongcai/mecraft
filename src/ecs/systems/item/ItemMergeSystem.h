#ifndef MECRAFT_ECS_ITEM_MERGE_SYSTEM_H
#define MECRAFT_ECS_ITEM_MERGE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ItemMergeSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<DropItemTag, DropEntityIdComponent, TransformComponent, ItemComponent, VelocityComponent,
                   LifetimeComponent, GroundedStateComponent>,
        std::tuple<TransformComponent, ItemComponent, VelocityComponent, LifetimeComponent, GroundedStateComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_MERGE_SYSTEM_H
