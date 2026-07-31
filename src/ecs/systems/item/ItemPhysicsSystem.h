#ifndef MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
#define MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ItemPhysicsSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<DropItemTag, TransformComponent, BoundsComponent, GroundedStateComponent>,
        std::tuple<TransformComponent, VelocityComponent, SpinVisualComponent, GroundedStateComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_ITEM_PHYSICS_SYSTEM_H
