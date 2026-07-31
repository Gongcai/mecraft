#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "Paths.h"
#include "../src/crafting/SmeltingSystem.h"
#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/world/HopperSystem.h"
#include "../src/game/inventory/BlockEntityInventoryLifecycle.h"
#include "../src/game/inventory/BlockEntityInventoryStore.h"
#include "../src/game/inventory/ContainerBehaviorRegistry.h"
#include "../src/game/inventory/MachineInventoryStore.h"
#include "../src/item/Item.h"
#include "../src/ui/inventory/ContainerUiRegistry.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[hopper_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

BlockStateId defaultState(const char* blockName) {
    return BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName(blockName));
}

BlockStateId hopperState(const uint16_t facing, const bool enabled) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:hopper"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::ENABLED, enabled ? PropIndices::ENABLED_TRUE : PropIndices::ENABLED_FALSE}});
}

void placeBlock(World& world, const glm::ivec3& position, const BlockStateId state) {
    world.setBlockState(position.x, position.y, position.z, state);
}

SmeltingSystem& loadSmelting(ecs::GameplayRegistry& registry) {
    SmeltingSystem& smelting = registry.ctxSet<SmeltingSystem>();
    smelting.loadRecipes(SMELTING_CONFIG_PATH);
    return smelting;
}

bool stackIs(const ItemStack& stack, const ItemID itemId, const uint16_t count) {
    return stack.itemId == itemId && stack.count == count;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    ContainerBehaviorRegistry::init();
    ui::ContainerUiRegistry::init();

    const ItemID apple = ItemRegistry::requireIdByName("minecraft:apple");
    const ItemID coal = ItemRegistry::requireIdByName("minecraft:coal");
    const ItemID rawIron = ItemRegistry::requireIdByName("minecraft:raw_iron");
    const ItemID ironIngot = ItemRegistry::requireIdByName("minecraft:iron_ingot");

    {
        ecs::GameplayRegistry registry;
        const glm::ivec3 hopperPos(0, 96, 0);
        const bool created = ensureBlockEntityInventoryForPlacedBlock(
            registry, BlockRegistry::requireIdByName("minecraft:hopper"), hopperPos);
        if (!created || !registry.ctxHas<BlockEntityInventoryStore>()) {
            return fail("placing a hopper should create a block-entity storage inventory");
        }
        const BlockEntityInventory* hopperInventory = registry.ctxGet<BlockEntityInventoryStore>().find(hopperPos);
        if (hopperInventory == nullptr || !hopperInventory->getSlotStack(0).isEmpty()) {
            return fail("placed hopper inventory should exist and start empty");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        const glm::ivec3 hopperPos(0, 96, 0);
        const glm::ivec3 chestPos(0, 97, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_DOWN, true));
        placeBlock(world, chestPos, defaultState("minecraft:chest"));

        BlockEntityInventory& chest = store.getOrCreate(chestPos, "minecraft:chest", 27);
        chest.setSlotItem(0, apple, 3);
        static_cast<void>(store.getOrCreate(hopperPos, "minecraft:hopper", 5));

        const std::size_t transfers = ecs::HopperSystem::processWorld(world, registry, 8);
        const BlockEntityInventory* hopper = store.find(hopperPos);
        if (transfers != 1 || !stackIs(chest.getSlotStack(0), apple, 2) || hopper == nullptr ||
            !stackIs(hopper->getSlotStack(0), apple, 1)) {
            return fail("enabled hopper should pull one item from the container above");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        const glm::ivec3 hopperPos(0, 80, 0);
        const glm::ivec3 chestPos(1, 80, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_EAST, true));
        placeBlock(world, chestPos, defaultState("minecraft:chest"));

        BlockEntityInventory& hopper = store.getOrCreate(hopperPos, "minecraft:hopper", 5);
        hopper.setSlotItem(0, apple, 2);
        BlockEntityInventory& chest = store.getOrCreate(chestPos, "minecraft:chest", 27);

        const std::size_t transfers = ecs::HopperSystem::processWorld(world, registry, 16);
        if (transfers != 1 || !stackIs(hopper.getSlotStack(0), apple, 1) || !stackIs(chest.getSlotStack(0), apple, 1)) {
            return fail("enabled hopper should push one item into the facing container");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        const glm::ivec3 hopperPos(0, 72, 0);
        const glm::ivec3 chestPos(1, 72, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_EAST, false));
        placeBlock(world, chestPos, defaultState("minecraft:chest"));

        BlockEntityInventory& hopper = store.getOrCreate(hopperPos, "minecraft:hopper", 5);
        hopper.setSlotItem(0, apple, 2);
        BlockEntityInventory& chest = store.getOrCreate(chestPos, "minecraft:chest", 27);

        const std::size_t transfers = ecs::HopperSystem::processWorld(world, registry, 24);
        if (transfers != 0 || !stackIs(hopper.getSlotStack(0), apple, 2) || !chest.getSlotStack(0).isEmpty()) {
            return fail("redstone-disabled hopper should not transfer items");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        const glm::ivec3 hopperPos(0, 64, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_DOWN, true));
        BlockEntityInventory& hopper = store.getOrCreate(hopperPos, "minecraft:hopper", 5);

        const entt::entity drop = registry.create();
        registry.emplace<ecs::DropItemTag>(drop);
        ecs::TransformComponent transform;
        transform.position = glm::vec3(hopperPos) + glm::vec3(0.5f, 0.8f, 0.5f);
        registry.emplace<ecs::TransformComponent>(drop, transform);
        registry.emplace<ecs::BoundsComponent>(drop, ecs::BoundsComponent{glm::vec3(0.125f)});
        registry.emplace<ecs::ItemComponent>(drop, ecs::ItemComponent{apple, 4});

        const std::size_t transfers = ecs::HopperSystem::processWorld(world, registry, 32);
        const ecs::ItemComponent* dropItem = registry.try_get<ecs::ItemComponent>(drop);
        if (transfers != 1 || !stackIs(hopper.getSlotStack(0), apple, 1) || dropItem == nullptr ||
            dropItem->stackCount != 3) {
            return fail("enabled hopper should pick up one intersecting dropped item");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        loadSmelting(registry);
        BlockEntityInventoryStore& storageStore = registry.ctxSet<BlockEntityInventoryStore>();
        MachineInventoryStore& machineStore = registry.ctxSet<MachineInventoryStore>();
        const glm::ivec3 hopperPos(0, 56, 0);
        const glm::ivec3 furnacePos(1, 56, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_EAST, true));
        placeBlock(world, furnacePos, defaultState("minecraft:furnace"));

        BlockEntityInventory& hopper = storageStore.getOrCreate(hopperPos, "minecraft:hopper", 5);
        hopper.setSlotItem(0, rawIron, 1);
        hopper.setSlotItem(1, coal, 1);
        MachineInventory& furnace = machineStore.getOrCreate(furnacePos, "minecraft:furnace", 3);

        if (ecs::HopperSystem::processWorld(world, registry, 40) != 1 ||
            !stackIs(furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT), rawIron, 1)) {
            return fail("hopper should insert smeltable items into the furnace input slot");
        }
        if (ecs::HopperSystem::processWorld(world, registry, 48) != 1 ||
            !stackIs(furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT), coal, 1)) {
            return fail("hopper should insert fuel items into the furnace fuel slot");
        }

        hopper.setSlotItem(2, ironIngot, 1);
        if (ecs::HopperSystem::processWorld(world, registry, 56) != 0 ||
            !stackIs(hopper.getSlotStack(2), ironIngot, 1) ||
            !furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT).isEmpty()) {
            return fail("hopper should not insert items into furnace output-only slots");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& storageStore = registry.ctxSet<BlockEntityInventoryStore>();
        MachineInventoryStore& machineStore = registry.ctxSet<MachineInventoryStore>();
        const glm::ivec3 hopperPos(0, 48, 0);
        const glm::ivec3 furnacePos(0, 49, 0);
        placeBlock(world, hopperPos, hopperState(PropIndices::FACING_DOWN, true));
        placeBlock(world, furnacePos, defaultState("minecraft:furnace"));

        BlockEntityInventory& hopper = storageStore.getOrCreate(hopperPos, "minecraft:hopper", 5);
        MachineInventory& furnace = machineStore.getOrCreate(furnacePos, "minecraft:furnace", 3);
        furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT, ItemStack{rawIron, 1, 0});
        furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT, ItemStack{coal, 1, 0});
        furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT, ItemStack{ironIngot, 2, 0});

        const std::size_t transfers = ecs::HopperSystem::processWorld(world, registry, 64);
        if (transfers != 1 || !stackIs(hopper.getSlotStack(0), ironIngot, 1) ||
            !stackIs(furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT), rawIron, 1) ||
            !stackIs(furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_FUEL_SLOT), coal, 1) ||
            !stackIs(furnace.getSlotStack(MachineInventory::DEFAULT_SMELTING_OUTPUT_SLOT), ironIngot, 1)) {
            return fail("hopper should extract only from furnace output-only slots");
        }
    }

    std::cout << "[hopper_system_test] PASS\n";
    return EXIT_SUCCESS;
}
