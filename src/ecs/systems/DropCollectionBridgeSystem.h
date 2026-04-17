#ifndef MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H
#define MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H

#include "../GameplayRegistry.h"

class DropSystem;
class World;
class Player;

namespace ecs {

class DropCollectionBridgeSystem {
public:
    /// Runs the registry-backed item ECS update chain inside gameplay fixed order.
    /// Facade compatibility stays in DropSystem, while fixed-step simulation is scheduled here.
    static void update(GameplayRegistry& registry,
                       DropSystem& dropSystem,
                       const World& world,
                       Player& player,
                       float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H
