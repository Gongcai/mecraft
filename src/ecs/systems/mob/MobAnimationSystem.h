#ifndef MECRAFT_MOB_ANIMATION_SYSTEM_H
#define MECRAFT_MOB_ANIMATION_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class MobAnimationSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<MobTag, SteveAnimationStateComponent, ChildrenComponent, TransformComponent, MobAIComponent>,
        std::tuple<SteveAnimationStateComponent, LocalTransformComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_MOB_ANIMATION_SYSTEM_H
