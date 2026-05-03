#ifndef MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H
#define MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H

#include <glm/glm.hpp>

#include "EventBus.h"
#include "../../world/Block.h"

namespace ecs {

struct BlockBreakParticleEvent {
    glm::ivec3 blockPos{};
    BlockID blockType = 0;
};

/// Backward-compatible alias.  New code should prefer EventBus<BlockBreakParticleEvent>.
struct ParticleEventBuffer {
    std::vector<BlockBreakParticleEvent> blockBreakEvents;
};

inline ParticleEventBuffer& ensureParticleEventBuffer(GameplayRegistry& registry) {
    if (!registry.ctxHas<ParticleEventBuffer>()) {
        registry.ctxSet<ParticleEventBuffer>();
    }
    return registry.ctxGet<ParticleEventBuffer>();
}

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H
