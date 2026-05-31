#ifndef MECRAFT_GAMEPLAY_STATE_CONTEXT_H
#define MECRAFT_GAMEPLAY_STATE_CONTEXT_H

class GameStateMachine;
class InputContextManager;
class InputManager;
class UIRenderer;
class Inventory;
class LocaleManager;

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
};

#endif // MECRAFT_GAMEPLAY_STATE_CONTEXT_H
