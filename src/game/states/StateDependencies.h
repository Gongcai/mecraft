#ifndef MECRAFT_STATE_DEPENDENCIES_H
#define MECRAFT_STATE_DEPENDENCIES_H

#include <string>
#include "../../world/DropSystem.h"

class GameStateMachine;
class Inventory;
class InputContextManager;
class InputManager;
class UIRenderer;
class World;
class AudioEngine;
class ParticleSystem;
namespace client {
class GameClient;
}

namespace physics {
class PhysicsSystem;
}

namespace ecs {
class GameplayRegistry;
}

class LocaleManager;
class RenderScene;
class Window;
class PresentationController;

/// Legacy state dependencies (kept for backward compatibility).
/// New states should use narrow context structs (GameplayStateContext, UIStateContext, etc.)
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
    client::GameClient& gameClient;
    bool isMultiplayer;
    RenderScene* renderScene = nullptr; ///< Optional; nullable for states that don't need render settings
    Window* window = nullptr;
    PresentationController* presentationController = nullptr;
};

#endif // MECRAFT_STATE_DEPENDENCIES_H
