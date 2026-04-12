#include "CraftingSystem.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void CraftingSystem::loadRecipes(const std::string& configPath) {
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

        // 解析 result
        recipe.result = static_cast<BlockID>(recipeJson.value("result", 0));
        recipe.resultCount = recipeJson.value("resultCount", 1);

        // 解析 pattern：字符串数组，每个字符串代表一行
        // 用字符串名称映射 BlockID（通过名称查找）
        const auto& patternJson = recipeJson["pattern"];
        if (!patternJson.is_array()) {
            continue;
        }

        // 建立键映射：key -> BlockID
        std::unordered_map<std::string, BlockID> keyMap;
        if (recipeJson.contains("key") && recipeJson["key"].is_object()) {
            for (const auto& [key, value] : recipeJson["key"].items()) {
                if (value.is_string()) {
                    // 通过 BlockRegistry 查找 BlockID — 这里简化为直接用整数
                    // 由于 JSON 中存储的是字符串名称，我们使用 blocks.json 的反向映射
                    // 为简化，key 映射中直接存储 BlockID 整数
                    keyMap[key] = static_cast<BlockID>(std::stoi(value.get<std::string>()));
                } else if (value.is_number()) {
                    keyMap[key] = static_cast<BlockID>(value.get<int>());
                }
            }
        }

        recipe.height = static_cast<int>(patternJson.size());
        recipe.width = 0;
        recipe.pattern.resize(recipe.height);

        for (int row = 0; row < recipe.height; ++row) {
            const std::string& rowStr = patternJson[row].get<std::string>();
            recipe.width = std::max(recipe.width, static_cast<int>(rowStr.size()));
            recipe.pattern[row].reserve(rowStr.size());
            for (char ch : rowStr) {
                std::string key(1, ch);
                if (ch == ' ' || keyMap.find(key) == keyMap.end()) {
                    recipe.pattern[row].push_back(BlockType::AIR);
                } else {
                    recipe.pattern[row].push_back(keyMap.at(key));
                }
            }
        }

        m_recipes.push_back(std::move(recipe));
    }
}

CraftingResult CraftingSystem::match(const std::vector<BlockID>& grid,
                                     int gridWidth,
                                     int gridHeight) const {
    // 先裁剪输入网格
    std::vector<std::vector<BlockID>> trimmed;
    int trimmedW = 0, trimmedH = 0;

    if (!trimGrid(grid, gridWidth, gridHeight, trimmed, trimmedW, trimmedH)) {
        // 空网格，无法合成
        return {BlockType::AIR, 0, false};
    }

    // 遍历所有配方，比较裁剪后的模式
    for (const auto& recipe : m_recipes) {
        if (recipe.width != trimmedW || recipe.height != trimmedH) {
            continue;
        }
        if (patternEquals(recipe.pattern, trimmed)) {
            return {recipe.result, recipe.resultCount, true};
        }
    }

    return {BlockType::AIR, 0, false};
}

const std::vector<CraftingRecipe>& CraftingSystem::getRecipes() const {
    return m_recipes;
}

void CraftingSystem::clear() {
    m_recipes.clear();
}

bool CraftingSystem::trimGrid(const std::vector<BlockID>& grid,
                               int gridWidth,
                               int gridHeight,
                               std::vector<std::vector<BlockID>>& outPattern,
                               int& outWidth,
                               int& outHeight) {
    // 找到非空行列的范围
    int minRow = gridHeight, maxRow = -1;
    int minCol = gridWidth, maxCol = -1;

    for (int row = 0; row < gridHeight; ++row) {
        for (int col = 0; col < gridWidth; ++col) {
            BlockID id = grid[static_cast<size_t>(row * gridWidth + col)];
            if (id != BlockType::AIR) {
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
            outPattern[static_cast<size_t>(row - minRow)].push_back(
                grid[static_cast<size_t>(row * gridWidth + col)]);
        }
    }

    return true;
}

bool CraftingSystem::patternEquals(const std::vector<std::vector<BlockID>>& a,
                                   const std::vector<std::vector<BlockID>>& b) {
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
