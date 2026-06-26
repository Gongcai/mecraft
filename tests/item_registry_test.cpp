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

    const ItemDef& dirtItem = ItemRegistry::get(ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:dirt")));
    if (dirtItem.placeBlock != BlockRegistry::requireIdByName("minecraft:dirt")) {
        return fail("dirt should place its source block");
    }
    if (ItemRegistry::findByName("dirt") != ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:dirt"))) {
        return fail("block name lookup should resolve dirt via BlockRegistry");
    }

    const ItemID blueWoolItem = ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:blue_wool"));
    if (blueWoolItem == RUNTIME_ID_NULL) {
        return fail("blue_wool should synthesize a block-backed item without items.json");
    }
    if (ItemRegistry::findByName("blue_wool") != blueWoolItem) {
        return fail("block name lookup should resolve synthesized block items");
    }
    const ItemDef& blueWool = ItemRegistry::get(blueWoolItem);
    if (blueWool.placeBlock != BlockRegistry::requireIdByName("minecraft:blue_wool") || blueWool.renderBlock != BlockRegistry::requireIdByName("minecraft:blue_wool")) {
        return fail("blue_wool synthesized item should render and place its source block");
    }

    const ItemID chestItem = ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:chest"));
    if (chestItem == RUNTIME_ID_NULL) {
        return fail("chest should synthesize a block-backed item");
    }
    if (ItemRegistry::findByName("chest") != chestItem) {
        return fail("block name lookup should resolve chest item");
    }
    if (ItemRegistry::toPlaceBlock(chestItem) != BlockRegistry::requireIdByName("minecraft:chest") ||
        ItemRegistry::toRenderBlock(chestItem) != BlockRegistry::requireIdByName("minecraft:chest")) {
        return fail("chest item should render and place chest block");
    }

    const ItemID barrelItem = ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:barrel"));
    if (barrelItem == RUNTIME_ID_NULL) {
        return fail("barrel should synthesize a block-backed item");
    }
    if (ItemRegistry::findByName("barrel") != barrelItem) {
        return fail("block name lookup should resolve barrel item");
    }
    if (ItemRegistry::toPlaceBlock(barrelItem) != BlockRegistry::requireIdByName("minecraft:barrel") ||
        ItemRegistry::toRenderBlock(barrelItem) != BlockRegistry::requireIdByName("minecraft:barrel")) {
        return fail("barrel item should render and place barrel block");
    }

    const ItemDef& coal = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:coal"));
    if (ItemRegistry::findByName("coal") != ItemRegistry::requireIdByName("minecraft:coal")) {
        return fail("item name lookup should resolve coal from items.json");
    }
    if (coal.placeBlock != RUNTIME_ID_NULL) {
        return fail("coal should not be directly placeable");
    }
    if (std::string(coal.iconTextureName) != "coal") {
        return fail("coal should provide iconTexture from items.json");
    }
    if (!ItemRegistry::hasTag(ItemRegistry::requireIdByName("minecraft:coal"), NamespacedId("minecraft", "coals"))) {
        return fail("coal should expose the coals item tag");
    }

    const ItemDef& apple = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:apple"));
    if (ItemRegistry::findByName("apple") != ItemRegistry::requireIdByName("minecraft:apple")) {
        return fail("item name lookup should resolve apple from items.json");
    }
    if (apple.placeBlock != RUNTIME_ID_NULL || apple.renderBlock != RUNTIME_ID_NULL) {
        return fail("apple should be a non-placeable pure item");
    }
    if (std::string(apple.iconTextureName) != "apple") {
        return fail("apple should use the apple item texture");
    }

    const ItemDef& ironPickaxe = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:iron_pickaxe"));
    if (ItemRegistry::findByName("iron_pickaxe") != ItemRegistry::requireIdByName("minecraft:iron_pickaxe")) {
        return fail("item name lookup should resolve iron_pickaxe from items.json");
    }
    if (!ironPickaxe.isTool || ironPickaxe.maxStack != 1 ||
        ironPickaxe.toolKind != "pickaxe" || ironPickaxe.toolTier != 2 ||
        ironPickaxe.toolEfficiency < 5.9f || ironPickaxe.toolEfficiency > 6.1f) {
        return fail("iron pickaxe should be a non-stackable tool");
    }

    const ItemDef& ironHoe = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:iron_hoe"));
    if (ItemRegistry::findByName("iron_hoe") != ItemRegistry::requireIdByName("minecraft:iron_hoe")) {
        return fail("item name lookup should resolve iron_hoe from items.json");
    }
    if (!ironHoe.isTool || ironHoe.maxStack != 1 ||
        ironHoe.toolKind != "hoe" || ironHoe.toolTier != 2 ||
        ironHoe.placeBlock != RUNTIME_ID_NULL || ironHoe.renderBlock != RUNTIME_ID_NULL ||
        std::string(ironHoe.iconTextureName) != "iron_hoe") {
        return fail("iron hoe should be a non-placeable hoe tool with an item texture");
    }
    const ItemUseRule* tillRule = ItemUseRules::findRule(ironHoe, ItemUseBehavior::TillSoil);
    if (tillRule == nullptr ||
        !ItemUseRules::matchesBlock(*tillRule, BlockRegistry::requireIdByName("minecraft:dirt")) ||
        !ItemUseRules::matchesBlock(*tillRule, BlockRegistry::requireIdByName("minecraft:grass_block")) ||
        tillRule->resultBlock != BlockRegistry::requireIdByName("minecraft:farmland") ||
        tillRule->consumeDurability != 1 ||
        !tillRule->requiresEmptyAbove) {
        return fail("iron hoe should declare a data-driven tilling rule");
    }

    const ItemID craftingTableItem = ItemRegistry::findByName("crafting_table");
    const BlockID craftingTableBlock = BlockRegistry::findByName("crafting_table");
    if (craftingTableItem == RUNTIME_ID_NULL || craftingTableBlock == RUNTIME_ID_NULL) {
        return fail("crafting_table should register both block and item IDs");
    }
    if (ItemRegistry::toPlaceBlock(craftingTableItem) != craftingTableBlock) {
        return fail("crafting_table item should place the crafting table block");
    }

    const ItemID cherryPlanksItem = ItemRegistry::findByName("cherry_planks");
    const BlockID cherryPlanksBlock = BlockRegistry::findByName("cherry_planks");
    if (cherryPlanksItem == RUNTIME_ID_NULL || cherryPlanksBlock == RUNTIME_ID_NULL) {
        return fail("cherry_planks should register both block and item IDs");
    }
    if (!BlockRegistry::hasTag(cherryPlanksBlock, NamespacedId("minecraft", "planks")) ||
        !ItemRegistry::hasTag(cherryPlanksItem, NamespacedId("minecraft", "planks"))) {
        return fail("block-backed planks item should inherit the planks tag");
    }

    const ItemID furnaceItem = ItemRegistry::findByName("furnace");
    const BlockID furnaceBlock = BlockRegistry::findByName("furnace");
    if (furnaceItem == RUNTIME_ID_NULL || furnaceBlock == RUNTIME_ID_NULL) {
        return fail("furnace should register both block and item IDs");
    }
    if (ItemRegistry::toPlaceBlock(furnaceItem) != furnaceBlock) {
        return fail("furnace item should place the furnace block");
    }

    const ItemID vineItem = ItemRegistry::findByName("vine");
    const BlockID vineBlock = BlockRegistry::findByName("vine");
    if (vineItem == RUNTIME_ID_NULL || vineBlock == RUNTIME_ID_NULL) {
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
    if (redBedItem == RUNTIME_ID_NULL || redBedBlock == RUNTIME_ID_NULL) {
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
    if (redstoneItem == RUNTIME_ID_NULL || redstoneWireBlock == RUNTIME_ID_NULL) {
        return fail("redstone item and redstone_wire block should be registered");
    }
    const ItemDef& redstone = ItemRegistry::get(redstoneItem);
    if (ItemRegistry::toPlaceBlock(redstoneItem) != redstoneWireBlock ||
        ItemRegistry::toRenderBlock(redstoneItem) != RUNTIME_ID_NULL ||
        std::string(redstone.iconTextureName) != "redstone") {
        return fail("redstone item should place redstone_wire while keeping its powder icon");
    }

    const ItemID redstoneTorchItem = ItemRegistry::findByName("redstone_torch");
    const BlockID redstoneTorchBlock = BlockRegistry::findByName("redstone_torch");
    if (redstoneTorchItem == RUNTIME_ID_NULL || redstoneTorchBlock == RUNTIME_ID_NULL) {
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
    if (repeaterItem == RUNTIME_ID_NULL ||
        comparatorItem == RUNTIME_ID_NULL ||
        hopperItem == RUNTIME_ID_NULL ||
        repeaterBlock == RUNTIME_ID_NULL ||
        comparatorBlock == RUNTIME_ID_NULL ||
        hopperBlock == RUNTIME_ID_NULL) {
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
    if (bucketItem != ItemRegistry::requireIdByName("minecraft:bucket") || waterBucketItem != ItemRegistry::requireIdByName("minecraft:water_bucket")) {
        return fail("bucket and water_bucket should resolve to their builtin item ids");
    }
    const ItemDef& bucket = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:bucket"));
    const ItemDef& waterBucket = ItemRegistry::get(ItemRegistry::requireIdByName("minecraft:water_bucket"));
    if (bucket.maxStack != 1 ||
        waterBucket.maxStack != 1 ||
        bucket.placeBlock != RUNTIME_ID_NULL ||
        waterBucket.placeBlock != RUNTIME_ID_NULL ||
        std::string(bucket.iconTextureName) != "bucket" ||
        std::string(waterBucket.iconTextureName) != "water_bucket") {
        return fail("bucket items should be non-stackable non-placeable items with explicit item textures");
    }
    const ItemUseRule* pickupWaterRule = ItemUseRules::findRule(bucket, ItemUseBehavior::BucketPickupFluid);
    const ItemUseRule* placeWaterRule = ItemUseRules::findRule(waterBucket, ItemUseBehavior::BucketPlaceFluid);
    if (pickupWaterRule == nullptr ||
        !ItemUseRules::matchesBlock(*pickupWaterRule, BlockRegistry::requireIdByName("minecraft:water")) ||
        pickupWaterRule->resultBlock != RUNTIME_ID_NULL ||
        pickupWaterRule->resultItem != waterBucketItem ||
        !pickupWaterRule->requiresSourceFluid ||
        placeWaterRule == nullptr ||
        placeWaterRule->resultBlock != BlockRegistry::requireIdByName("minecraft:water") ||
        placeWaterRule->resultItem != bucketItem ||
        !placeWaterRule->requiresFluidPlacement) {
        return fail("bucket items should declare data-driven fluid use rules");
    }

    const ItemID oakDoorItem = ItemRegistry::findByName("oak_door");
    const ItemID oakTrapdoorItem = ItemRegistry::findByName("oak_trapdoor");
    const ItemID oakFenceGateItem = ItemRegistry::findByName("oak_fence_gate");
    if (oakDoorItem == RUNTIME_ID_NULL ||
        oakTrapdoorItem == RUNTIME_ID_NULL ||
        oakFenceGateItem == RUNTIME_ID_NULL) {
        return fail("oak door, trapdoor, and fence gate items should be registered");
    }
    const ItemDef& oakDoor = ItemRegistry::get(oakDoorItem);
    const ItemDef& oakTrapdoor = ItemRegistry::get(oakTrapdoorItem);
    const ItemDef& oakFenceGate = ItemRegistry::get(oakFenceGateItem);
    if (oakDoor.placeBlock != BlockRegistry::requireIdByName("minecraft:oak_door") ||
        oakTrapdoor.placeBlock != BlockRegistry::requireIdByName("minecraft:oak_trapdoor") ||
        oakFenceGate.placeBlock != BlockRegistry::requireIdByName("minecraft:oak_fence_gate") ||
        oakDoor.renderBlock != BlockRegistry::requireIdByName("minecraft:oak_door") ||
        oakTrapdoor.renderBlock != BlockRegistry::requireIdByName("minecraft:oak_trapdoor") ||
        oakFenceGate.renderBlock != BlockRegistry::requireIdByName("minecraft:oak_fence_gate") ||
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
    if (wildflowersItem == RUNTIME_ID_NULL ||
        leafLitterItem == RUNTIME_ID_NULL ||
        glowLichenItem == RUNTIME_ID_NULL ||
        wildflowersBlock == RUNTIME_ID_NULL ||
        leafLitterBlock == RUNTIME_ID_NULL ||
        glowLichenBlock == RUNTIME_ID_NULL) {
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
    if (oakLeavesItem == RUNTIME_ID_NULL) {
        return fail("oak_leaves should register a block-backed item");
    }
    if (!ui::shouldUseBakedBlockIcon(ItemRegistry::get(oakLeavesItem))) {
        return fail("oak_leaves should keep its tinted baked block icon");
    }

    const ItemID wheatSeedsItem = ItemRegistry::findByName("wheat_seeds");
    const BlockID wheatCropBlock = BlockRegistry::findByName("wheat");
    if (wheatSeedsItem == RUNTIME_ID_NULL || wheatCropBlock == RUNTIME_ID_NULL) {
        return fail("wheat seeds item and wheat crop block should be registered");
    }
    const ItemDef& wheatSeeds = ItemRegistry::get(wheatSeedsItem);
    if (std::string(wheatSeeds.iconTextureName) != "wheat_seeds" ||
        ItemRegistry::toPlaceBlock(wheatSeedsItem) != wheatCropBlock ||
        ItemRegistry::toRenderBlock(wheatSeedsItem) != RUNTIME_ID_NULL) {
        return fail("wheat seeds should place wheat while keeping its item icon");
    }

    const ItemID carrotItem = ItemRegistry::findByName("carrot");
    const ItemID potatoItem = ItemRegistry::findByName("potato");
    const BlockID carrotsBlock = BlockRegistry::findByName("carrots");
    const BlockID potatoesBlock = BlockRegistry::findByName("potatoes");
    if (carrotItem == RUNTIME_ID_NULL || potatoItem == RUNTIME_ID_NULL ||
        carrotsBlock == RUNTIME_ID_NULL || potatoesBlock == RUNTIME_ID_NULL) {
        return fail("carrot and potato crop items and blocks should be registered");
    }
    if (ItemRegistry::toPlaceBlock(carrotItem) != carrotsBlock ||
        ItemRegistry::toRenderBlock(carrotItem) != RUNTIME_ID_NULL) {
        return fail("carrot item should place carrots while keeping its item icon");
    }
    if (ItemRegistry::toPlaceBlock(potatoItem) != potatoesBlock ||
        ItemRegistry::toRenderBlock(potatoItem) != RUNTIME_ID_NULL) {
        return fail("potato item should place potatoes while keeping its item icon");
    }

    // BlockDropTable tests
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:coal_ore")) != ItemRegistry::requireIdByName("minecraft:coal")) {
        return fail("coal_ore should drop coal item");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::findByName("iron_ore")) != ItemRegistry::findByName("raw_iron")) {
        return fail("iron_ore should drop raw iron for furnace smelting");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:stone")) != ItemRegistry::findByName("cobblestone")) {
        return fail("stone should drop cobblestone");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:dirt")) != ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:dirt"))) {
        return fail("dirt should drop itself by default");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:chest")) != chestItem) {
        return fail("chest should drop itself by default");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:barrel")) != barrelItem) {
        return fail("barrel should drop itself by default");
    }
    if (BlockDropTable::getDropItem(RUNTIME_ID_NULL) != RUNTIME_ID_NULL) {
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
    if (BlockDropTable::getDropItem(BlockRegistry::findByName("piston_head")) != RUNTIME_ID_NULL) {
        return fail("piston_head should not drop a block item");
    }
    if (BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:oak_door")) != oakDoorItem ||
        BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:oak_trapdoor")) != oakTrapdoorItem ||
        BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:oak_fence_gate")) != oakFenceGateItem) {
        return fail("oak door, trapdoor, and fence gate should drop their own items");
    }

    std::cout << "[item_registry_test] PASS\n";
    return EXIT_SUCCESS;
}
