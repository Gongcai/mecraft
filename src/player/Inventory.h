#pragma once

#include <array>
#include "../world/Block.h"

class Inventory {
public:
    static constexpr int HOTBAR_SIZE = 9;
    static constexpr int INVENTORY_ROWS = 4;
    static constexpr int INVENTORY_COLUMNS = 9;
    static constexpr int INVENTORY_SIZE = INVENTORY_ROWS * INVENTORY_COLUMNS;
    static constexpr int MAIN_INVENTORY_ROWS = 3;

    Inventory();

    // 数字键选择槽位 (1-9)
    void setSelectedSlot(int slot);
    [[nodiscard]] int getSelectedSlot() const;

    // 滚轮切换槽位 (direction: 1=下一个, -1=上一个, 循环)
    void scrollSlot(int direction);

    // 获取/设置槽位中的方块类型
    [[nodiscard]] BlockID getSlot(int slot) const;
    void setSlot(int slot, BlockID block);

    // 获取当前选中的方块
    [[nodiscard]] BlockID getSelectedBlock() const;

    [[nodiscard]] bool isValidSlot(int slot) const;
    [[nodiscard]] BlockID takeSlot(int slot);
    [[nodiscard]] BlockID placeSlot(int slot, BlockID block);
    void swapSlots(int a, int b);

    // Inventory grid mapping (row-major UI): top 3 rows are main inventory, bottom row is hotbar.
    [[nodiscard]] static int toInventoryIndex(int row, int column);
    [[nodiscard]] static int toInventoryIndexFromGridSlot(int gridSlot);

private:
    std::array<BlockID, INVENTORY_SIZE> m_slots;
    int m_selectedSlot = 0;
};
