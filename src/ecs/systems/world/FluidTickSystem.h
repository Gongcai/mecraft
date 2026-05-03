#ifndef MECRAFT_ECS_FLUID_TICK_SYSTEM_H
#define MECRAFT_ECS_FLUID_TICK_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

class FluidTickSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Legacy entry point — still available for direct calls.
    static void update(class World& world, uint64_t currentTick, uint32_t budget = 4096);
};

} // namespace ecs

#endif // MECRAFT_ECS_FLUID_TICK_SYSTEM_H
