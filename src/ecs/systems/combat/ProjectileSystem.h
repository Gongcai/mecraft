#ifndef MECRAFT_ECS_PROJECTILE_SYSTEM_H
#define MECRAFT_ECS_PROJECTILE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class ProjectileSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<TransformComponent, VelocityComponent, ProjectileComponent>,
        std::tuple<TransformComponent, VelocityComponent, ProjectileThrowerComponent, InventoryDataComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PROJECTILE_SYSTEM_H
