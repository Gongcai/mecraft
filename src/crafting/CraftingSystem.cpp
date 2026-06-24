#include "CraftingSystem.h"

#include <algorithm>
#include <fstream>
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
}

void CraftingSystem::loadRecipes(const std::string& configPath) {
    ItemRegistry::init();

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

        if (!recipeJson.contains("pattern")) {
            continue;
        }
        const auto& patternJson = recipeJson["pattern"];
        if (!patternJson.is_array() || patternJson.empty()) {
            continue;
        }

        std::unordered_map<std::string, ItemID> keyMap;
        if (recipeJson.contains("key") && recipeJson["key"].is_object()) {
            for (const auto& [key, value] : recipeJson["key"].items()) {
                ItemID keyItem = 0;
                if (parseItemToken(value, keyItem)) {
                    keyMap[key] = keyItem;
                }
            }
        }

        recipe.height = static_cast<int>(patternJson.size());
        recipe.width = 0;
        recipe.pattern.resize(recipe.height);

        bool hasIngredient = false;
        bool hasInvalidRow = false;
        for (int row = 0; row < recipe.height; ++row) {
            if (!patternJson[row].is_string()) {
                hasInvalidRow = true;
                break;
            }
            const std::string& rowStr = patternJson[row].get<std::string>();
            recipe.width = std::max(recipe.width, static_cast<int>(rowStr.size()));
            recipe.pattern[row].reserve(rowStr.size());
            for (char ch : rowStr) {
                std::string key(1, ch);
                if (ch == ' ') {
                    recipe.pattern[row].push_back(0);
                    continue;
                }

                const auto keyIt = keyMap.find(key);
                if (keyIt == keyMap.end()) {
                    hasInvalidRow = true;
                    break;
                }

                recipe.pattern[row].push_back(keyIt->second);
                hasIngredient = true;
            }
            if (hasInvalidRow) {
                break;
            }
        }

        if (hasInvalidRow || !hasIngredient || recipe.width <= 0) {
            continue;
        }

        m_recipes.push_back(std::move(recipe));
    }
}

CraftingResult CraftingSystem::match(const std::vector<ItemID>& grid,
                                     int gridWidth,
                                     int gridHeight) const {
    std::vector<std::vector<ItemID>> trimmed;
    int trimmedW = 0, trimmedH = 0;

    if (!trimGrid(grid, gridWidth, gridHeight, trimmed, trimmedW, trimmedH)) {
        return {0, 0, false};
    }

    for (const auto& recipe : m_recipes) {
        if (recipe.width != trimmedW || recipe.height != trimmedH) {
            continue;
        }
        if (patternEquals(recipe.pattern, trimmed)) {
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

bool CraftingSystem::trimGrid(const std::vector<ItemID>& grid,
                               int gridWidth,
                               int gridHeight,
                               std::vector<std::vector<ItemID>>& outPattern,
                               int& outWidth,
                               int& outHeight) {
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
            outPattern[static_cast<size_t>(row - minRow)].push_back(
                grid[index]);
        }
    }

    return true;
}

bool CraftingSystem::patternEquals(const std::vector<std::vector<ItemID>>& a,
                                   const std::vector<std::vector<ItemID>>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t row = 0; row < a.size(); ++row) {
        if (a[row].size() != b[row].size()) {
            return false;
        }
        for (size_t col = 0; col < a[row].size(); ++col) {
            if (a[row][col] != b[row][col]) {
                return false;
            }
        }
    }
    return true;
}
