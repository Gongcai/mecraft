#include "FallingBlockSpawnSystem.h"

#include "../../entity/EntityFactory.h"
#include "../../util/FallingBlockEventBuffer.h"
#include "../../components/Components.h"

namespace ecs {

void FallingBlockSpawnSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    if (!registry.ctxHas<FallingBlockSpawnEventBus>()) return;
    auto& bus = registry.ctxGet<FallingBlockSpawnEventBus>();

    for (const auto& event : bus.events) {
        if (event.blockId == 0) continue;
        FallingBlockSpawnParams params;
        params.blockId = event.blockId;
        params.gridPosition = event.blockPos;
        EntityFactory::createFallingBlock(registry, params);
    }
    bus.clear();
}

} // namespace ecs
