#ifndef MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H
#define MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H

#include <glm/glm.hpp>

#include "EventBus.h"
#include "../../world/block/Block.h"

namespace ecs {

struct BlockBreakParticleEvent {
    glm::ivec3 blockPos{};
    BlockID blockType = 0;
    glm::vec3 worldPos{0.0f};
    bool useWorldPos = false;
    int particleCount = 24;
    float spread = 0.6f;
    float velocityScale = 1.0f;
    float minLife = 0.4f;
    float maxLife = 0.8f;
    float minSize = 0.06f;
    float maxSize = 0.14f;
};

inline BlockBreakParticleEvent makeImpactParticleEvent(const glm::vec3& worldPos, const BlockID blockType,
                                                       const int particleCount = 14) {
    BlockBreakParticleEvent event;
    event.blockType = blockType;
    event.worldPos = worldPos;
    event.useWorldPos = true;
    event.particleCount = particleCount;
    event.spread = 0.18f;
    event.velocityScale = 0.85f;
    event.minLife = 0.22f;
    event.maxLife = 0.48f;
    event.minSize = 0.04f;
    event.maxSize = 0.095f;
    return event;
}

/// Particle event bus — use EventBus<BlockBreakParticleEvent> directly.
using ParticleEventBus = EventBus<BlockBreakParticleEvent>;

/// Helper: ensure a ParticleEventBus exists in the registry context and return a reference.
inline ParticleEventBus& ensureParticleEventBus(GameplayRegistry& registry) {
    return ensureEventBus<BlockBreakParticleEvent>(registry);
}

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_EVENT_BUFFER_H
