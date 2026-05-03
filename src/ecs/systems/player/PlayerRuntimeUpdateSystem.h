#ifndef MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
#define MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H

#include "../../ISystem.h"

namespace ecs {

/// Local-player-only runtime polish layered on top of generic ECS character physics.
class PlayerRuntimeUpdateSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;
};

} // namespace ecs

#endif // MECRAFT_ECS_PLAYER_RUNTIME_UPDATE_SYSTEM_H
