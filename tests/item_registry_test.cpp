#include <cstdlib>
#include <iostream>

#include "../src/item/Item.h"

namespace {
int fail(const char* message) {
    std::cerr << "[item_registry_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    const ItemDef& dirtItem = ItemRegistry::get(ItemRegistry::fromBlock(BlockIds::DIRT));
    if (dirtItem.placeBlock != BlockIds::DIRT) {
        return fail("dirt should place its source block via block fallback");
    }
    if (ItemRegistry::findByName("dirt") != ItemRegistry::fromBlock(BlockIds::DIRT)) {
        return fail("block name lookup should resolve dirt via BlockRegistry");
    }

    const ItemID blueWoolItem = ItemRegistry::fromBlock(BlockIds::BLUE_WOOL);
    if (blueWoolItem == ItemIds::AIR) {
        return fail("blue_wool should synthesize a block-backed item without items.json");
    }
    if (ItemRegistry::findByName("blue_wool") != blueWoolItem) {
        return fail("block name lookup should resolve synthesized block items");
    }
    const ItemDef& blueWool = ItemRegistry::get(blueWoolItem);
    if (blueWool.placeBlock != BlockIds::BLUE_WOOL || blueWool.renderBlock != BlockIds::BLUE_WOOL) {
        return fail("blue_wool synthesized item should render and place its source block");
    }

    const ItemDef& coal = ItemRegistry::get(ItemIds::COAL);
    if (ItemRegistry::findByName("coal") != ItemIds::COAL) {
        return fail("item name lookup should resolve coal from items.json");
    }
    if (coal.placeBlock != BlockIds::AIR) {
        return fail("coal should not be directly placeable");
    }
    if (std::string(coal.iconTextureName) != "coal") {
        return fail("coal should provide iconTexture from items.json");
    }

    const ItemDef& ironPickaxe = ItemRegistry::get(ItemIds::IRON_PICKAXE);
    if (ItemRegistry::findByName("iron_pickaxe") != ItemIds::IRON_PICKAXE) {
        return fail("item name lookup should resolve iron_pickaxe from items.json");
    }
    if (!ironPickaxe.isTool || ironPickaxe.maxStack != 1) {
        return fail("iron pickaxe should be a non-stackable tool");
    }

    // BlockDropTable tests
    if (BlockDropTable::getDropItem(BlockIds::COAL_ORE) != ItemIds::COAL) {
        return fail("coal_ore should drop coal item");
    }
    if (BlockDropTable::getDropItem(BlockIds::DIRT) != ItemRegistry::fromBlock(BlockIds::DIRT)) {
        return fail("dirt should drop itself by default");
    }
    if (BlockDropTable::getDropItem(BlockIds::AIR) != ItemIds::AIR) {
        return fail("air should drop air (nothing)");
    }

    std::cout << "[item_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
