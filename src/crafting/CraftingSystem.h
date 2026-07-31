#pragma once

#include <string>
#include <vector>

#include "../item/Item.h"

struct CraftingIngredient {
    enum class Kind { Empty, Item, Tag };

    Kind kind = Kind::Empty;
    ItemID itemId = 0;
    NamespacedId tag = NamespacedId("minecraft", "air");
};

struct CraftingRecipe {
    // Pattern cells are stored as [row][column].
    std::vector<std::vector<CraftingIngredient>> pattern;
    std::vector<CraftingIngredient> ingredients;
    ItemID result = 0;
    int resultCount = 1;
    int width = 0;
    int height = 0;
    bool shapeless = false;
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

    // Compare a shaped recipe pattern against concrete grid items.
    static bool patternMatches(const std::vector<std::vector<CraftingIngredient>>& recipePattern,
                               const std::vector<std::vector<ItemID>>& gridPattern);

    // Compare shapeless recipe ingredients against concrete grid items.
    static bool shapelessMatches(const std::vector<CraftingIngredient>& recipeIngredients,
                                 const std::vector<ItemID>& gridItems);

    // Return true when a concrete item satisfies a recipe ingredient.
    static bool ingredientMatches(const CraftingIngredient& ingredient, ItemID itemId);

    std::vector<CraftingRecipe> m_recipes;
};
