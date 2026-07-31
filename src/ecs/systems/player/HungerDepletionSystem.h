#ifndef MECRAFT_ECS_HUNGER_DEPLETION_SYSTEM_H
#define MECRAFT_ECS_HUNGER_DEPLETION_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Deplete player food by 1 every 100 in-game seconds.
class HungerDepletionSystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<LocalPlayerTag, FoodComponent>, std::tuple<FoodComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_HUNGER_DEPLETION_SYSTEM_H
