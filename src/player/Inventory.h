#pragma once

#include <array>
#include <cstdint>
#include "../world/block/Block.h"
#include "../item/Item.h"

class Inventory {
public:
    static constexpr int HOTBAR_SIZE = 9;
    static constexpr int INVENTORY_ROWS = 4;
    static constexpr int INVENTORY_COLUMNS = 9;
    static constexpr int INVENTORY_SIZE = INVENTORY_ROWS * INVENTORY_COLUMNS;
    static constexpr int MAIN_INVENTORY_ROWS = 3;

    Inventory();
    void initializeDefaultLoadout();

    void setSelectedSlot(int slot);
    [[nodiscard]] int getSelectedSlot() const;
    void scrollSlot(int direction);

    [[nodiscard]] ItemID getSlotItem(int slot) const;
    void setSlotItem(int slot, ItemID item, uint16_t count = 1);
    [[nodiscard]] ItemStack getSlotStack(int slot) const;
    void setSlotStack(int slot, const ItemStack& stack);

    [[nodiscard]] ItemID getSelectedItem() const;
    [[nodiscard]] ItemStack getSelectedStack() const;
    [[nodiscard]] BlockID getSelectedBlock() const;
    [[nodiscard]] bool consumeSelectedOne();

    [[nodiscard]] bool isValidSlot(int slot) const;
    void swapSlots(int a, int b);

    // Add items with stack merging. Returns remaining count that did not fit.
    [[nodiscard]] uint32_t addItem(ItemID itemId, uint32_t count);

    [[nodiscard]] static int toInventoryIndex(int row, int column);
    [[nodiscard]] static int toInventoryIndexFromGridSlot(int gridSlot);

private:
    std::array<ItemStack, INVENTORY_SIZE> m_slots;
    int m_selectedSlot = 0;
};
