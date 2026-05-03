#ifndef MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
#define MECRAFT_ECS_STEVE_SYNC_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Handle view mode toggle from input, and sync player position/camera to Steve entity.
class SteveSyncSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
