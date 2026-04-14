#include <cstdlib>
#include <iostream>

#include "../src/core/InputManager.h"
#include "../src/player/Inventory.h"

namespace {
int fail(const char* message) {
    std::cerr << "[inventory_drag_logic_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    Inventory inventory;

    if (Inventory::toInventoryIndexFromGridSlot(0) != 9) {
        return fail("grid slot 0 should map to inventory slot 9");
    }
    if (Inventory::toInventoryIndexFromGridSlot(27) != 0) {
        return fail("grid slot 27 should map to hotbar slot 0");
    }
    if (Inventory::toInventoryIndexFromGridSlot(35) != 8) {
        return fail("grid slot 35 should map to hotbar slot 8");
    }

    const BlockID sourceItem = inventory.getSlot(0);
    if (sourceItem == BlockType::AIR) {
        return fail("expected non-empty default hotbar slot");
    }

    const BlockID picked = inventory.takeSlot(0);
    if (picked != sourceItem) {
        return fail("takeSlot should return the original item");
    }
    if (inventory.getSlot(0) != BlockType::AIR) {
        return fail("takeSlot should clear source slot");
    }

    const BlockID replaced = inventory.placeSlot(10, picked);
    if (inventory.getSlot(10) != picked) {
        return fail("placeSlot should put item into destination slot");
    }
    if (replaced == BlockType::AIR) {
        return fail("placeSlot should return replaced item when destination was occupied");
    }

    inventory.swapSlots(10, 11);
    if (inventory.getSlot(11) != picked) {
        return fail("swapSlots should move placed item to other slot");
    }

    InputManager input;
    input.beginUIDragItem(static_cast<int>(picked), 1, 11);
    const auto& dragged = input.getUIDragItem();
    if (!dragged.active || dragged.itemId != static_cast<int>(picked) || dragged.count != 1 || dragged.sourceSlot != 11) {
        return fail("beginUIDragItem should publish active drag payload");
    }

    input.clearUIDragItem();
    if (input.getUIDragItem().active) {
        return fail("clearUIDragItem should clear drag payload");
    }

    std::cout << "[inventory_drag_logic_test] PASS\n";
    return EXIT_SUCCESS;
}

