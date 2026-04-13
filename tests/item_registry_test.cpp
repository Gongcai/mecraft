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

    const ItemDef& dirtItem = ItemRegistry::get(ItemRegistry::fromBlock(BlockType::DIRT));
    if (dirtItem.placeBlock != BlockType::DIRT) {
        return fail("dirt should place its source block via items.json");
    }

    const ItemDef& coal = ItemRegistry::get(ItemType::COAL);
    if (ItemRegistry::findByName("coal") != ItemType::COAL) {
        return fail("item name lookup should resolve coal from items.json");
    }
    if (coal.placeBlock != BlockType::AIR) {
        return fail("coal should not be directly placeable");
    }
    if (std::string(coal.iconTextureName) != "coal") {
        return fail("coal should provide iconTexture from items.json");
    }

    const ItemDef& ironPickaxe = ItemRegistry::get(ItemType::IRON_PICKAXE);
    if (ItemRegistry::findByName("iron_pickaxe") != ItemType::IRON_PICKAXE) {
        return fail("item name lookup should resolve iron_pickaxe from items.json");
    }
    if (!ironPickaxe.isTool || ironPickaxe.maxStack != 1) {
        return fail("iron pickaxe should be a non-stackable tool");
    }

    std::cout << "[item_registry_test] PASS\n";
    return EXIT_SUCCESS;
}

