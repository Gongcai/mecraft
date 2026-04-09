#include "Inventory.h"

Inventory::Inventory() {
    // 默认物品栏内容
    m_slots[0] = BlockType::DIRT;
    m_slots[1] = BlockType::GRASS;
    m_slots[2] = BlockType::STONE;
    m_slots[3] = BlockType::SAND;
    m_slots[4] = BlockType::WOOD;
    m_slots[5] = BlockType::GLASS;
    m_slots[6] = BlockType::COAL_ORE;
    m_slots[7] = BlockType::DIAMOND_ORE;
    m_slots[8] = BlockType::IRON_ORE;
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
    if (slot >= 0 && slot < HOTBAR_SIZE) {
        return m_slots[slot];
    }
    return BlockType::AIR;
}

void Inventory::setSlot(int slot, BlockID block) {
    if (slot >= 0 && slot < HOTBAR_SIZE) {
        m_slots[slot] = block;
    }
}

BlockID Inventory::getSelectedBlock() const {
    return m_slots[m_selectedSlot];
}
