#ifndef MECRAFT_STATE_DEPENDENCIES_H
#define MECRAFT_STATE_DEPENDENCIES_H

#include <string>

class GameStateMachine;
class Inventory;
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

class LocaleManager;

struct StateDependencies {
    GameStateMachine& fsm;
    Inventory& inventory;
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
    const LocaleManager& localeManager;
};

#endif // MECRAFT_STATE_DEPENDENCIES_H
