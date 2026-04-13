#pragma once

#include <string>
#include <vector>

#include "../item/Item.h"

// 合成配方：模式 + 产物
struct CraftingRecipe {
    // 模式：二维数组 [row][col]，用 ItemID 表示
    // ItemType::AIR 表示该位置为空
    std::vector<std::vector<ItemID>> pattern;
    ItemID result = ItemType::AIR;
    int resultCount = 1;
    int width = 0;   // 模式实际宽度
    int height = 0;  // 模式实际高度
};

struct CraftingResult {
    ItemID itemId = ItemType::AIR;
    int count = 0;
    bool matched = false;
};

class CraftingSystem {
public:
    // 从 JSON 文件加载合成配方
    void loadRecipes(const std::string& configPath);

    // 核心合成匹配接口
    // grid: 合成格内容，按行优先顺序排列
    // gridWidth / gridHeight: 合成格尺寸（如 2x2 或 3x3）
    // 支持在格内任意位置放置配方（自动裁剪空白行列后匹配）
    [[nodiscard]] CraftingResult match(const std::vector<ItemID>& grid, int gridWidth, int gridHeight) const;

    // 获取所有已加载的配方（只读）
    [[nodiscard]] const std::vector<CraftingRecipe>& getRecipes() const;

    // 清空所有配方
    void clear();

private:
    // 裁剪网格中的空行空列，返回最小包围矩形的内容
    static bool trimGrid(const std::vector<ItemID>& grid, int gridWidth, int gridHeight,
                         std::vector<std::vector<ItemID>>& outPattern, int& outWidth, int& outHeight);

    // 比较两个 pattern 是否完全相同
    static bool patternEquals(const std::vector<std::vector<ItemID>>& a,
                              const std::vector<std::vector<ItemID>>& b);

    std::vector<CraftingRecipe> m_recipes;
};
