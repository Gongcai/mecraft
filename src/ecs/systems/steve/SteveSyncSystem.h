#ifndef MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
#define MECRAFT_ECS_STEVE_SYNC_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Handle view mode toggle from input, and sync player position/camera to Steve entity.
class SteveSyncSystem : public ISystem {
public:
    using Dependencies =
        SystemDependency<std::tuple<LocalPlayerTag, TransformComponent, CameraStateComponent, SteveTag>,
                         std::tuple<TransformComponent, CameraStateComponent>>;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_STEVE_SYNC_SYSTEM_H
