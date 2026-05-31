#ifndef MECRAFT_COMMAND_STATE_CONTEXT_H
#define MECRAFT_COMMAND_STATE_CONTEXT_H

#include <string>

class GameStateMachine;
class InputContextManager;
class InputManager;
class UIRenderer;
class World;
class LocaleManager;

namespace ecs {
class GameplayRegistry;
}

/// Narrow context for CommandState — only command-relevant dependencies.
struct CommandStateContext {
    GameStateMachine& fsm;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    std::string& lastSubmittedCommand;
    World& world;
    ecs::GameplayRegistry& ecsRegistry;
    const LocaleManager& localeManager;
};

#endif // MECRAFT_COMMAND_STATE_CONTEXT_H
