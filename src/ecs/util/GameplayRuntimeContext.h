#ifndef MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H
#define MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H

#include "../../core/states/GameplayModeRules.h"

class IGameplayModeRules;

namespace ecs {

struct GameplayRuntimeContext {
    const IGameplayModeRules* modeRules = nullptr;
    GameplayMode gameplayMode = GameplayMode::Survival;
};

} // namespace ecs

#endif // MECRAFT_ECS_GAMEPLAY_RUNTIME_CONTEXT_H
