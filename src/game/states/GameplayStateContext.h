#ifndef MECRAFT_GAMEPLAY_STATE_CONTEXT_H
#define MECRAFT_GAMEPLAY_STATE_CONTEXT_H

class GameStateMachine;
class InputContextManager;
class InputManager;
class UIRenderer;
class Inventory;
class LocaleManager;
class RenderScene;
class World;

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
    RenderScene* renderScene = nullptr;  ///< Optional; for in-game settings screen
    World* world = nullptr;              ///< Optional; for render distance control
};

#endif // MECRAFT_GAMEPLAY_STATE_CONTEXT_H
