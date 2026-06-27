#include <cstdlib>
#include <iostream>

#include "Paths.h"
#include "crafting/SmeltingSystem.h"
#include "game/inventory/MachineInventoryStore.h"
#include "item/Item.h"

namespace {
int fail(const char* message) {
    std::cerr << "[smelting_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

bool item(const char* name, ItemID& outId) {
    return ItemRegistry::tryGetIdByName(name, outId);
}
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    SmeltingSystem smelting;
    smelting.loadRecipes(SMELTING_CONFIG_PATH);

    ItemID rawIron = 0;
    ItemID ironIngot = 0;
    ItemID coal = 0;
    ItemID sand = 0;
    ItemID glass = 0;
    if (!item("minecraft:raw_iron", rawIron) ||
        !item("minecraft:iron_ingot", ironIngot) ||
        !item("minecraft:coal", coal) ||
        !item("minecraft:sand", sand) ||
        !item("minecraft:glass", glass)) {
        return fail("expected smelting test item ids to exist");
    }

    const SmeltingRecipe* ironRecipe = smelting.findRecipe(rawIron);
    if (ironRecipe == nullptr || ironRecipe->result != ironIngot) {
        return fail("raw iron should smelt into iron ingot");
    }
    if (!smelting.isFuel(coal) || smelting.fuelSeconds(coal) < 79.9f) {
        return fail("coal should be an 80 second fuel");
    }

    const MachineSmeltingProcessor processor{
        MachineInventory::DEFAULT_SMELTING_INPUT_SLOT,
        MachineInventory::DEFAULT_SMELTING_FUEL_SLOT,
        MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT,
    };

    MachineInventory furnace(3);
    furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT, ItemStack{rawIron, 2, 0});
    furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT, ItemStack{coal, 1, 0});
    furnace.tick(10.0f, smelting, processor);

    const ItemStack firstOutput = furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT);
    if (firstOutput.itemId != ironIngot || firstOutput.count != 1) {
        return fail("ten seconds should smelt one raw iron");
    }
    if (furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT).count != 1) {
        return fail("smelting should consume one input item");
    }
    if (!furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT).isEmpty()) {
        return fail("starting a burn should consume one coal item");
    }

    furnace.tick(10.0f, smelting, processor);
    const ItemStack secondOutput = furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT);
    if (secondOutput.itemId != ironIngot || secondOutput.count != 2) {
        return fail("remaining burn time should smelt the second raw iron");
    }

    MachineInventory blocked(3);
    blocked.setSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT, ItemStack{sand, 1, 0});
    blocked.setSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT, ItemStack{coal, 1, 0});
    blocked.setSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT, ItemStack{ironIngot, 1, 0});
    blocked.tick(10.0f, smelting, processor);
    if (blocked.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT).itemId != ironIngot ||
        blocked.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT).count != 1) {
        return fail("blocked output should preserve the existing output stack");
    }
    if (blocked.getSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT).count != 1) {
        return fail("blocked output should not consume fuel");
    }

    MachineInventory sandFurnace(3);
    sandFurnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT, ItemStack{sand, 1, 0});
    sandFurnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT, ItemStack{coal, 1, 0});
    sandFurnace.tick(10.0f, smelting, processor);
    const ItemStack glassOutput = sandFurnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT);
    if (glassOutput.itemId != glass || glassOutput.count != 1) {
        return fail("sand should smelt into glass");
    }

    std::cout << "[smelting_system_test] PASS\n";
    return EXIT_SUCCESS;
}
