#ifndef MECRAFT_ECS_FALLING_BLOCK_EVENT_BUFFER_H
#define MECRAFT_ECS_FALLING_BLOCK_EVENT_BUFFER_H

#include <glm/glm.hpp>

#include "EventBus.h"
#include "../../world/block/Block.h"

namespace ecs {

/// Emitted by BlockSupportSystem when a gravity-affected block loses support.
/// Consumed by FallingBlockSpawnSystem (tick pipeline) which spawns a falling entity.
struct FallingBlockSpawnEvent {
    BlockID blockId = 0;
    glm::ivec3 blockPos{};
};

using FallingBlockSpawnEventBus = EventBus<FallingBlockSpawnEvent>;

inline FallingBlockSpawnEventBus& ensureFallingBlockSpawnEventBus(GameplayRegistry& registry) {
    return ensureEventBus<FallingBlockSpawnEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_FALLING_BLOCK_EVENT_BUFFER_H
