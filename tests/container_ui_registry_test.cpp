#include <cstdlib>
#include <iostream>

#include "../src/ui/inventory/ContainerUiRegistry.h"
#include "../src/world/block/Block.h"

namespace {
int fail(const char* message) {
    std::cerr << "[container_ui_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

const ui::ContainerSlotGroupDef* findSlotGroup(const ui::ContainerUiDef& def, const std::string& id) {
    for (const ui::ContainerSlotGroupDef& group : def.slotGroups) {
        if (group.id == id) {
            return &group;
        }
    }
    return nullptr;
}

const ui::ContainerProgressDef* findProgress(const ui::ContainerUiDef& def, const std::string& id) {
    for (const ui::ContainerProgressDef& progress : def.progressBars) {
        if (progress.id == id) {
            return &progress;
        }
    }
    return nullptr;
}
}

int main() {
    BlockRegistry::init(nullptr);
    ui::ContainerUiRegistry::init();

    const ui::ContainerUiDef& chest = ui::ContainerUiRegistry::require("minecraft:chest");
    if (chest.behavior != "chest" ||
        chest.backgroundTexture != "chest_generic_54" ||
        chest.width != 177.0f ||
        chest.height != 222.0f ||
        chest.slotGroups.size() != 3 ||
        !chest.progressBars.empty()) {
        return fail("chest UI should parse its static layout definition");
    }
    const ui::ContainerSlotGroupDef* chestSlots = findSlotGroup(chest, "chest");
    if (chestSlots == nullptr ||
        chestSlots->kind != ui::ContainerSlotGroupKind::Container ||
        chestSlots->columns != 9 ||
        chestSlots->rows != 3 ||
        chestSlots->firstSlot != 0) {
        return fail("chest UI should declare the chest slot grid");
    }

    const ui::ContainerUiDef& furnace = ui::ContainerUiRegistry::require("minecraft:furnace");
    if (furnace.behavior != "furnace" ||
        furnace.backgroundTexture != "furnace" ||
        furnace.slotGroups.size() != 5 ||
        furnace.progressBars.size() != 2) {
        return fail("furnace UI should parse slots and progress bars");
    }
    const ui::ContainerSlotGroupDef* furnaceFuel = findSlotGroup(furnace, "fuel");
    if (furnaceFuel == nullptr ||
        furnaceFuel->kind != ui::ContainerSlotGroupKind::Container ||
        furnaceFuel->firstSlot != 1 ||
        furnaceFuel->x != 56.0f ||
        furnaceFuel->y != 53.0f) {
        return fail("furnace UI should declare the fuel slot");
    }
    const ui::ContainerProgressDef* cookProgress = findProgress(furnace, "cook");
    if (cookProgress == nullptr ||
        cookProgress->kind != ui::ContainerProgressKind::Cook ||
        cookProgress->direction != "right" ||
        cookProgress->width != 24.0f) {
        return fail("furnace UI should declare cook progress metadata");
    }

    const ui::ContainerUiDef& crafting = ui::ContainerUiRegistry::require("minecraft:crafting_table");
    if (crafting.behavior != "crafting_table" ||
        crafting.backgroundTexture != "crafting_table" ||
        crafting.textureWidth != 256.0f ||
        crafting.textureHeight != 256.0f ||
        crafting.slotGroups.size() != 4) {
        return fail("crafting table UI should parse its layout definition");
    }
    const ui::ContainerSlotGroupDef* craftingInput = findSlotGroup(crafting, "crafting_input");
    if (craftingInput == nullptr ||
        craftingInput->kind != ui::ContainerSlotGroupKind::CraftingInput ||
        craftingInput->columns != 3 ||
        craftingInput->rows != 3) {
        return fail("crafting table UI should declare a 3x3 crafting input");
    }

    const BlockDef& chestBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:chest"));
    const BlockDef& furnaceBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:furnace"));
    const BlockDef& craftingBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:crafting_table"));
    if (&ui::ContainerUiRegistry::require(chestBlock.containerUi) != &chest ||
        &ui::ContainerUiRegistry::require(furnaceBlock.containerUi) != &furnace ||
        &ui::ContainerUiRegistry::require(craftingBlock.containerUi) != &crafting) {
        return fail("block containerUi bindings should resolve to registered UI definitions");
    }

    std::cout << "[container_ui_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
