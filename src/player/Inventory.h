#pragma once

#include <array>
#include "../world/Block.h"

class Inventory {
public:
    static constexpr int HOTBAR_SIZE = 9;

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

private:
    std::array<BlockID, HOTBAR_SIZE> m_slots;
    int m_selectedSlot = 0;
};
