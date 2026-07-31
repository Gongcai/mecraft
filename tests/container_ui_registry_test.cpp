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

bool hasCenteredAdaptivePanel(const ui::ContainerUiDef& def) {
    return def.anchorX == 0.5f && def.anchorY == 0.5f && def.pivotX == 0.5f && def.pivotY == 0.5f &&
           def.offsetX == 0.0f && def.offsetY == 0.0f && def.scale == 2.0f && def.fitPadding == 8.0f;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ui::ContainerUiRegistry::init();

    const ui::ContainerUiDef& chest = ui::ContainerUiRegistry::require("minecraft:chest");
    if (chest.behavior != "minecraft:chest" || chest.backgroundTexture != "generic_54" ||
        chest.backgroundTexturePath != "textures/gui/generic_54.png" || chest.width != 176.0f ||
        chest.height != 222.0f || !hasCenteredAdaptivePanel(chest) || chest.slotGroups.size() != 3 ||
        !chest.progressBars.empty()) {
        return fail("chest UI should parse its static layout definition");
    }
    const ui::ContainerSlotGroupDef* chestSlots = findSlotGroup(chest, "chest");
    if (chestSlots == nullptr || chestSlots->kind != ui::ContainerSlotGroupKind::Container ||
        chestSlots->columns != 9 || chestSlots->rows != 3 || chestSlots->firstSlot != 0) {
        return fail("chest UI should declare the chest slot grid");
    }

    const ui::ContainerUiDef& barrel = ui::ContainerUiRegistry::require("minecraft:barrel");
    if (barrel.behavior != "minecraft:barrel" || barrel.backgroundTexture != "generic_54" ||
        barrel.backgroundTexturePath != "textures/gui/generic_54.png" || barrel.width != 176.0f ||
        barrel.height != 222.0f || !hasCenteredAdaptivePanel(barrel) || barrel.slotGroups.size() != 3 ||
        !barrel.progressBars.empty()) {
        return fail("barrel UI should parse its static storage layout definition");
    }
    const ui::ContainerSlotGroupDef* barrelSlots = findSlotGroup(barrel, "barrel");
    if (barrelSlots == nullptr || barrelSlots->kind != ui::ContainerSlotGroupKind::Container ||
        barrelSlots->columns != 9 || barrelSlots->rows != 3 || barrelSlots->firstSlot != 0) {
        return fail("barrel UI should declare the barrel slot grid");
    }

    const ui::ContainerUiDef& dispenser = ui::ContainerUiRegistry::require("minecraft:dispenser");
    if (dispenser.behavior != "minecraft:dispenser" || dispenser.backgroundTexture != "dispenser" ||
        dispenser.backgroundTexturePath != "textures/gui/dispenser.png" || dispenser.width != 176.0f ||
        dispenser.height != 166.0f || !hasCenteredAdaptivePanel(dispenser) || dispenser.slotGroups.size() != 3 ||
        !dispenser.progressBars.empty()) {
        return fail("dispenser UI should parse its storage layout definition");
    }
    const ui::ContainerSlotGroupDef* dispenserSlots = findSlotGroup(dispenser, "dispenser");
    if (dispenserSlots == nullptr || dispenserSlots->kind != ui::ContainerSlotGroupKind::Container ||
        dispenserSlots->columns != 3 || dispenserSlots->rows != 3 || dispenserSlots->firstSlot != 0) {
        return fail("dispenser UI should declare the 3x3 dispenser slot grid");
    }

    const ui::ContainerUiDef& dropper = ui::ContainerUiRegistry::require("minecraft:dropper");
    if (dropper.behavior != "minecraft:dropper" || dropper.backgroundTexture != "dispenser" ||
        dropper.backgroundTexturePath != "textures/gui/dispenser.png" || dropper.width != 176.0f ||
        dropper.height != 166.0f || !hasCenteredAdaptivePanel(dropper) || dropper.slotGroups.size() != 3 ||
        !dropper.progressBars.empty()) {
        return fail("dropper UI should parse its storage layout definition");
    }
    const ui::ContainerSlotGroupDef* dropperSlots = findSlotGroup(dropper, "dropper");
    if (dropperSlots == nullptr || dropperSlots->kind != ui::ContainerSlotGroupKind::Container ||
        dropperSlots->columns != 3 || dropperSlots->rows != 3 || dropperSlots->firstSlot != 0) {
        return fail("dropper UI should declare the 3x3 dropper slot grid");
    }

    const ui::ContainerUiDef& hopper = ui::ContainerUiRegistry::require("minecraft:hopper");
    if (hopper.behavior != "minecraft:hopper" || hopper.backgroundTexture != "hopper" ||
        hopper.backgroundTexturePath != "textures/gui/hopper.png" || hopper.width != 176.0f ||
        hopper.height != 133.0f || !hasCenteredAdaptivePanel(hopper) || hopper.slotGroups.size() != 3 ||
        !hopper.progressBars.empty()) {
        return fail("hopper UI should parse its 5-slot storage layout definition");
    }
    const ui::ContainerSlotGroupDef* hopperSlots = findSlotGroup(hopper, "hopper");
    if (hopperSlots == nullptr || hopperSlots->kind != ui::ContainerSlotGroupKind::Container ||
        hopperSlots->columns != 5 || hopperSlots->rows != 1 || hopperSlots->firstSlot != 0 || hopperSlots->x != 44.0f ||
        hopperSlots->y != 20.0f) {
        return fail("hopper UI should declare the 5-slot hopper row");
    }

    const ui::ContainerUiDef& furnace = ui::ContainerUiRegistry::require("minecraft:furnace");
    if (furnace.behavior != "minecraft:furnace" || furnace.backgroundTexture != "furnace" ||
        furnace.backgroundTexturePath != "textures/gui/furnace.png" || !hasCenteredAdaptivePanel(furnace) ||
        furnace.slotGroups.size() != 5 || furnace.progressBars.size() != 2) {
        return fail("furnace UI should parse slots and progress bars");
    }
    const ui::ContainerSlotGroupDef* furnaceFuel = findSlotGroup(furnace, "fuel");
    if (furnaceFuel == nullptr || furnaceFuel->kind != ui::ContainerSlotGroupKind::Container ||
        furnaceFuel->firstSlot != 1 || furnaceFuel->x != 56.0f || furnaceFuel->y != 53.0f) {
        return fail("furnace UI should declare the fuel slot");
    }
    const ui::ContainerProgressDef* cookProgress = findProgress(furnace, "cook");
    if (cookProgress == nullptr || cookProgress->kind != ui::ContainerProgressKind::Cook ||
        cookProgress->direction != "right" || cookProgress->width != 24.0f) {
        return fail("furnace UI should declare cook progress metadata");
    }

    const ui::ContainerUiDef& crafting = ui::ContainerUiRegistry::require("minecraft:crafting_table");
    if (crafting.behavior != "minecraft:crafting_table" || crafting.backgroundTexture != "crafting_table" ||
        crafting.backgroundTexturePath != "textures/gui/crafting_table.png" || crafting.textureWidth != 256.0f ||
        crafting.textureHeight != 256.0f || !hasCenteredAdaptivePanel(crafting) || crafting.slotGroups.size() != 4) {
        return fail("crafting table UI should parse its layout definition");
    }
    const ui::ContainerSlotGroupDef* craftingInput = findSlotGroup(crafting, "crafting_input");
    if (craftingInput == nullptr || craftingInput->kind != ui::ContainerSlotGroupKind::CraftingInput ||
        craftingInput->columns != 3 || craftingInput->rows != 3 || craftingInput->x != 29.5f ||
        craftingInput->y != 16.5f) {
        return fail("crafting table UI should declare a 3x3 crafting input");
    }
    const ui::ContainerSlotGroupDef* craftingResult = findSlotGroup(crafting, "crafting_result");
    if (craftingResult == nullptr || craftingResult->kind != ui::ContainerSlotGroupKind::CraftingResult ||
        craftingResult->firstSlot != 9 || craftingResult->x != 123.5f || craftingResult->y != 34.5f) {
        return fail("crafting table UI should declare the result slot position");
    }
    const ui::ContainerSlotGroupDef* craftingPlayerInventory = findSlotGroup(crafting, "player_inventory");
    const ui::ContainerSlotGroupDef* craftingHotbar = findSlotGroup(crafting, "hotbar");
    if (craftingPlayerInventory == nullptr || craftingHotbar == nullptr ||
        craftingPlayerInventory->kind != ui::ContainerSlotGroupKind::PlayerInventory ||
        craftingHotbar->kind != ui::ContainerSlotGroupKind::PlayerInventory ||
        craftingPlayerInventory->firstSlot != 9 || craftingHotbar->firstSlot != 0 ||
        craftingPlayerInventory->y != 84.0f || craftingHotbar->y != 142.0f) {
        return fail("crafting table UI should declare player inventory and hotbar positions");
    }

    const BlockDef& chestBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:chest"));
    const BlockDef& barrelBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:barrel"));
    const BlockDef& dispenserBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:dispenser"));
    const BlockDef& dropperBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:dropper"));
    const BlockDef& hopperBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:hopper"));
    const BlockDef& furnaceBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:furnace"));
    const BlockDef& craftingBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:crafting_table"));
    if (&ui::ContainerUiRegistry::require(chestBlock.containerUi) != &chest ||
        &ui::ContainerUiRegistry::require(barrelBlock.containerUi) != &barrel ||
        &ui::ContainerUiRegistry::require(dispenserBlock.containerUi) != &dispenser ||
        &ui::ContainerUiRegistry::require(dropperBlock.containerUi) != &dropper ||
        &ui::ContainerUiRegistry::require(hopperBlock.containerUi) != &hopper ||
        &ui::ContainerUiRegistry::require(furnaceBlock.containerUi) != &furnace ||
        &ui::ContainerUiRegistry::require(craftingBlock.containerUi) != &crafting) {
        return fail("block containerUi bindings should resolve to registered UI definitions");
    }

    std::cout << "[container_ui_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
