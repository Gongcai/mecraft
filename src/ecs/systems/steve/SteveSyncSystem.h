#ifndef MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
#define MECRAFT_ECS_STEVE_SYNC_SYSTEM_H

#include "../../GameplayRegistry.h"

class CameraController;

namespace ecs {

class SteveSyncSystem {
public:
    /// Handle view mode toggle from input, and sync player position/camera to Steve entity.
    static void update(GameplayRegistry& registry, CameraController& cameraController);
};

} // namespace ecs

#endif // MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
