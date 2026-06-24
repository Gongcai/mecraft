#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../item/Item.h"

struct SmeltingRecipe {
    ItemID input = 0;
    ItemID result = 0;
    uint16_t resultCount = 1;
    float cookSeconds = 10.0f;
};

class SmeltingSystem {
public:
    void loadRecipes(const std::string& configPath);

    [[nodiscard]] const SmeltingRecipe* findRecipe(ItemID input) const;
    [[nodiscard]] float fuelSeconds(ItemID fuel) const;
    [[nodiscard]] bool isFuel(ItemID fuel) const;

    [[nodiscard]] const std::vector<SmeltingRecipe>& getRecipes() const;
    void clear();

private:
    std::vector<SmeltingRecipe> m_recipes;
    std::unordered_map<ItemID, std::size_t> m_recipeByInput;
    std::unordered_map<ItemID, float> m_fuelSeconds;
};
