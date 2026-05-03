#include "MobAnimationSystem.h"

#include "../../components/Components.h"
#include "../HumanoidAnimationHelper.h"

namespace ecs {

void MobAnimationSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    if (dt <= 0.0f) return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, SteveAnimationStateComponent, ChildrenComponent>();

    for (auto entity : view) {
        auto& anim = reg.get<SteveAnimationStateComponent>(entity);

        float torsoYaw = 0.0f;
        if (reg.all_of<MobAIComponent>(entity)) {
            torsoYaw = reg.get<MobAIComponent>(entity).yaw;
        }

        // Mob head stays upright — no pitch
        HumanoidAnimationHelper::update(reg, entity, anim, dt, torsoYaw, 0.0f);
    }
}

} // namespace ecs
