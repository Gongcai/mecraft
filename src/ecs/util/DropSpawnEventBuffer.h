#ifndef MECRAFT_ECS_DROP_SPAWN_EVENT_BUFFER_H
#define MECRAFT_ECS_DROP_SPAWN_EVENT_BUFFER_H

#include <glm/glm.hpp>

#include "EventBus.h"
#include "../../world/Block.h"

namespace ecs {

struct DropSpawnRequestEvent {
    BlockID blockId = 0;
    glm::ivec3 blockPos{};
};

/// Backward-compatible alias.  New code should prefer EventBus<DropSpawnRequestEvent>.
struct DropSpawnEventBuffer {
    std::vector<DropSpawnRequestEvent> spawnRequests;
};

inline DropSpawnEventBuffer& ensureDropSpawnEventBuffer(GameplayRegistry& registry) {
    if (!registry.ctxHas<DropSpawnEventBuffer>()) {
        registry.ctxSet<DropSpawnEventBuffer>();
    }
    return registry.ctxGet<DropSpawnEventBuffer>();
}

} // namespace ecs

#endif // MECRAFT_ECS_DROP_SPAWN_EVENT_BUFFER_H
