#ifndef MECRAFT_ECS_REDSTONE_EVENT_BUFFER_H
#define MECRAFT_ECS_REDSTONE_EVENT_BUFFER_H

#include <cstdint>

#include <glm/vec3.hpp>

#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"
#include "EventBus.h"

namespace ecs {

struct RedstoneDeviceActivationEvent {
    glm::ivec3 position{0};
    BlockID blockId = RUNTIME_ID_NULL;
    StateID stateId = RUNTIME_ID_NULL;
    uint64_t redstoneTick = 0;
};

/// Redstone device activation bus for rising-edge triggered block behavior.
using RedstoneDeviceActivationEventBus = EventBus<RedstoneDeviceActivationEvent>;

/// Helper: ensure a RedstoneDeviceActivationEventBus exists in the registry context.
inline RedstoneDeviceActivationEventBus& ensureRedstoneDeviceActivationEventBus(GameplayRegistry& registry) {
    return ensureEventBus<RedstoneDeviceActivationEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_REDSTONE_EVENT_BUFFER_H
