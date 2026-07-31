#ifndef MECRAFT_GAMEPLAY_STATE_CONTEXT_H
#define MECRAFT_GAMEPLAY_STATE_CONTEXT_H

#include <functional>

class GameStateMachine;
class InputContextManager;
class InputManager;
class UIRenderer;
class Inventory;
class LocaleManager;
class RenderScene;
class World;
class Window;
class PresentationController;

namespace client {
class GameClient;
}

namespace ecs {
class GameplayRegistry;
}

/// Narrow context for GameplayState — only gameplay-relevant dependencies.
struct GameplayStateContext {
    GameStateMachine& fsm;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    ecs::GameplayRegistry& ecsRegistry;
    Inventory& inventory;
    const LocaleManager& localeManager;
    client::GameClient& gameClient;
    RenderScene* renderScene = nullptr; ///< Optional; for in-game settings screen
    World* world = nullptr; ///< Optional; active world for settings and world interactions
    std::function<void(int)> renderDistanceSetter;
    bool isMultiplayer = false;
    Window* window = nullptr;
    PresentationController* presentationController = nullptr;
};

#endif // MECRAFT_GAMEPLAY_STATE_CONTEXT_H
