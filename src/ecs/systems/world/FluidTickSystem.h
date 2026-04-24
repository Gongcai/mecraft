#ifndef MECRAFT_ECS_FLUID_TICK_SYSTEM_H
#define MECRAFT_ECS_FLUID_TICK_SYSTEM_H

#include <cstdint>

class World;

namespace ecs {

class FluidTickSystem {
public:
    static void update(World& world, uint64_t currentTick, uint32_t budget = 4096);
};

} // namespace ecs

#endif // MECRAFT_ECS_FLUID_TICK_SYSTEM_H
