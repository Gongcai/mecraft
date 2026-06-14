#ifndef MECRAFT_ECS_FALLING_BLOCK_SPAWN_SYSTEM_H
#define MECRAFT_ECS_FALLING_BLOCK_SPAWN_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Tick-rate system that consumes FallingBlockSpawnEvent events emitted by
/// BlockSupportSystem and spawns falling-block entities into the registry.
class FallingBlockSpawnSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_FALLING_BLOCK_SPAWN_SYSTEM_H
