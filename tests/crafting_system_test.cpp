#include <cassert>
#include <iostream>
#include <vector>

#include "crafting/CraftingSystem.h"
#include "item/Item.h"

// 辅助：构造网格（行优先）
static std::vector<ItemID> makeGrid(std::initializer_list<std::initializer_list<ItemID>> rows) {
    std::vector<ItemID> grid;
    for (const auto& row : rows) {
        for (ItemID id : row) {
            grid.push_back(id);
        }
    }
    return grid;
}

static void testEmptyGrid() {
    CraftingSystem sys;
    // 空网格
    std::vector<ItemID> grid(9, ItemIds::AIR);
    auto result = sys.match(grid, 3, 3);
    assert(!result.matched);
    std::cout << "  PASS: empty grid returns no match\n";
}

static void testLoadAndMatch() {
    CraftingSystem sys;
    sys.loadRecipes("../assets/config/recipes.json");

    // 橡木原木(5) -> 橡木木板(15) x4
    {
        std::vector<ItemID> grid = {5, 0, 0, 0, 0, 0, 0, 0, 0};
        auto result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == 15);
        assert(result.count == 4);
        std::cout << "  PASS: oak log -> oak planks x4\n";
    }

    // 橡木原木放在右下角也应匹配（自动裁剪空白）
    {
        std::vector<ItemID> grid = {0, 0, 0, 0, 0, 0, 0, 0, 5};
        auto result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == 15);
        assert(result.count == 4);
        std::cout << "  PASS: oak log at corner -> oak planks x4 (trimmed)\n";
    }

    // 白桦原木(27) -> 白桦木板(17) x4
    {
        std::vector<ItemID> grid = {27, 0, 0, 0, 0, 0, 0, 0, 0};
        auto result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == 17);
        assert(result.count == 4);
        std::cout << "  PASS: birch log -> birch planks x4\n";
    }

    // 沙子(4) 2x1 -> 蓝色羊毛(28) x2
    {
        std::vector<ItemID> grid = {4, 0, 0, 4, 0, 0, 0, 0, 0};
        auto result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == 28);
        assert(result.count == 2);
        std::cout << "  PASS: sand 2x1 -> blue wool x2\n";
    }

    // 沙子(4) 2x2 -> 玻璃(6) x4
    {
        std::vector<ItemID> grid = {4, 4, 0, 4, 4, 0, 0, 0, 0};
        auto result = sys.match(grid, 3, 3);
        assert(result.matched);
        assert(result.itemId == 6);
        assert(result.count == 4);
        std::cout << "  PASS: sand 2x2 -> glass x4\n";
    }

    // 不匹配的配方
    {
        std::vector<ItemID> grid = {1, 2, 0, 0, 0, 0, 0, 0, 0};
        auto result = sys.match(grid, 3, 3);
        assert(!result.matched);
        std::cout << "  PASS: unmatched pattern returns no match\n";
    }

    // 2x2 格合成测试
    {
        std::vector<ItemID> grid2x2 = {4, 4, 4, 4};
        auto result = sys.match(grid2x2, 2, 2);
        assert(result.matched);
        assert(result.itemId == 6);
        assert(result.count == 4);
        std::cout << "  PASS: sand 2x2 grid -> glass x4\n";
    }
}

static void testNoRecipesLoaded() {
    CraftingSystem sys;
    std::vector<ItemID> grid = {5};
    auto result = sys.match(grid, 1, 1);
    assert(!result.matched);
    std::cout << "  PASS: no recipes loaded -> no match\n";
}

static void testClear() {
    CraftingSystem sys;
    sys.loadRecipes("../assets/config/recipes.json");
    assert(!sys.getRecipes().empty());
    sys.clear();
    assert(sys.getRecipes().empty());
    std::cout << "  PASS: clear removes all recipes\n";
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    std::cout << "CraftingSystem tests:\n";
    testEmptyGrid();
    testLoadAndMatch();
    testNoRecipesLoaded();
    testClear();
    std::cout << "All crafting system tests passed!\n";
    return 0;
}
