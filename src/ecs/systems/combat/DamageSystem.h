#ifndef MECRAFT_ECS_DAMAGE_SYSTEM_H
#define MECRAFT_ECS_DAMAGE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class DamageSystem : public ISystem {
public:
    using Dependencies =
        SystemDependency<std::tuple<HealthComponent>, std::tuple<HealthComponent, HurtEffectComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_DAMAGE_SYSTEM_H
