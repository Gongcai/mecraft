#ifndef MECRAFT_ECS_DROP_RUNTIME_STATE_H
#define MECRAFT_ECS_DROP_RUNTIME_STATE_H

#include <cstddef>

#include "../GameplayRegistry.h"

namespace ecs {

struct DropRuntimeState {
    std::size_t nextId = 1;
    float mergeAccumulator = 0.0f;
};

inline DropRuntimeState& ensureDropRuntimeState(GameplayRegistry& registry) {
    if (!registry.ctxHas<DropRuntimeState>()) {
        registry.ctxSet<DropRuntimeState>();
    }
    return registry.ctxGet<DropRuntimeState>();
}

} // namespace ecs

#endif // MECRAFT_ECS_DROP_RUNTIME_STATE_H
