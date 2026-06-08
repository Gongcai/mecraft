#ifndef MECRAFT_ECS_PLAYER_MELEE_SYSTEM_H
#define MECRAFT_ECS_PLAYER_MELEE_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

class PlayerMeleeSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, TransformComponent, CameraStateComponent, BlockActionIntentComponent, MeleeAttackComponent>,
        std::tuple<MeleeAttackComponent, BlockActionIntentComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_MELEE_SYSTEM_H
