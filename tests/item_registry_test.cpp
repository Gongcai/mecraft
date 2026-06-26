#include <cstdlib>
#include <iostream>

#include "../src/item/Item.h"
#include "../src/ui/ItemIconPolicy.h"

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
        return fail("dirt should place its source block");
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

    const ItemID chestItem = ItemRegistry::fromBlock(BlockIds::CHEST);
    if (chestItem == ItemIds::AIR) {
        return fail("chest should synthesize a block-backed item");
    }
    if (ItemRegistry::findByName("chest") != chestItem) {
        return fail("block name lookup should resolve chest item");
    }
    if (ItemRegistry::toPlaceBlock(chestItem) != BlockIds::CHEST ||
        ItemRegistry::toRenderBlock(chestItem) != BlockIds::CHEST) {
        return fail("chest item should render and place chest block");
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
    if (!ItemRegistry::hasTag(ItemIds::COAL, NamespacedId("minecraft", "coals"))) {
        return fail("coal should expose the coals item tag");
    }

    const ItemDef& apple = ItemRegistry::get(ItemIds::APPLE);
    if (ItemRegistry::findByName("apple") != ItemIds::APPLE) {
        return fail("item name lookup should resolve apple from items.json");
    }
    if (apple.placeBlock != BlockIds::AIR || apple.renderBlock != BlockIds::AIR) {
        return fail("apple should be a non-placeable pure item");
    }
    if (std::string(apple.iconTextureName) != "apple") {
        return fail("apple should use the apple item texture");
    }

    const ItemDef& ironPickaxe = ItemRegistry::get(ItemIds::IRON_PICKAXE);
    if (ItemRegistry::findByName("iron_pickaxe") != ItemIds::IRON_PICKAXE) {
        return fail("item name lookup should resolve iron_pickaxe from items.json");
    }
    if (!ironPickaxe.isTool || ironPickaxe.maxStack != 1 ||
        ironPickaxe.toolKind != "pickaxe" || ironPickaxe.toolTier != 2 ||
        ironPickaxe.toolEfficiency < 5.9f || ironPickaxe.toolEfficiency > 6.1f) {
        return fail("iron pickaxe should be a non-stackable tool");
    }

    const ItemDef& ironHoe = ItemRegistry::get(ItemIds::IRON_HOE);
    if (ItemRegistry::findByName("iron_hoe") != ItemIds::IRON_HOE) {
        return fail("item name lookup should resolve iron_hoe from items.json");
    }
    if (!ironHoe.isTool || ironHoe.maxStack != 1 ||
        ironHoe.toolKind != "hoe" || ironHoe.toolTier != 2 ||
        ironHoe.placeBlock != BlockIds::AIR || ironHoe.renderBlock != BlockIds::AIR ||
        std::string(ironHoe.iconTextureName) != "iron_hoe") {
        return fail("iron hoe should be a non-placeable hoe tool with an item texture");
    }

    const ItemID craftingTableItem = ItemRegistry::findByName("crafting_table");
    const BlockID craftingTableBlock = BlockRegistry::findByName("crafting_table");
    if (craftingTableItem == ItemIds::AIR || craftingTableBlock == BlockIds::AIR) {
        return fail("crafting_table should register both block and item IDs");
    }
    if (ItemRegistry::toPlaceBlock(craftingTableItem) != craftingTableBlock) {
        return fail("crafting_table item should place the crafting table block");
    }

    const ItemID cherryPlanksItem = ItemRegistry::findByName("cherry_planks");
    const BlockID cherryPlanksBlock = BlockRegistry::findByName("cherry_planks");
    if (cherryPlanksItem == ItemIds::AIR || cherryPlanksBlock == BlockIds::AIR) {
        return fail("cherry_planks should register both block and item IDs");
    }
    if (!BlockRegistry::hasTag(cherryPlanksBlock, NamespacedId("minecraft", "planks")) ||
        !ItemRegistry::hasTag(cherryPlanksItem, NamespacedId("minecraft", "planks"))) {
        return fail("block-backed planks item should inherit the planks tag");
    }

    const ItemID furnaceItem = ItemRegistry::findByName("furnace");
    const BlockID furnaceBlock = BlockRegistry::findByName("furnace");
    if (furnaceItem == ItemIds::AIR || furnaceBlock == BlockIds::AIR) {
        return fail("furnace should register both block and item IDs");
    }
    if (ItemRegistry::toPlaceBlock(furnaceItem) != furnaceBlock) {
        return fail("furnace item should place the furnace block");
    }

    const ItemID vineItem = ItemRegistry::findByName("vine");
    const BlockID vineBlock = BlockRegistry::findByName("vine");
    if (vineItem == ItemIds::AIR || vineBlock == BlockIds::AIR) {
        return fail("vine should register both block and item IDs");
    }
    const ItemDef& vine = ItemRegistry::get(vineItem);
    if (vine.renderBlock != vineBlock || std::string(vine.iconTextureName) != "vine") {
        return fail("vine item should use its item texture and render block");
    }
    if (ui::shouldUseBakedBlockIcon(vine)) {
        return fail("vine item icon should use its tinted item texture");
    }

    const ItemID redBedItem = ItemRegistry::findByName("red_bed");
    const BlockID redBedBlock = BlockRegistry::findByName("red_bed");
    if (redBedItem == ItemIds::AIR || redBedBlock == BlockIds::AIR) {
        return fail("red_bed item and block should be registered");
    }
    const ItemDef& redBed = ItemRegistry::get(redBedItem);
    if (redBed.placeBlock != redBedBlock ||
        redBed.renderBlock != redBedBlock ||
        std::string(redBed.iconTextureName) != "bed") {
        return fail("red_bed item should place the bed block and use the bed item texture");
    }
    if (ui::shouldUseBakedBlockIcon(redBed)) {
        return fail("red_bed item icon should use its explicit item texture");
    }

    const ItemID redstoneItem = ItemRegistry::findByName("redstone");
    const BlockID redstoneWireBlock = BlockRegistry::findByName("redstone_wire");
    if (redstoneItem == ItemIds::AIR || redstoneWireBlock == BlockIds::AIR) {
        return fail("redstone item and redstone_wire block should be registered");
    }
    const ItemDef& redstone = ItemRegistry::get(redstoneItem);
    if (ItemRegistry::toPlaceBlock(redstoneItem) != redstoneWireBlock ||
        ItemRegistry::toRenderBlock(redstoneItem) != BlockIds::AIR ||
        std::string(redstone.iconTextureName) != "redstone") {
        return fail("redstone item should place redstone_wire while keeping its powder icon");
    }

    const ItemID redstoneTorchItem = ItemRegistry::findByName("redstone_torch");
    const BlockID redstoneTorchBlock = BlockRegistry::findByName("redstone_torch");
    if (redstoneTorchItem == ItemIds::AIR || redstoneTorchBlock == BlockIds::AIR) {
        return fail("redstone_torch item and block should be registered");
    }
    const ItemDef& redstoneTorch = ItemRegistry::get(redstoneTorchItem);
    if (redstoneTorch.placeBlock != redstoneTorchBlock ||
        redstoneTorch.renderBlock != redstoneTorchBlock ||
        std::string(redstoneTorch.iconTextureName) != "redstone_torch") {
        return fail("redstone_torch item should place the torch block and use its item texture");
    }

    const ItemID repeaterItem = ItemRegistry::findByName("repeater");
    const ItemID comparatorItem = ItemRegistry::findByName("comparator");
    const ItemID hopperItem = ItemRegistry::findByName("hopper");
    const BlockID repeaterBlock = BlockRegistry::findByName("repeater");
    const BlockID comparatorBlock = BlockRegistry::findByName("comparator");
    const BlockID hopperBlock = BlockRegistry::findByName("hopper");
    if (repeaterItem == ItemIds::AIR ||
        comparatorItem == ItemIds::AIR ||
        hopperItem == ItemIds::AIR ||
        repeaterBlock == BlockIds::AIR ||
        comparatorBlock == BlockIds::AIR ||
        hopperBlock == BlockIds::AIR) {
        return fail("redstone component items and blocks should be registered");
    }
    const ItemDef& repeater = ItemRegistry::get(repeaterItem);
    const ItemDef& comparator = ItemRegistry::get(comparatorItem);
    const ItemDef& hopper = ItemRegistry::get(hopperItem);
    if (repeater.placeBlock != repeaterBlock ||
        comparator.placeBlock != comparatorBlock ||
        hopper.placeBlock != hopperBlock ||
        std::string(repeater.iconTextureName) != "repeater" ||
        std::string(comparator.iconTextureName) != "comparator" ||
        std::string(hopper.iconTextureName) != "hopper") {
        return fail("redstone component items should place their blocks and use explicit item textures");
    }
    if (ui::shouldUseBakedBlockIcon(repeater) ||
        ui::shouldUseBakedBlockIcon(comparator) ||
        ui::shouldUseBakedBlockIcon(hopper)) {
        return fail("redstone component item icons should use their item textures");
    }

    const ItemID bucketItem = ItemRegistry::findByName("bucket");
    const ItemID waterBucketItem = ItemRegistry::findByName("water_bucket");
    if (bucketItem != ItemIds::BUCKET || waterBucketItem != ItemIds::WATER_BUCKET) {
        return fail("bucket and water_bucket should resolve to their builtin item ids");
    }
    const ItemDef& bucket = ItemRegistry::get(ItemIds::BUCKET);
    const ItemDef& waterBucket = ItemRegistry::get(ItemIds::WATER_BUCKET);
    if (bucket.maxStack != 1 ||
        waterBucket.maxStack != 1 ||
        bucket.placeBlock != BlockIds::AIR ||
        waterBucket.placeBlock != BlockIds::AIR ||
        std::string(bucket.iconTextureName) != "bucket" ||
        std::string(waterBucket.iconTextureName) != "water_bucket") {
        return fail("bucket items should be non-stackable non-placeable items with explicit item textures");
    }

    const ItemID oakDoorItem = ItemRegistry::findByName("oak_door");
    const ItemID oakTrapdoorItem = ItemRegistry::findByName("oak_trapdoor");
    const ItemID oakFenceGateItem = ItemRegistry::findByName("oak_fence_gate");
    if (oakDoorItem == ItemIds::AIR ||
        oakTrapdoorItem == ItemIds::AIR ||
        oakFenceGateItem == ItemIds::AIR) {
        return fail("oak door, trapdoor, and fence gate items should be registered");
    }
    const ItemDef& oakDoor = ItemRegistry::get(oakDoorItem);
    const ItemDef& oakTrapdoor = ItemRegistry::get(oakTrapdoorItem);
    const ItemDef& oakFenceGate = ItemRegistry::get(oakFenceGateItem);
    if (oakDoor.placeBlock != BlockIds::OAK_DOOR ||
        oakTrapdoor.placeBlock != BlockIds::OAK_TRAPDOOR ||
        oakFenceGate.placeBlock != BlockIds::OAK_FENCE_GATE ||
        oakDoor.renderBlock != BlockIds::OAK_DOOR ||
        oakTrapdoor.renderBlock != BlockIds::OAK_TRAPDOOR ||
        oakFenceGate.renderBlock != BlockIds::OAK_FENCE_GATE ||
        std::string(oakDoor.iconTextureName) != "oak_door" ||
        std::string(oakTrapdoor.iconTextureName) != "oak_trapdoor" ||
        std::string(oakFenceGate.iconTextureName) != "oak_planks") {
        return fail("oak door, trapdoor, and fence gate items should place/render their blocks and use declared icons");
    }
    if (ui::shouldUseBakedBlockIcon(oakDoor) ||
        ui::shouldUseBakedBlockIcon(oakTrapdoor) ||
        ui::shouldUseBakedBlockIcon(oakFenceGate)) {
        return fail("oak door, trapdoor, and fence gate item icons should use explicit item textures");
    }

    const ItemID wildflowersItem = ItemRegistry::findByName("wildflowers");
    const ItemID leafLitterItem = ItemRegistry::findByName("leaf_litter");
    const ItemID glowLichenItem = ItemRegistry::findByName("glow_lichen");
    const BlockID wildflowersBlock = BlockRegistry::findByName("wildflowers");
    const BlockID leafLitterBlock = BlockRegistry::findByName("leaf_litter");
    const BlockID glowLichenBlock = BlockRegistry::findByName("glow_lichen");
    if (wildflowersItem == ItemIds::AIR ||
        leafLitterItem == ItemIds::AIR ||
        glowLichenItem == ItemIds::AIR ||
        wildflowersBlock == BlockIds::AIR ||
        leafLitterBlock == BlockIds::AIR ||
        glowLichenBlock == BlockIds::AIR) {
        return fail("new face plane decoration items and blocks should be registered");
    }
    const ItemDef& wildflowers = ItemRegistry::get(wildflowersItem);
    const ItemDef& leafLitter = ItemRegistry::get(leafLitterItem);
    const ItemDef& glowLichen = ItemRegistry::get(glowLichenItem);
    if (wildflowers.placeBlock != wildflowersBlock ||
        leafLitter.placeBlock != leafLitterBlock ||
        glowLichen.placeBlock != glowLichenBlock ||
        std::string(wildflowers.iconTextureName) != "wildflowers" ||
        std::string(leafLitter.iconTextureName) != "leaf_litter" ||
        std::string(glowLichen.iconTextureName) != "glow_lichen") {
        return fail("new face plane items should place their block and use explicit item textures");
    }
    if (ui::shouldUseBakedBlockIcon(wildflowers) ||
        ui::shouldUseBakedBlockIcon(leafLitter) ||
        ui::shouldUseBakedBlockIcon(glowLichen)) {
        return fail("new face plane item icons should use their item textures");
    }

    const ItemID oakLeavesItem = ItemRegistry::findByName("oak_leaves");
    if (oakLeavesItem == ItemIds::AIR) {
        return fail("oak_leaves should register a block-backed item");
    }
    if (!ui::shouldUseBakedBlockIcon(ItemRegistry::get(oakLeavesItem))) {
        return fail("oak_leaves should keep its tinted baked block icon");
    }

    const ItemID wheatSeedsItem = ItemRegistry::findByName("wheat_seeds");
    const BlockID wheatCropBlock = BlockRegistry::findByName("wheat");
    if (wheatSeedsItem == ItemIds::AIR || wheatCropBlock == BlockIds::AIR) {
        return fail("wheat seeds item and wheat crop block should be registered");
    }
    const ItemDef& wheatSeeds = ItemRegistry::get(wheatSeedsItem);
    if (std::string(wheatSeeds.iconTextureName) != "wheat_seeds" ||
        ItemRegistry::toPlaceBlock(wheatSeedsItem) != wheatCropBlock ||
        ItemRegistry::toRenderBlock(wheatSeedsItem) != BlockIds::AIR) {
        return fail("wheat seeds should place wheat while keeping its item icon");
    }

    const ItemID carrotItem = ItemRegistry::findByName("carrot");
    const ItemID potatoItem = ItemRegistry::findByName("potato");
    const BlockID carrotsBlock = BlockRegistry::findByName("carrots");
    const BlockID potatoesBlock = BlockRegistry::findByName("potatoes");
    if (carrotItem == ItemIds::AIR || potatoItem == ItemIds::AIR ||
        carrotsBlock == BlockIds::AIR || potatoesBlock == BlockIds::AIR) {
        return fail("carrot and potato crop items and blocks should be registered");
    }
    if (ItemRegistry::toPlaceBlock(carrotItem) != carrotsBlock ||
        ItemRegistry::toRenderBlock(carrotItem) != BlockIds::AIR) {
        return fail("carrot item should place carrots while keeping its item icon");
    }
    if (ItemRegistry::toPlaceBlock(potatoItem) != potatoesBlock ||
        ItemRegistry::toRenderBlock(potatoItem) != BlockIds::AIR) {
        return fail("potato item should place potatoes while keeping its item icon");
    }

    // BlockDropTable tests
    if (BlockDropTable::getDropItem(BlockIds::COAL_ORE) != ItemIds::COAL) {
        return fail("coal_ore should drop coal item");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::findByName("iron_ore")) != ItemRegistry::findByName("raw_iron")) {
        return fail("iron_ore should drop raw iron for furnace smelting");
    }
    if (BlockDropTable::getDropItem(BlockIds::STONE) != ItemRegistry::findByName("cobblestone")) {
        return fail("stone should drop cobblestone");
    }
    if (BlockDropTable::getDropItem(BlockIds::DIRT) != ItemRegistry::fromBlock(BlockIds::DIRT)) {
        return fail("dirt should drop itself by default");
    }
    if (BlockDropTable::getDropItem(BlockIds::CHEST) != chestItem) {
        return fail("chest should drop itself by default");
    }
    if (BlockDropTable::getDropItem(BlockIds::AIR) != ItemIds::AIR) {
        return fail("air should drop air (nothing)");
    }
    if (BlockDropTable::getDropItem(wheatCropBlock) != ItemRegistry::findByName("wheat")) {
        return fail("wheat crop should drop wheat item");
    }
    if (BlockDropTable::getDropItem(carrotsBlock) != carrotItem ||
        BlockDropTable::getDropItem(potatoesBlock) != potatoItem) {
        return fail("carrot and potato crops should drop their food items");
    }
    if (BlockDropTable::getDropItem(redBedBlock) != redBedItem) {
        return fail("red_bed should drop its bed item");
    }
    if (BlockDropTable::getDropItem(redstoneWireBlock) != redstoneItem) {
        return fail("redstone_wire should drop redstone powder");
    }
    if (BlockDropTable::getDropItem(redstoneTorchBlock) != redstoneTorchItem) {
        return fail("redstone_torch should drop its torch item");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::findByName("piston_head")) != ItemIds::AIR) {
        return fail("piston_head should not drop a block item");
    }
    if (BlockDropTable::getDropItem(BlockIds::OAK_DOOR) != oakDoorItem ||
        BlockDropTable::getDropItem(BlockIds::OAK_TRAPDOOR) != oakTrapdoorItem ||
        BlockDropTable::getDropItem(BlockIds::OAK_FENCE_GATE) != oakFenceGateItem) {
        return fail("oak door, trapdoor, and fence gate should drop their own items");
    }

    std::cout << "[item_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
