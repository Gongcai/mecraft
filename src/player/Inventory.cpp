#include "Inventory.h"

#include <utility>

Inventory::Inventory() : m_slots{} {
    m_slots.fill(BlockType::AIR);

    // 默认热栏内容
    m_slots[0] = BlockType::DIRT;
    m_slots[1] = BlockType::GRASS;
    m_slots[2] = BlockType::STONE;
    m_slots[3] = BlockType::SAND;
    m_slots[4] = BlockType::WOOD;
    m_slots[5] = BlockType::GLASS;
    m_slots[6] = BlockType::COAL_ORE;
    m_slots[7] = BlockType::ROSE;
    m_slots[8] = BlockType::TALL_GRASS;

    // 默认背包内容（前三行）
    m_slots[9] = BlockType::IRON_ORE;
    m_slots[10] = BlockType::DIAMOND_ORE;
    m_slots[11] = BlockType::WATER;
    m_slots[12] = BlockType::BIRCH_LOG;
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

BlockID Inventory::getSlot(int slot) const {
    if (slot >= 0 && slot < INVENTORY_SIZE) {
        return m_slots[slot];
    }
    return BlockType::AIR;
}

void Inventory::setSlot(int slot, BlockID block) {
    if (slot >= 0 && slot < INVENTORY_SIZE) {
        m_slots[slot] = block;
    }
}

BlockID Inventory::getSelectedBlock() const {
    return m_slots[m_selectedSlot];
}

bool Inventory::isValidSlot(const int slot) const {
    return slot >= 0 && slot < INVENTORY_SIZE;
}

BlockID Inventory::takeSlot(const int slot) {
    if (!isValidSlot(slot)) {
        return BlockType::AIR;
    }
    const BlockID value = m_slots[slot];
    m_slots[slot] = BlockType::AIR;
    return value;
}

BlockID Inventory::placeSlot(const int slot, const BlockID block) {
    if (!isValidSlot(slot)) {
        return block;
    }
    const BlockID replaced = m_slots[slot];
    m_slots[slot] = block;
    return replaced;
}

void Inventory::swapSlots(const int a, const int b) {
    if (!isValidSlot(a) || !isValidSlot(b) || a == b) {
        return;
    }
    std::swap(m_slots[a], m_slots[b]);
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

