#include "Inventory.h"
#include "../item/Item.h"
#include <algorithm>
#include <utility>

Inventory::Inventory() : m_slots{} {
    for (ItemStack& stack : m_slots) {
        stack = {};
    }
}

void Inventory::initializeDefaultLoadout() {
    for (ItemStack& stack : m_slots) {
        stack = {};
    }

    // Default hotbar content. Call only after block/item registries are initialized.
    setSlotItem(0, BlockIds::DIRT,64);
    setSlotItem(1, BlockIds::GRASS);
    setSlotItem(2, BlockIds::STONE);
    setSlotItem(3, BlockIds::SAND);
    setSlotItem(4, BlockIds::WOOD);
    setSlotItem(5, BlockIds::GLASS);
    setSlotItem(6, BlockIds::COAL_ORE);
    setSlotItem(7, ItemIds::APPLE, 16);
    setSlotItem(8, ItemIds::IRON_PICKAXE, 1);
    setSlotItem(13, BlockIds::TORCH, 64);
    setSlotItem(14, ItemRegistry::fromBlock(BlockIds::BLUE_WOOL), 64);
    setSlotItem(15,BlockIds::BIRCH_LEAVES,64);
    // Default inventory content.
    setSlotItem(9, BlockIds::IRON_ORE);
    setSlotItem(10, BlockIds::DIAMOND_ORE);
    setSlotItem(11, BlockIds::WATER);
    setSlotItem(12, BlockIds::BIRCH_LOG);
    setSlotItem(16, ItemIds::COAL, 16);
}

void Inventory::setSelectedSlot(int slot) {
    if (slot >= 0 && slot < HOTBAR_SIZE) {
        m_selectedSlot = slot;
    }
}

int Inventory::getSelectedSlot() const {
    return m_selectedSlot;
}

void Inventory::scrollSlot(int direction) {
    m_selectedSlot += direction;
    if (m_selectedSlot < 0) m_selectedSlot = HOTBAR_SIZE - 1;
    if (m_selectedSlot >= HOTBAR_SIZE) m_selectedSlot = 0;
}

ItemID Inventory::getSlotItem(const int slot) const {
    if (!isValidSlot(slot)) {
        return 0;
    }
    const ItemStack& stack = m_slots[slot];
    return stack.isEmpty() ? 0 : stack.itemId;
}

void Inventory::setSlotItem(const int slot, const ItemID item, const uint16_t count) {
    if (!isValidSlot(slot) || item >= 65536) {
        return;
    }

    if (item == 0 || count == 0) {
        m_slots[slot] = {};
        return;
    }

    ItemStack stack;
    stack.itemId = item;
    stack.count = count;
    // Avoid touching ItemRegistry here: Inventory can be constructed before BlockRegistry
    // is fully initialized with ResourceMgr, and early ItemRegistry access would lock blocks
    // into fallback texture index 0 for the whole run.
    // NOTE: durability is intentionally left as 0 here for the same reason.
    // Callers that need proper durability (e.g. addItem) set it explicitly.
    stack.durability = 0;
    m_slots[slot] = stack;
}

ItemStack Inventory::getSlotStack(const int slot) const {
    if (!isValidSlot(slot)) {
        return {};
    }
    return m_slots[slot];
}

void Inventory::setSlotStack(const int slot, const ItemStack& stack) {
    if (!isValidSlot(slot)) {
        return;
    }
    if (stack.isEmpty()) {
        m_slots[slot] = {};
        return;
    }
    m_slots[slot] = stack;
}

ItemID Inventory::getSelectedItem() const {
    return getSlotItem(m_selectedSlot);
}

ItemStack Inventory::getSelectedStack() const {
    return getSlotStack(m_selectedSlot);
}

BlockID Inventory::getSelectedBlock() const {
    return ItemRegistry::toPlaceBlock(getSelectedItem());
}

bool Inventory::consumeSelectedOne() {
    if (!isValidSlot(m_selectedSlot)) {
        return false;
    }

    ItemStack& stack = m_slots[m_selectedSlot];
    if (stack.isEmpty()) {
        return false;
    }

    if (stack.count > 1) {
        --stack.count;
        return true;
    }

    m_slots[m_selectedSlot] = {};
    return true;
}

bool Inventory::isValidSlot(const int slot) const {
    return slot >= 0 && slot < INVENTORY_SIZE;
}

void Inventory::swapSlots(const int a, const int b) {
    if (!isValidSlot(a) || !isValidSlot(b) || a == b) {
        return;
    }
    std::swap(m_slots[a], m_slots[b]);
}

uint32_t Inventory::addItem(const ItemID itemId, uint32_t count) {
    if (itemId == 0 || count == 0 || itemId >= 65536) {
        return count;
    }

    const ItemDef& def = ItemRegistry::get(itemId);
    if (def.maxStack == 0) {
        return count;
    }

    // First pass: merge into existing stacks.
    for (ItemStack& stack : m_slots) {
        if (count == 0) {
            return 0;
        }
        if (stack.isEmpty() || stack.itemId != itemId || stack.count >= def.maxStack) {
            continue;
        }

        const uint16_t freeSpace = static_cast<uint16_t>(def.maxStack - stack.count);
        const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, freeSpace));
        stack.count = static_cast<uint16_t>(stack.count + add);
        count -= add;
    }

    // Second pass: fill empty slots.
    for (ItemStack& stack : m_slots) {
        if (count == 0) {
            return 0;
        }
        if (!stack.isEmpty()) {
            continue;
        }

        const uint16_t add = static_cast<uint16_t>(std::min<uint32_t>(count, def.maxStack));
        stack.itemId = itemId;
        stack.count = add;
        stack.durability = def.isTool ? def.maxDurability : 0;
        count -= add;
    }

    return count;
}

int Inventory::toInventoryIndex(const int row, const int column) {
    if (row < 0 || row >= INVENTORY_ROWS || column < 0 || column >= INVENTORY_COLUMNS) {
        return -1;
    }

    if (row < MAIN_INVENTORY_ROWS) {
        return HOTBAR_SIZE + row * INVENTORY_COLUMNS + column;
    }
    return column;
}

int Inventory::toInventoryIndexFromGridSlot(const int gridSlot) {
    if (gridSlot < 0 || gridSlot >= INVENTORY_SIZE) {
        return -1;
    }

    const int row = gridSlot / INVENTORY_COLUMNS;
    const int col = gridSlot % INVENTORY_COLUMNS;
    return toInventoryIndex(row, col);
}
