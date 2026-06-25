#include "FluidTickSystem.h"

#include "../../../world/World.h"

namespace ecs {

void FluidTickSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) return;
    if (ctx.services.gameClient) return;
    update(*ctx.services.world, ctx.tickIndex);
}

void FluidTickSystem::update(World& world, const uint64_t currentTick, const uint32_t budget) {
    world.fluidSystem().processScheduledBlockTicks(currentTick, budget);
}

} // namespace ecs
