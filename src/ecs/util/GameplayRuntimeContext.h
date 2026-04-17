#ifndef MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H
#define MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H

class IGameplayModeRules;

namespace ecs {

struct GameplayRuntimeContext {
    const IGameplayModeRules* modeRules = nullptr;
};

} // namespace ecs

#endif // MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H
