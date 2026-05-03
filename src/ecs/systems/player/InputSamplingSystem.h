#ifndef MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H
#define MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H

#include "../../ISystem.h"

class InputContextManager;

namespace ecs {

/// Sample all actions and axes from InputContextManager and write into registry context.
class InputSamplingSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_INPUT_SAMPLING_SYSTEM_H
