#pragma once

#include "../../item/Item.h"
#include "../../player/Inventory.h"

namespace ecs {

inline void applySelectedToolDurabilityWear(Inventory& inventory) {
    ItemStack heldStack = inventory.getSelectedStack();
    if (heldStack.isEmpty()) {
        return;
    }

    const ItemDef& itemDef = ItemRegistry::get(heldStack.itemId);
    if (!itemDef.isTool || itemDef.maxDurability == 0) {
        return;
    }

    if (heldStack.durability == 0) {
        heldStack.durability = itemDef.maxDurability;
    }

    --heldStack.durability;
    if (heldStack.durability == 0) {
        inventory.setSlotStack(inventory.getSelectedSlot(), {});
        return;
    }

    inventory.setSlotStack(inventory.getSelectedSlot(), heldStack);
}

} // namespace ecs
