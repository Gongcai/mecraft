#ifndef MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H
#define MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H

#include "../../GameplayRegistry.h"

class DropSystem;
class World;

namespace ecs {

class DropCollectionBridgeSystem {
public:
    /// Runs the registry-backed item ECS update chain inside gameplay fixed order.
    static void update(GameplayRegistry& registry,
                       DropSystem& dropSystem,
                       const World& world,
                       float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_DROP_COLLECTION_BRIDGE_SYSTEM_H
