#ifndef MECRAFT_ECS_VIEW_BOB_SYSTEM_H
#define MECRAFT_ECS_VIEW_BOB_SYSTEM_H

#include "../../GameplayRegistry.h"

namespace ecs {

class ViewBobSystem {
public:
    /// Update view bob blend and offsets based on movement state.
    static void update(GameplayRegistry& registry, float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_VIEW_BOB_SYSTEM_H
