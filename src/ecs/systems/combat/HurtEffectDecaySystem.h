#ifndef MECRAFT_ECS_HURT_EFFECT_DECAY_SYSTEM_H
#define MECRAFT_ECS_HURT_EFFECT_DECAY_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class HurtEffectDecaySystem : public ISystem {
public:
    using Dependencies = SystemDependency<std::tuple<HurtEffectComponent>, std::tuple<HurtEffectComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_HURT_EFFECT_DECAY_SYSTEM_H
