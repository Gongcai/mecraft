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

    std::cout << "[crafting_grid_control_test] PASS\n";
    return EXIT_SUCCESS;
}
