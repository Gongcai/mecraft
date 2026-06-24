#pragma once

#include <string>
#include <vector>

#include "../item/Item.h"

struct CraftingRecipe {
    // Pattern cells are stored as [row][column]. ItemID 0 marks an empty cell.
    std::vector<std::vector<ItemID>> pattern;
    ItemID result = 0;
    int resultCount = 1;
    int width = 0;
    int height = 0;
};

struct CraftingResult {
    ItemID itemId = 0;
    int count = 0;
    bool matched = false;
};

class CraftingSystem {
public:
    // Load shaped recipes from a JSON configuration file.
    void loadRecipes(const std::string& configPath);

    // Match a row-major crafting grid against loaded shaped recipes.
    // Empty outer rows and columns are trimmed before comparison, allowing a
    // recipe to be placed anywhere inside a larger grid.
    [[nodiscard]] CraftingResult match(const std::vector<ItemID>& grid, int gridWidth, int gridHeight) const;

    // Return the loaded recipe list for tests and diagnostics.
    [[nodiscard]] const std::vector<CraftingRecipe>& getRecipes() const;

    // Remove every loaded recipe.
    void clear();

private:
    // Build the smallest non-empty rectangle from the input grid.
    static bool trimGrid(const std::vector<ItemID>& grid, int gridWidth, int gridHeight,
                         std::vector<std::vector<ItemID>>& outPattern, int& outWidth, int& outHeight);

    // Compare two shaped recipe patterns cell by cell.
    static bool patternEquals(const std::vector<std::vector<ItemID>>& a,
                              const std::vector<std::vector<ItemID>>& b);

    std::vector<CraftingRecipe> m_recipes;
};
