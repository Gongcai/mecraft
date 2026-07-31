#ifndef MECRAFT_ECS_FALL_DAMAGE_SYSTEM_H
#define MECRAFT_ECS_FALL_DAMAGE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Apply fall damage when the player lands with sufficient impact speed.
class FallDamageSystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<LocalPlayerTag, LandingStateComponent>,
                                          std::tuple<HealthComponent, HurtEffectComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_FALL_DAMAGE_SYSTEM_H
