#pragma once

class GameStateMachine;
class Inventory;
class InputContextManager;
class InputManager;
class UIRenderer;
class DropSystem;

namespace client {
class GameClient;
}
namespace ecs {
class GameplayRegistry;
}

/// Narrow context for inventory-related states and controllers.
struct InventoryStateContext {
    GameStateMachine& fsm;
    Inventory& inventory;
    InputContextManager& context;
    InputManager& input;
    UIRenderer& uiRenderer;
    DropSystem& dropSystem;
    ecs::GameplayRegistry& ecsRegistry;
    client::GameClient* gameClient = nullptr;
    bool isMultiplayer = false;
};
