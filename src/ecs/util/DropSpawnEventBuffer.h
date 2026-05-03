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

/// Drop spawn event bus — use EventBus<DropSpawnRequestEvent> directly.
using DropSpawnEventBus = EventBus<DropSpawnRequestEvent>;

/// Helper: ensure a DropSpawnEventBus exists in the registry context and return a reference.
inline DropSpawnEventBus& ensureDropSpawnEventBus(GameplayRegistry& registry) {
    return ensureEventBus<DropSpawnRequestEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_DROP_SPAWN_EVENT_BUFFER_H
