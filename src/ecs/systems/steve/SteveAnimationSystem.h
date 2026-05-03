#ifndef MECRAFT_STEVE_ANIMATION_SYSTEM_H
#define MECRAFT_STEVE_ANIMATION_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class SteveAnimationSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<SteveTag, SteveAnimationStateComponent, ChildrenComponent, TransformComponent, CameraStateComponent>,
        std::tuple<SteveAnimationStateComponent, LocalTransformComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_STEVE_ANIMATION_SYSTEM_H
