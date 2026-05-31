#ifndef MECRAFT_GAMEPLAY_STATE_ECS_BRIDGE_H
#define MECRAFT_GAMEPLAY_STATE_ECS_BRIDGE_H

class Inventory;

namespace ecs {
class GameplayRegistry;
}

/// Encapsulates ECS component mutations needed by GameplayState.
class GameplayStateEcsBridge {
public:
    static void syncSelectedHotbarSlot(ecs::GameplayRegistry& registry, const Inventory& inventory);
    static void resetBlockBreakSession(ecs::GameplayRegistry& registry);
};

#endif // MECRAFT_GAMEPLAY_STATE_ECS_BRIDGE_H
