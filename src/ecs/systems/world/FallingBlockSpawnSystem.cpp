#include "FallingBlockSpawnSystem.h"

#include "../../entity/EntityFactory.h"
#include "../../util/FallingBlockEventBuffer.h"
#include "../../components/Components.h"

namespace ecs {

void FallingBlockSpawnSystem::update(SystemContext& ctx) {
    if (ctx.services.gameClient)
        return;
    processEvents(ctx.registry);
}

size_t FallingBlockSpawnSystem::processEvents(GameplayRegistry& registry) {
    if (!registry.ctxHas<FallingBlockSpawnEventBus>())
        return 0;
    auto& bus = registry.ctxGet<FallingBlockSpawnEventBus>();

    size_t spawnedCount = 0;
    for (const auto& event : bus.events) {
        if (event.blockId == 0)
            continue;
        FallingBlockSpawnParams params;
        params.blockId = event.blockId;
        params.gridPosition = event.blockPos;
        EntityFactory::createFallingBlock(registry, params);
        ++spawnedCount;
    }
    bus.clear();
    return spawnedCount;
}

} // namespace ecs
