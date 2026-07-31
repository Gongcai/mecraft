#ifndef MECRAFT_ECS_DEATH_SYSTEM_H
#define MECRAFT_ECS_DEATH_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class DeathSystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<MobTag, HealthComponent>,
                                          std::tuple<DropItemTag, TransformComponent, ItemComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_DEATH_SYSTEM_H
