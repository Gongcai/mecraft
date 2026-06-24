#include <cstdlib>
#include <iostream>

#include "Paths.h"
#include "crafting/CraftingSystem.h"
#include "item/Item.h"
#include "ui/inventory/CraftingGridControl.h"

namespace {
int fail(const char* message) {
    std::cerr << "[crafting_grid_control_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool getItemId(const char* name, ItemID& outId) {
    return ItemRegistry::tryGetIdByName(name, outId);
}
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    ItemID oakLog = 0;
    ItemID oakPlanks = 0;
    if (!getItemId("minecraft:oak_log", oakLog) ||
        !getItemId("minecraft:oak_planks", oakPlanks)) {
        return fail("expected oak log and oak planks item ids to exist");
    }

    CraftingSystem crafting;
    crafting.loadRecipes(RECIPES_CONFIG_PATH);

    CraftingGridControl grid;
    grid.setCraftingSlot(0, oakLog, 3);
    grid.updateCraftingResult(crafting);

    if (grid.getResultSlot() != oakPlanks || grid.getResultCount() != 4) {
        return fail("three logs should expose one craft result of four planks");
    }

    if (!grid.consumeOneCraft()) {
        return fail("taking the result should consume one craft");
    }
    if (grid.getCraftingSlot(0) != oakLog || grid.getCraftingSlotCount(0) != 2) {
        return fail("taking one result should consume exactly one log");
    }
    if (grid.getResultSlot() != 0 || grid.getResultCount() != 0) {
        return fail("result should be cleared until it is recalculated");
    }

    grid.updateCraftingResult(crafting);
    if (grid.getResultSlot() != oakPlanks || grid.getResultCount() != 4) {
        return fail("remaining logs should still allow another single craft");
    }

    if (!grid.consumeOneCraft()) {
        return fail("second craft should consume one log");
    }
    grid.updateCraftingResult(crafting);
    if (!grid.consumeOneCraft()) {
        return fail("third craft should consume the last log");
    }
    if (grid.getCraftingSlot(0) != 0 || grid.getCraftingSlotCount(0) != 0) {
        return fail("all logs should be consumed after three separate crafts");
    }

    grid.updateCraftingResult(crafting);
    if (grid.getResultSlot() != 0 || grid.getResultCount() != 0) {
        return fail("empty grid should not expose a result");
    }

    CraftingGridLayout workbenchLayout;
    workbenchLayout.gridSize = 3;
    grid.setLayout(workbenchLayout);
    grid.clearAll();

    ItemID cobblestone = 0;
    ItemID stick = 0;
    ItemID stonePickaxe = 0;
    if (!getItemId("minecraft:cobblestone", cobblestone) ||
        !getItemId("minecraft:stick", stick) ||
        !getItemId("minecraft:stone_pickaxe", stonePickaxe)) {
        return fail("expected stone pickaxe ingredients and result item ids to exist");
    }

    grid.setCraftingSlot(0, cobblestone, 1);
    grid.setCraftingSlot(1, cobblestone, 1);
    grid.setCraftingSlot(2, cobblestone, 1);
    grid.setCraftingSlot(4, stick, 2);
    grid.setCraftingSlot(7, stick, 2);
    grid.updateCraftingResult(crafting);

    if (grid.getResultSlotIndex() != 9 || grid.getResultSlot() != stonePickaxe || grid.getResultCount() != 1) {
        return fail("3x3 workbench grid should expose the stone pickaxe result");
    }

    if (!grid.consumeOneCraft()) {
        return fail("taking the workbench result should consume one craft");
    }
    if (grid.getCraftingSlot(4) != stick || grid.getCraftingSlotCount(4) != 1 ||
        grid.getCraftingSlot(7) != stick || grid.getCraftingSlotCount(7) != 1) {
        return fail("3x3 craft should consume one item from each occupied ingredient slot");
    }

    std::cout << "[crafting_grid_control_test] PASS\n";
    return EXIT_SUCCESS;
}
