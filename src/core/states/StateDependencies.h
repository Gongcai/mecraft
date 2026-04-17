#ifndef MECRAFT_STATE_DEPENDENCIES_H
#define MECRAFT_STATE_DEPENDENCIES_H

#include <string>

class GameStateMachine;
class Player;
class InputContextManager;
class InputManager;
class UIRenderer;
class World;
class AudioEngine;
class ParticleSystem;
class DropSystem;

namespace physics {
class PhysicsSystem;
}

namespace ecs {
class GameplayRegistry;
}

struct StateDependencies {
    GameStateMachine& fsm;
    Player& player;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    std::string& lastSubmittedCommand;
    physics::PhysicsSystem& physicsSystem;
    World& world;
    AudioEngine& audioEngine;
    ParticleSystem& particleSystem;
    DropSystem& dropSystem;
    ecs::GameplayRegistry& ecsRegistry;
};

#endif // MECRAFT_STATE_DEPENDENCIES_H
