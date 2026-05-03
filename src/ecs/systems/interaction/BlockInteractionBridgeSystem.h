#ifndef MECRAFT_ECS_BLOCK_INTERACTION_BRIDGE_SYSTEM_H
#define MECRAFT_ECS_BLOCK_INTERACTION_BRIDGE_SYSTEM_H

#include "../../GameplayRegistry.h"

class World;
class DropSystem;
class UIRenderer;

namespace ecs {

class BlockInteractionBridgeSystem {
public:
    /// Transitional bridge system:
    /// migrates block targeting/break/place behavior out of GameplayState into ECS fixed update chain.
    static void update(GameplayRegistry& registry,
                       World& world,
                       DropSystem& dropSystem,
                       UIRenderer& uiRenderer,
                       float dt);
};

} // namespace ecs

#endif // MECRAFT_ECS_BLOCK_INTERACTION_BRIDGE_SYSTEM_H
