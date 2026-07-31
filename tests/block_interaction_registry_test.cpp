#include <cstdlib>
#include <iostream>

#include "../src/game/interaction/BlockInteractionRegistry.h"
#include "../src/world/block/Block.h"

namespace {
int fail(const char* message) {
    std::cerr << "[block_interaction_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main() {
    BlockRegistry::init(nullptr);
    BlockInteractionRegistry::init();

    const BlockInteractionDef& lever = BlockInteractionRegistry::require("minecraft:toggle_powered");
    if (lever.action != BlockInteractionActionKind::ToggleBooleanProperty || lever.property != "powered" ||
        lever.falseValue != "false" || lever.trueValue != "true") {
        return fail("lever interaction should declare a powered boolean toggle");
    }

    const BlockInteractionDef& button = BlockInteractionRegistry::require("minecraft:button_press");
    if (button.action != BlockInteractionActionKind::SetPropertyOnce || button.property != "powered" ||
        button.setValue != "true") {
        return fail("button interaction should declare a one-way powered press");
    }

    const BlockInteractionDef& repeater = BlockInteractionRegistry::require("minecraft:cycle_repeater_delay");
    if (repeater.action != BlockInteractionActionKind::CycleProperty || repeater.property != "delay" ||
        repeater.cycleValues.size() != 4 || repeater.cycleValues[0] != "1" || repeater.cycleValues[3] != "4") {
        return fail("repeater interaction should cycle delay values from data");
    }

    const BlockInteractionDef& door = BlockInteractionRegistry::require("minecraft:toggle_door_open");
    if (door.action != BlockInteractionActionKind::ToggleBooleanProperty || door.property != "open" ||
        door.partnerSync != BlockInteractionPartnerSync::DoorOpen) {
        return fail("door interaction should declare open toggle with door partner sync");
    }

    const BlockDef& leverBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:lever"));
    const BlockDef& stoneButtonBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:stone_button"));
    const BlockDef& oakButtonBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_button"));
    const BlockDef& repeaterBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:repeater"));
    const BlockDef& comparatorBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:comparator"));
    const BlockDef& doorBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_door"));
    const BlockDef& trapdoorBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_trapdoor"));
    const BlockDef& fenceGateBlock = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:oak_fence_gate"));

    if (leverBlock.interaction != "minecraft:toggle_powered" ||
        stoneButtonBlock.interaction != "minecraft:button_press" ||
        oakButtonBlock.interaction != "minecraft:button_press" ||
        repeaterBlock.interaction != "minecraft:cycle_repeater_delay" ||
        comparatorBlock.interaction != "minecraft:toggle_comparator_mode" ||
        doorBlock.interaction != "minecraft:toggle_door_open" || trapdoorBlock.interaction != "minecraft:toggle_open" ||
        fenceGateBlock.interaction != "minecraft:toggle_open") {
        return fail("right-click control blocks should bind interactions from blocks.json");
    }

    std::cout << "[block_interaction_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
