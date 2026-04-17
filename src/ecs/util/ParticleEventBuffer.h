#ifndef MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H
#define MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H

#include <vector>

#include <glm/glm.hpp>

#include "../GameplayRegistry.h"
#include "../../world/Block.h"

namespace ecs {

struct BlockBreakParticleEvent {
    glm::ivec3 blockPos{};
    BlockID blockType = 0;
};

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
