#include "FluidTickSystem.h"

#include "../../../world/World.h"

namespace ecs {

void FluidTickSystem::update(World& world, const uint64_t currentTick, const uint32_t budget) {
    world.fluidSystem().processScheduledBlockTicks(currentTick, budget);
}

} // namespace ecs
