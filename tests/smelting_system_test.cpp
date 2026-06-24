#include <cstdlib>
#include <iostream>

#include "Paths.h"
#include "crafting/SmeltingSystem.h"
#include "game/inventory/FurnaceInventoryStore.h"
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

    FurnaceInventory furnace;
    furnace.setSlotStack(FurnaceInventory::INPUT_SLOT, ItemStack{rawIron, 2, 0});
    furnace.setSlotStack(FurnaceInventory::FUEL_SLOT, ItemStack{coal, 1, 0});
    furnace.tick(10.0f, smelting);

    const ItemStack firstOutput = furnace.getSlotStack(FurnaceInventory::OUTPUT_SLOT);
    if (firstOutput.itemId != ironIngot || firstOutput.count != 1) {
        return fail("ten seconds should smelt one raw iron");
    }
    if (furnace.getSlotStack(FurnaceInventory::INPUT_SLOT).count != 1) {
        return fail("smelting should consume one input item");
    }
    if (!furnace.getSlotStack(FurnaceInventory::FUEL_SLOT).isEmpty()) {
        return fail("starting a burn should consume one coal item");
    }

    furnace.tick(10.0f, smelting);
    const ItemStack secondOutput = furnace.getSlotStack(FurnaceInventory::OUTPUT_SLOT);
    if (secondOutput.itemId != ironIngot || secondOutput.count != 2) {
        return fail("remaining burn time should smelt the second raw iron");
    }

    FurnaceInventory blocked;
    blocked.setSlotStack(FurnaceInventory::INPUT_SLOT, ItemStack{sand, 1, 0});
    blocked.setSlotStack(FurnaceInventory::FUEL_SLOT, ItemStack{coal, 1, 0});
    blocked.setSlotStack(FurnaceInventory::OUTPUT_SLOT, ItemStack{ironIngot, 1, 0});
    blocked.tick(10.0f, smelting);
    if (blocked.getSlotStack(FurnaceInventory::OUTPUT_SLOT).itemId != ironIngot ||
        blocked.getSlotStack(FurnaceInventory::OUTPUT_SLOT).count != 1) {
        return fail("blocked output should preserve the existing output stack");
    }
    if (blocked.getSlotStack(FurnaceInventory::FUEL_SLOT).count != 1) {
        return fail("blocked output should not consume fuel");
    }

    FurnaceInventory sandFurnace;
    sandFurnace.setSlotStack(FurnaceInventory::INPUT_SLOT, ItemStack{sand, 1, 0});
    sandFurnace.setSlotStack(FurnaceInventory::FUEL_SLOT, ItemStack{coal, 1, 0});
    sandFurnace.tick(10.0f, smelting);
    const ItemStack glassOutput = sandFurnace.getSlotStack(FurnaceInventory::OUTPUT_SLOT);
    if (glassOutput.itemId != glass || glassOutput.count != 1) {
        return fail("sand should smelt into glass");
    }

    std::cout << "[smelting_system_test] PASS\n";
    return EXIT_SUCCESS;
}
