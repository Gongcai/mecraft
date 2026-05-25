#pragma once

// Unified slot index offset for crafting grid slots.
// Slots 0..Inventory::INVENTORY_SIZE-1 are regular inventory.
// Slots kCraftingSlotBase+0..3 are 2x2 crafting grid, +4 is result.
inline constexpr int kCraftingSlotBase = 10000;
