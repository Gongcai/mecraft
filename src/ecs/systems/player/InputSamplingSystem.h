#ifndef MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H
#define MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H

#include "../../GameplayRegistry.h"
#include "../../util/InputFrameState.h"

class InputContextManager;

namespace ecs {

class InputSamplingSystem {
public:
    /// Sample all actions and axes from InputContextManager and write into registry context.
    static void update(GameplayRegistry& registry, const InputContextManager& inputCtx);
};

} // namespace ecs

#endif // MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H
