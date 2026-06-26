#include <cstdlib>
#include <iostream>

#include "../src/game/inventory/ContainerBehaviorRegistry.h"
#include "../src/ui/inventory/ContainerUiRegistry.h"

namespace {
int fail(const char* message) {
    std::cerr << "[container_behavior_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

const ContainerSlotRuleDef* findSlotRule(const ContainerBehaviorDef& def, const std::string& id) {
    for (const ContainerSlotRuleDef& rule : def.slotRules) {
        if (rule.id == id) {
            return &rule;
        }
    }
    return nullptr;
}

const ContainerProcessorDef* findProcessor(const ContainerBehaviorDef& def, const std::string& id) {
    for (const ContainerProcessorDef& processor : def.processors) {
        if (processor.id == id) {
            return &processor;
        }
    }
    return nullptr;
}
}

int main() {
    ContainerBehaviorRegistry::init();
    ui::ContainerUiRegistry::init();

    const ContainerBehaviorDef& chest = ContainerBehaviorRegistry::require("minecraft:chest");
    if (chest.handler != "storage" ||
        chest.storage.kind != ContainerStorageKind::BlockEntity ||
        chest.storage.slots != 27 ||
        !chest.slotRules.empty() ||
        !chest.processors.empty()) {
        return fail("chest behavior should describe block-entity storage");
    }

    const ContainerBehaviorDef& furnace = ContainerBehaviorRegistry::require("minecraft:furnace");
    if (furnace.handler != "smelting" ||
        furnace.storage.kind != ContainerStorageKind::BlockEntity ||
        furnace.storage.slots != 3 ||
        furnace.slotRules.size() != 3 ||
        furnace.processors.size() != 1) {
        return fail("furnace behavior should describe smelting storage and processor metadata");
    }
    const ContainerSlotRuleDef* furnaceFuel = findSlotRule(furnace, "fuel");
    if (furnaceFuel == nullptr ||
        furnaceFuel->slot != 1 ||
        furnaceFuel->accepts != "fuel" ||
        furnaceFuel->outputOnly) {
        return fail("furnace behavior should declare the fuel slot rule");
    }
    const ContainerSlotRuleDef* furnaceOutput = findSlotRule(furnace, "output");
    if (furnaceOutput == nullptr ||
        furnaceOutput->slot != 2 ||
        !furnaceOutput->outputOnly) {
        return fail("furnace behavior should declare the output slot rule");
    }
    const ContainerProcessorDef* smelting = findProcessor(furnace, "smelting");
    if (smelting == nullptr ||
        smelting->type != "smelting" ||
        smelting->inputSlot != 0 ||
        smelting->fuelSlot != 1 ||
        smelting->outputSlot != 2) {
        return fail("furnace behavior should declare smelting processor slots");
    }

    const ContainerBehaviorDef& crafting = ContainerBehaviorRegistry::require("minecraft:crafting_table");
    if (crafting.handler != "crafting" ||
        crafting.storage.kind != ContainerStorageKind::Transient ||
        crafting.storage.slots != 10) {
        return fail("crafting table behavior should describe transient crafting storage");
    }
    const ContainerProcessorDef* craftingProcessor = findProcessor(crafting, "crafting");
    if (craftingProcessor == nullptr ||
        craftingProcessor->type != "crafting" ||
        craftingProcessor->gridSize != 3 ||
        craftingProcessor->resultSlot != 9) {
        return fail("crafting table behavior should declare crafting processor metadata");
    }

    if (&ContainerBehaviorRegistry::require(ui::ContainerUiRegistry::require("minecraft:chest").behavior) != &chest ||
        &ContainerBehaviorRegistry::require(ui::ContainerUiRegistry::require("minecraft:furnace").behavior) != &furnace ||
        &ContainerBehaviorRegistry::require(ui::ContainerUiRegistry::require("minecraft:crafting_table").behavior) != &crafting) {
        return fail("container UI behavior bindings should resolve to behavior definitions");
    }

    std::cout << "[container_behavior_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
