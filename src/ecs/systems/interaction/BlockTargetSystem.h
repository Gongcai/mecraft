#ifndef MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H
#define MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Raycast from the player's eye to update the targeted block each frame.
class BlockTargetSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_TARGET_SYSTEM_H
