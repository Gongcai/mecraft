#ifndef MECRAFT_ECS_REDSTONE_DEVICE_ACTION_SYSTEM_H
#define MECRAFT_ECS_REDSTONE_DEVICE_ACTION_SYSTEM_H

#include "../../ISystem.h"

#include <cstddef>

class World;

namespace ecs {

class GameplayRegistry;

/// Consumes redstone rising-edge device events and executes block-specific actions.
class RedstoneDeviceActionSystem : public ISystem {
public:
    void update(SystemContext& ctx) override;

    /// Execute all pending redstone device activation events.
    /// @param world  Mutable world used by devices that edit blocks or fluids.
    /// @param registry  Gameplay registry that owns device inventories, entities, and event buses.
    /// @return Number of device events that performed an action.
    static std::size_t processEvents(World& world, GameplayRegistry& registry);
};

} // namespace ecs

#endif // MECRAFT_ECS_REDSTONE_DEVICE_ACTION_SYSTEM_H
