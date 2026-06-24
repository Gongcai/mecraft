#include "SmeltingSystem.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
bool parseItemToken(const json& token, ItemID& outItemId) {
    if (!token.is_string()) {
        return false;
    }

    const std::string text = token.get<std::string>();
    return ItemRegistry::tryGetIdByName(text, outItemId);
}
}

void SmeltingSystem::loadRecipes(const std::string& configPath) {
    ItemRegistry::init();

    std::ifstream file(configPath);
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;

    clear();

    if (root.contains("recipes") && root["recipes"].is_array()) {
        for (const auto& recipeJson : root["recipes"]) {
            ItemID input = 0;
            ItemID result = 0;
            if (!recipeJson.contains("input") ||
                !recipeJson.contains("result") ||
                !parseItemToken(recipeJson["input"], input) ||
                !parseItemToken(recipeJson["result"], result) ||
                input == 0 ||
                result == 0) {
                continue;
            }

            const int resultCount = recipeJson.value("resultCount", 1);
            const float cookSeconds = recipeJson.value("cookSeconds", 10.0f);
            if (resultCount <= 0 || cookSeconds <= 0.0f) {
                continue;
            }

            SmeltingRecipe recipe;
            recipe.input = input;
            recipe.result = result;
            recipe.resultCount = static_cast<uint16_t>(std::min(resultCount, 65535));
            recipe.cookSeconds = cookSeconds;

            const std::size_t index = m_recipes.size();
            m_recipeByInput[input] = index;
            m_recipes.push_back(recipe);
        }
    }

    if (root.contains("fuels") && root["fuels"].is_array()) {
        for (const auto& fuelJson : root["fuels"]) {
            ItemID item = 0;
            if (!fuelJson.contains("item") ||
                !parseItemToken(fuelJson["item"], item) ||
                item == 0) {
                continue;
            }

            const float seconds = fuelJson.value("seconds", 0.0f);
            if (seconds <= 0.0f) {
                continue;
            }

            m_fuelSeconds[item] = seconds;
        }
    }
}

const SmeltingRecipe* SmeltingSystem::findRecipe(const ItemID input) const {
    const auto it = m_recipeByInput.find(input);
    if (it == m_recipeByInput.end()) {
        return nullptr;
    }
    return &m_recipes[it->second];
}

float SmeltingSystem::fuelSeconds(const ItemID fuel) const {
    const auto it = m_fuelSeconds.find(fuel);
    if (it == m_fuelSeconds.end()) {
        return 0.0f;
    }
    return it->second;
}

bool SmeltingSystem::isFuel(const ItemID fuel) const {
    return fuelSeconds(fuel) > 0.0f;
}

const std::vector<SmeltingRecipe>& SmeltingSystem::getRecipes() const {
    return m_recipes;
}

void SmeltingSystem::clear() {
    m_recipes.clear();
    m_recipeByInput.clear();
    m_fuelSeconds.clear();
}
