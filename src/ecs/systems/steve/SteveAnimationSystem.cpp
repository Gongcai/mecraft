#include "SteveAnimationSystem.h"

#include "../../components/Components.h"
#include "../HumanoidAnimationHelper.h"

namespace ecs {

void SteveAnimationSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f)
        return;

    auto& reg = registry.registry();
    auto view = reg.view<SteveTag, SteveAnimationStateComponent, ChildrenComponent>();
    for (auto entity : view) {
        auto& anim = reg.get<SteveAnimationStateComponent>(entity);

        float torsoYaw = 0.0f;
        float headPitch = 0.0f;
        if (reg.all_of<CameraStateComponent>(entity)) {
            auto& cam = reg.get<CameraStateComponent>(entity);
            torsoYaw = cam.yaw;
            headPitch = cam.pitch;
        }

        HumanoidAnimationHelper::update(reg, entity, anim, dt, torsoYaw, headPitch);
    }
}

} // namespace ecs
