#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "crafting/CraftingSystem.h"
#include "item/Item.h"
#include "Paths.h"

namespace {

ItemID item(const char* name) {
    const ItemID id = ItemRegistry::findByName(name);
    assert(id != ItemIds::AIR);
    return id;
}

std::vector<ItemID> makeGrid(std::initializer_list<ItemID> cells) {
    return std::vector<ItemID>(cells);
}

void testEmptyGrid() {
    CraftingSystem sys;
    const std::vector<ItemID> grid(9, ItemIds::AIR);
    const CraftingResult result = sys.match(grid, 3, 3);
    assert(!result.matched);
    std::cout << "  PASS: empty grid returns no match\n";
}

void testLoadAndMatch() {
    CraftingSystem sys;
    sys.loadRecipes(RECIPES_CONFIG_PATH);

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:oak_log"), 0, 0,
            0, 0, 0,
            0, 0, 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:oak_planks"));
        assert(result.count == 4);
        std::cout << "  PASS: oak log -> oak planks x4\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            0, 0, 0,
            0, 0, 0,
            0, 0, item("minecraft:oak_log")
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:oak_planks"));
        assert(result.count == 4);
        std::cout << "  PASS: trimmed oak log -> oak planks x4\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:oak_planks"), 0,
            item("minecraft:oak_planks"), 0
        });
        const CraftingResult result = sys.match(grid, 2, 2);
        assert(result.matched);
        assert(result.itemId == item("minecraft:stick"));
        assert(result.count == 4);
        std::cout << "  PASS: oak planks -> sticks x4\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:cherry_planks"), 0,
            item("minecraft:cherry_planks"), 0
        });
        const CraftingResult result = sys.match(grid, 2, 2);
        assert(result.matched);
        assert(result.itemId == item("minecraft:stick"));
        assert(result.count == 4);
        std::cout << "  PASS: tagged cherry planks -> sticks x4\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:oak_planks"), item("minecraft:cherry_planks"),
            item("minecraft:birch_planks"), item("minecraft:spruce_planks")
        });
        const CraftingResult result = sys.match(grid, 2, 2);
        assert(result.matched);
        assert(result.itemId == item("minecraft:crafting_table"));
        assert(result.count == 1);
        std::cout << "  PASS: mixed tagged planks 2x2 -> crafting table\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:coal"), 0,
            item("minecraft:stick"), 0
        });
        const CraftingResult result = sys.match(grid, 2, 2);
        assert(result.matched);
        assert(result.itemId == item("minecraft:torch"));
        assert(result.count == 4);
        std::cout << "  PASS: coal + stick -> torches x4\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:cobblestone"), item("minecraft:cobblestone"), item("minecraft:cobblestone"),
            0, item("minecraft:stick"), 0,
            0, item("minecraft:stick"), 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:stone_pickaxe"));
        assert(result.count == 1);
        std::cout << "  PASS: cobblestone + sticks -> stone pickaxe\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:spruce_planks"), item("minecraft:spruce_planks"), item("minecraft:spruce_planks"),
            0, item("minecraft:stick"), 0,
            0, item("minecraft:stick"), 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:wooden_pickaxe"));
        assert(result.count == 1);
        std::cout << "  PASS: tagged spruce planks + sticks -> wooden pickaxe\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:vine"), 0, 0,
            0, item("minecraft:cobblestone"), 0,
            0, 0, 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:mossy_cobblestone"));
        assert(result.count == 1);
        std::cout << "  PASS: shapeless vine + cobblestone -> mossy cobblestone\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            0, 0, 0,
            0, item("minecraft:diamond_block"), 0,
            0, 0, 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:diamond"));
        assert(result.count == 9);
        std::cout << "  PASS: shapeless diamond block -> diamonds x9\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:wheat"), item("minecraft:wheat"), item("minecraft:wheat"),
            0, 0, 0,
            0, 0, 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == item("minecraft:bread"));
        assert(result.count == 1);
        std::cout << "  PASS: wheat -> bread\n";
    }

    {
        const std::vector<ItemID> grid = makeGrid({
            item("minecraft:dirt"), item("minecraft:stone"), 0,
            0, 0, 0,
            0, 0, 0
        });
        const CraftingResult result = sys.match(grid, 3, 3);
        assert(!result.matched);
        std::cout << "  PASS: unmatched pattern returns no match\n";
    }
}

void testInvalidGridShape() {
    CraftingSystem sys;
    sys.loadRecipes(RECIPES_CONFIG_PATH);
    const std::vector<ItemID> grid = {item("minecraft:oak_log")};
    const CraftingResult result = sys.match(grid, 2, 2);
    assert(!result.matched);
    std::cout << "  PASS: invalid grid shape returns no match\n";
}

void testNoRecipesLoaded() {
    CraftingSystem sys;
    const std::vector<ItemID> grid = {item("minecraft:oak_log")};
    const CraftingResult result = sys.match(grid, 1, 1);
    assert(!result.matched);
    std::cout << "  PASS: no recipes loaded -> no match\n";
}

void testClear() {
    CraftingSystem sys;
    sys.loadRecipes(RECIPES_CONFIG_PATH);
    assert(!sys.getRecipes().empty());
    sys.clear();
    assert(sys.getRecipes().empty());
    std::cout << "  PASS: clear removes all recipes\n";
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    std::cout << "CraftingSystem tests:\n";
    testEmptyGrid();
    testLoadAndMatch();
    testInvalidGridShape();
    testNoRecipesLoaded();
    testClear();
    std::cout << "All crafting system tests passed!\n";
    return EXIT_SUCCESS;
}
