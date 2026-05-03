#ifndef MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H
#define MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H

#include "../../ISystem.h"
#include "../../components/Components.h"

namespace ecs {

/// Raycast from the player's eye to update the targeted block each frame.
class BlockTargetSystem : public ISystem {
public:
    using Dependencies = SystemDependency<
        std::tuple<LocalPlayerTag, BlockActionIntentComponent, TransformComponent, CameraStateComponent>,
        std::tuple<BlockTargetComponent>
    >;

    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H
