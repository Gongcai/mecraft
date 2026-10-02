#ifndef MECRAFT_ECS_OXYGEN_DEPLETION_SYSTEM_H
#define MECRAFT_ECS_OXYGEN_DEPLETION_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class OxygenDepletionSystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<LocalPlayerTag, TransformComponent, AirSupplyComponent>,
                                          std::tuple<AirSupplyComponent, HealthComponent, HurtEffectComponent>>;

    /// Update oxygen consumption, recovery, and drowning for local player entities.
    /// @param ctx Fixed-step system context containing the registry, world, and elapsed time.
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_OXYGEN_DEPLETION_SYSTEM_H
