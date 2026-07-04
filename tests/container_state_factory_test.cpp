#include <cstdlib>
#include <iostream>
#include <memory>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/engine/input/InputContextManager.h"
#include "../src/game/inventory/ContainerBehaviorRegistry.h"
#include "../src/game/inventory/ContainerStateFactory.h"
#include "../src/game/inventory/DataDrivenContainerState.h"
#include "../src/game/inventory/InventoryStateContext.h"
#include "../src/game/inventory/SmeltingContainerState.h"
#include "../src/game/inventory/WorkbenchState.h"
#include "../src/game/states/GameStateMachine.h"
#include "../src/player/ActionMap.h"
#include "../src/player/Inventory.h"
#include "../src/ui/core/UIRenderer.h"
#include "../src/ui/inventory/ContainerUiRegistry.h"
#include "../src/world/DropSystem.h"

namespace {
int fail(const char* message) {
    std::cerr << "[container_state_factory_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    ui::ContainerUiRegistry::init();
    ContainerBehaviorRegistry::init();

    GameStateMachine fsm;
    Inventory inventory;
    ActionMap actionMap;
    InputManager input;
    InputContextManager context(actionMap, input);
    UIRenderer uiRenderer;
    DropSystem dropSystem;
    ecs::GameplayRegistry registry;
    InventoryStateContext deps{
        fsm,
        inventory,
        context,
        input,
        uiRenderer,
        dropSystem,
        registry
    };

    const glm::ivec3 blockPosition(1, 64, 2);
    std::unique_ptr<IGameState> chest =
        ContainerStateFactory::create(deps, "minecraft:chest", blockPosition);
    if (chest->kind() != GameStateKind::DataDrivenContainer) {
        return fail("storage handler should create a data-driven container state");
    }

    std::unique_ptr<IGameState> barrel =
        ContainerStateFactory::create(deps, "minecraft:barrel", blockPosition);
    if (barrel->kind() != GameStateKind::DataDrivenContainer) {
        return fail("barrel storage handler should create a data-driven container state");
    }

    std::unique_ptr<IGameState> dispenser =
        ContainerStateFactory::create(deps, "minecraft:dispenser", blockPosition);
    if (dispenser->kind() != GameStateKind::DataDrivenContainer) {
        return fail("dispenser storage handler should create a data-driven container state");
    }

    std::unique_ptr<IGameState> dropper =
        ContainerStateFactory::create(deps, "minecraft:dropper", blockPosition);
    if (dropper->kind() != GameStateKind::DataDrivenContainer) {
        return fail("dropper storage handler should create a data-driven container state");
    }

    std::unique_ptr<IGameState> hopper =
        ContainerStateFactory::create(deps, "minecraft:hopper", blockPosition);
    if (hopper->kind() != GameStateKind::DataDrivenContainer) {
        return fail("hopper storage handler should create a data-driven container state");
    }

    std::unique_ptr<IGameState> furnace =
        ContainerStateFactory::create(deps, "minecraft:furnace", blockPosition);
    if (furnace->kind() != GameStateKind::SmeltingContainer) {
        return fail("smelting handler should create the smelting container state");
    }

    std::unique_ptr<IGameState> crafting =
        ContainerStateFactory::create(deps, "minecraft:crafting_table", blockPosition);
    if (crafting->kind() != GameStateKind::Workbench) {
        return fail("crafting handler should create the workbench state");
    }

    std::cout << "[container_state_factory_test] PASS\n";
    return EXIT_SUCCESS;
}
