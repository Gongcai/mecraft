#include "CraftingSystem.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
bool parseItemToken(const json& token, ItemID& outItemId) {
    if (token.is_number_integer()) {
        const int id = token.get<int>();
        if (id < 0 || id >= static_cast<int>(ItemRegistry::getItemCount())) {
            return false;
        }
        outItemId = static_cast<ItemID>(id);
        return true;
    }

    if (!token.is_string()) {
        return false;
    }

    const std::string text = token.get<std::string>();
    bool allDigits = !text.empty();
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            allDigits = false;
            break;
        }
    }

    if (allDigits) {
        const int id = std::stoi(text);
        if (id < 0 || id >= static_cast<int>(ItemRegistry::getItemCount())) {
            return false;
        }
        outItemId = static_cast<ItemID>(id);
        return true;
    }

    return ItemRegistry::tryGetIdByName(text, outItemId);
}

bool parseTagToken(const json& token, NamespacedId& outTag) {
    if (!token.is_string()) {
        return false;
    }

    std::string text = token.get<std::string>();
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.empty()) {
        return false;
    }

    outTag = NamespacedId(text);
    return true;
}

bool parseIngredientToken(const json& token, CraftingIngredient& outIngredient) {
    outIngredient = CraftingIngredient{};

    if (token.is_object()) {
        const bool hasItem = token.contains("item");
        const bool hasTag = token.contains("tag");
        if (hasItem == hasTag) {
            return false;
        }

        if (hasItem) {
            ItemID itemId = 0;
            if (!parseItemToken(token["item"], itemId) || itemId == 0) {
                return false;
            }
            outIngredient.kind = CraftingIngredient::Kind::Item;
            outIngredient.itemId = itemId;
            return true;
        }

        NamespacedId tag;
        if (!parseTagToken(token["tag"], tag)) {
            return false;
        }
        outIngredient.kind = CraftingIngredient::Kind::Tag;
        outIngredient.tag = tag;
        return true;
    }

    if (token.is_string()) {
        const std::string text = token.get<std::string>();
        if (!text.empty() && text.front() == '#') {
            NamespacedId tag;
            if (!parseTagToken(token, tag)) {
                return false;
            }
            outIngredient.kind = CraftingIngredient::Kind::Tag;
            outIngredient.tag = tag;
            return true;
        }
    }

    ItemID itemId = 0;
    if (!parseItemToken(token, itemId) || itemId == 0) {
        return false;
    }
    outIngredient.kind = CraftingIngredient::Kind::Item;
    outIngredient.itemId = itemId;
    return true;
}

bool isShapelessRecipe(const json& recipeJson) {
    if (recipeJson.contains("type") && recipeJson["type"].is_string()) {
        const std::string type = recipeJson["type"].get<std::string>();
        return type == "shapeless" || type == "crafting_shapeless" || type == "minecraft:crafting_shapeless";
    }
    return recipeJson.contains("ingredients") && !recipeJson.contains("pattern");
}
} // namespace

void CraftingSystem::loadRecipes(const std::string& configPath) {
    ItemRegistry::init();
    clear();

    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;

    if (!root.contains("recipes") || !root["recipes"].is_array()) {
        return;
    }

    for (const auto& recipeJson : root["recipes"]) {
        CraftingRecipe recipe;

        ItemID result = 0;
        if (!recipeJson.contains("result") || !parseItemToken(recipeJson["result"], result) || result == 0) {
            continue;
        }
        recipe.result = result;
        recipe.resultCount = recipeJson.value("resultCount", 1);
        if (recipe.resultCount <= 0) {
            continue;
        }

        recipe.shapeless = isShapelessRecipe(recipeJson);
        if (recipe.shapeless) {
            if (!recipeJson.contains("ingredients") || !recipeJson["ingredients"].is_array()) {
                continue;
            }

            bool hasInvalidIngredient = false;
            for (const auto& ingredientJson : recipeJson["ingredients"]) {
                CraftingIngredient ingredient;
                if (!parseIngredientToken(ingredientJson, ingredient)) {
                    hasInvalidIngredient = true;
                    break;
                }
                recipe.ingredients.push_back(ingredient);
            }

            if (hasInvalidIngredient || recipe.ingredients.empty()) {
                continue;
            }

            m_recipes.push_back(std::move(recipe));
            continue;
        }

        if (!recipeJson.contains("pattern")) {
            continue;
        }
        const auto& patternJson = recipeJson["pattern"];
        if (!patternJson.is_array() || patternJson.empty()) {
            continue;
        }

        std::unordered_map<char, CraftingIngredient> keyMap;
        if (recipeJson.contains("key") && recipeJson["key"].is_object()) {
            for (const auto& [key, value] : recipeJson["key"].items()) {
                if (key.size() != 1 || key.front() == ' ') {
                    continue;
                }

                CraftingIngredient keyIngredient;
                if (parseIngredientToken(value, keyIngredient)) {
                    keyMap[key.front()] = keyIngredient;
                }
            }
        }

        std::vector<std::string> rows;
        rows.reserve(patternJson.size());
        bool hasInvalidRow = false;
        for (const auto& rowJson : patternJson) {
            if (!rowJson.is_string()) {
                hasInvalidRow = true;
                break;
            }
            rows.push_back(rowJson.get<std::string>());
            recipe.width = std::max(recipe.width, static_cast<int>(rows.back().size()));
        }

        if (hasInvalidRow || rows.empty() || recipe.width <= 0) {
            continue;
        }

        recipe.height = static_cast<int>(rows.size());
        recipe.pattern.resize(static_cast<size_t>(recipe.height));

        bool hasIngredient = false;
        for (int row = 0; row < recipe.height; ++row) {
            recipe.pattern[static_cast<size_t>(row)].reserve(static_cast<size_t>(recipe.width));
            for (int col = 0; col < recipe.width; ++col) {
                const char ch = col < static_cast<int>(rows[static_cast<size_t>(row)].size())
                                    ? rows[static_cast<size_t>(row)][static_cast<size_t>(col)]
                                    : ' ';

                if (ch == ' ') {
                    recipe.pattern[static_cast<size_t>(row)].push_back(CraftingIngredient{});
                    continue;
                }

                const auto keyIt = keyMap.find(ch);
                if (keyIt == keyMap.end()) {
                    hasInvalidRow = true;
                    break;
                }

                recipe.pattern[static_cast<size_t>(row)].push_back(keyIt->second);
                hasIngredient = true;
            }
            if (hasInvalidRow) {
                break;
            }
        }

        if (hasInvalidRow || !hasIngredient) {
            continue;
        }

        m_recipes.push_back(std::move(recipe));
    }
}

CraftingResult CraftingSystem::match(const std::vector<ItemID>& grid, int gridWidth, int gridHeight) const {
    std::vector<std::vector<ItemID>> trimmed;
    int trimmedW = 0, trimmedH = 0;

    if (!trimGrid(grid, gridWidth, gridHeight, trimmed, trimmedW, trimmedH)) {
        return {0, 0, false};
    }

    std::vector<ItemID> nonEmptyItems;
    for (const std::vector<ItemID>& row : trimmed) {
        for (const ItemID itemId : row) {
            if (itemId != 0) {
                nonEmptyItems.push_back(itemId);
            }
        }
    }

    for (const auto& recipe : m_recipes) {
        if (recipe.shapeless) {
            if (shapelessMatches(recipe.ingredients, nonEmptyItems)) {
                return {recipe.result, recipe.resultCount, true};
            }
            continue;
        }

        if (recipe.width != trimmedW || recipe.height != trimmedH) {
            continue;
        }
        if (patternMatches(recipe.pattern, trimmed)) {
            return {recipe.result, recipe.resultCount, true};
        }
    }

    return {0, 0, false};
}

const std::vector<CraftingRecipe>& CraftingSystem::getRecipes() const {
    return m_recipes;
}

void CraftingSystem::clear() {
    m_recipes.clear();
}

bool CraftingSystem::trimGrid(const std::vector<ItemID>& grid, int gridWidth, int gridHeight,
                              std::vector<std::vector<ItemID>>& outPattern, int& outWidth, int& outHeight) {
    if (gridWidth <= 0 || gridHeight <= 0 ||
        grid.size() < static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight)) {
        outWidth = 0;
        outHeight = 0;
        return false;
    }

    int minRow = gridHeight, maxRow = -1;
    int minCol = gridWidth, maxCol = -1;

    for (int row = 0; row < gridHeight; ++row) {
        for (int col = 0; col < gridWidth; ++col) {
            const size_t index = static_cast<size_t>(row) * static_cast<size_t>(gridWidth) + static_cast<size_t>(col);
            ItemID id = grid[index];
            if (id != 0) {
                minRow = std::min(minRow, row);
                maxRow = std::max(maxRow, row);
                minCol = std::min(minCol, col);
                maxCol = std::max(maxCol, col);
            }
        }
    }

    if (maxRow < 0 || maxCol < 0) {
        outWidth = 0;
        outHeight = 0;
        return false;
    }

    outWidth = maxCol - minCol + 1;
    outHeight = maxRow - minRow + 1;
    outPattern.resize(static_cast<size_t>(outHeight));

    for (int row = minRow; row <= maxRow; ++row) {
        outPattern[static_cast<size_t>(row - minRow)].reserve(static_cast<size_t>(outWidth));
        for (int col = minCol; col <= maxCol; ++col) {
            const size_t index = static_cast<size_t>(row) * static_cast<size_t>(gridWidth) + static_cast<size_t>(col);
            outPattern[static_cast<size_t>(row - minRow)].push_back(grid[index]);
        }
    }

    return true;
}

bool CraftingSystem::patternMatches(const std::vector<std::vector<CraftingIngredient>>& recipePattern,
                                    const std::vector<std::vector<ItemID>>& gridPattern) {
    if (recipePattern.size() != gridPattern.size()) {
        return false;
    }
    for (size_t row = 0; row < recipePattern.size(); ++row) {
        if (recipePattern[row].size() != gridPattern[row].size()) {
            return false;
        }
        for (size_t col = 0; col < recipePattern[row].size(); ++col) {
            if (!ingredientMatches(recipePattern[row][col], gridPattern[row][col])) {
                return false;
            }
        }
    }
    return true;
}

bool CraftingSystem::shapelessMatches(const std::vector<CraftingIngredient>& recipeIngredients,
                                      const std::vector<ItemID>& gridItems) {
    if (recipeIngredients.size() != gridItems.size()) {
        return false;
    }

    std::vector<bool> used(gridItems.size(), false);
    std::function<bool(size_t)> matchIngredient = [&](const size_t ingredientIndex) {
        if (ingredientIndex == recipeIngredients.size()) {
            return true;
        }

        for (size_t itemIndex = 0; itemIndex < gridItems.size(); ++itemIndex) {
            if (used[itemIndex]) {
                continue;
            }
            if (!ingredientMatches(recipeIngredients[ingredientIndex], gridItems[itemIndex])) {
                continue;
            }

            used[itemIndex] = true;
            if (matchIngredient(ingredientIndex + 1)) {
                return true;
            }
            used[itemIndex] = false;
        }

        return false;
    };

    return matchIngredient(0);
}

bool CraftingSystem::ingredientMatches(const CraftingIngredient& ingredient, const ItemID itemId) {
    switch (ingredient.kind) {
    case CraftingIngredient::Kind::Empty: return itemId == 0;
    case CraftingIngredient::Kind::Item: return itemId != 0 && itemId == ingredient.itemId;
    case CraftingIngredient::Kind::Tag: return itemId != 0 && ItemRegistry::hasTag(itemId, ingredient.tag);
    }
    return false;
}
