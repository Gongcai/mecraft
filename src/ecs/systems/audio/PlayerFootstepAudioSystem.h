#ifndef MECRAFT_ECS_PLAYER_FOOTSTEP_AUDIO_SYSTEM_H
#define MECRAFT_ECS_PLAYER_FOOTSTEP_AUDIO_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Drives footstep and landing-impact audio events.
/// Successor of PlayerAudioBridgeSystem with the unified ISystem interface.
class PlayerFootstepAudioSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, LandingStateComponent>,
        std::tuple<FootstepStateComponent, HurtEffectComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_FOOTSTEP_AUDIO_SYSTEM_H
