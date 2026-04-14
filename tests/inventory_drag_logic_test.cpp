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
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

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

    const ItemID sourceItem = inventory.getSlotItem(0);
    if (sourceItem == ItemIds::AIR) {
        return fail("expected non-empty default hotbar slot");
    }

    const ItemStack pickedStack = inventory.getSlotStack(0);
    const ItemID picked = pickedStack.itemId;
    if (picked != sourceItem) {
        return fail("getSlotStack should return the original item");
    }
    inventory.setSlotItem(0, ItemIds::AIR, 0);
    if (inventory.getSlotItem(0) != ItemIds::AIR) {
        return fail("clearing slot should result in AIR");
    }

    const ItemID replaced = inventory.getSlotItem(10);
    inventory.setSlotItem(10, picked, 1);
    if (inventory.getSlotItem(10) != picked) {
        return fail("setSlotItem should put item into destination slot");
    }

    inventory.swapSlots(10, 11);
    if (inventory.getSlotItem(11) != picked) {
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

