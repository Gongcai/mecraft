#ifndef MECRAFT_CREATIVEMODESTATE_H
#define MECRAFT_CREATIVEMODESTATE_H

#include "GameplayState.h"

namespace physics {
class PhysicsSystem;
}

class CreativeModeState final : public GameplayState {
public:
    CreativeModeState(GameStateMachine& fsm,
                      Player& player,
                      InputContextManager& ctx,
                      InputManager& input,
                      UIRenderer& uiRenderer,
                      std::string& lastSubmittedCommand,
                      physics::PhysicsSystem& physicsSystem,
                      World& world,
                      AudioEngine& audioEngine,
                      ParticleSystem& particleSystem,
                      DropSystem& dropSystem)
            : GameplayState(fsm,
                            player,
                            ctx,
                            input,
                            uiRenderer,
                            lastSubmittedCommand,
                            physicsSystem,
                            world,
                            audioEngine,
                            particleSystem,
                            dropSystem,
                            CreativeModeRules::instance()) {}
};

#endif //MECRAFT_CREATIVEMODESTATE_H
