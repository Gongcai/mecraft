#ifndef MECRAFT_ECS_SYSTEM_CONTEXT_H
#define MECRAFT_ECS_SYSTEM_CONTEXT_H

#include "GameplayRegistry.h"
#include "GameplayServices.h"

namespace ecs {

/// Unified context passed to every ISystem::update() call.
/// Bundles the registry, external service pointers, and per-frame timing.
struct SystemContext {
    GameplayRegistry& registry;
    GameplayServices& services;
    float dt = 0.0f;
};

} // namespace ecs

#endif // MECRAFT_ECS_SYSTEM_CONTEXT_H
