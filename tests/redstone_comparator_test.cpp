#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/game/inventory/BlockEntityInventoryStore.h"
#include "../src/game/inventory/MachineInventoryStore.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_comparator_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareComparatorArea(World& world, const int y) {
    for (int x = -2; x <= 5; ++x) {
        for (int z = -1; z <= 3; ++z) {
            world.setBlock(x, y - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
            world.setBlock(x, y, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 1, z, RUNTIME_ID_NULL);
        }
    }
}

StateID leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID comparatorState(const uint16_t mode, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:comparator"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::MODE, mode}
        });
}

uint8_t wirePower(const World& world, const int x, const int y, const int z) {
    static const std::array<uint16_t, 16> kPowerValues = {
        PropIndices::POWER_0,
        PropIndices::POWER_1,
        PropIndices::POWER_2,
        PropIndices::POWER_3,
        PropIndices::POWER_4,
        PropIndices::POWER_5,
        PropIndices::POWER_6,
        PropIndices::POWER_7,
        PropIndices::POWER_8,
        PropIndices::POWER_9,
        PropIndices::POWER_10,
        PropIndices::POWER_11,
        PropIndices::POWER_12,
        PropIndices::POWER_13,
        PropIndices::POWER_14,
        PropIndices::POWER_15,
    };

    const StateID state = world.getBlockState(x, y, z);
    const uint16_t value = BlockStateRegistry::getPropertyIndex(state, PropIndices::POWER);
    for (uint8_t power = 0; power < kPowerValues.size(); ++power) {
        if (value == kPowerValues[power]) {
            return power;
        }
    }

    throw std::runtime_error("Wire state does not contain a valid power value");
}

bool powered(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::POWERED) == PropIndices::POWERED_TRUE;
}

bool lampLit(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::LIT) == PropIndices::LIT_TRUE;
}

void placeComparatorOutput(World& world, const int y, const uint16_t mode) {
    world.setBlockState(2, y, 0, comparatorState(mode, false));
    world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(4, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    {
        const int y = 96;
        prepareComparatorArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        placeComparatorOutput(world, y, PropIndices::MODE_COMPARE);

        ecs::RedstoneSystem::processWorld(world, 0);
        if (!powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 15 ||
            !lampLit(world, 4, y, 0)) {
            return fail("compare mode should pass rear input when no side input is stronger");
        }
    }

    {
        const int y = 80;
        prepareComparatorArea(world, y);
        world.setBlockState(-1, y, 0, leverState(true));
        world.setBlockState(0, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, y, 2, leverState(true));
        world.setBlockState(2, y, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        placeComparatorOutput(world, y, PropIndices::MODE_COMPARE);

        ecs::RedstoneSystem::processWorld(world, 1);
        if (powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 0 ||
            lampLit(world, 4, y, 0)) {
            return fail("compare mode should block output when side input is stronger than rear input");
        }
    }

    {
        const int y = 64;
        prepareComparatorArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, y, 3, leverState(true));
        world.setBlockState(2, y, 2, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, y, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        placeComparatorOutput(world, y, PropIndices::MODE_SUBTRACT);

        ecs::RedstoneSystem::processWorld(world, 2);
        if (!powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 1 ||
            !lampLit(world, 4, y, 0)) {
            return fail("subtract mode should output rear minus strongest side input");
        }
    }

    {
        const int y = 48;
        const glm::ivec3 chestPosition(1, y, 0);
        prepareComparatorArea(world, y);
        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        BlockEntityInventory& chest = store.getOrCreate(chestPosition, "minecraft:chest", 27);
        const ItemID coalId = ItemRegistry::requireIdByName("minecraft:coal");
        const uint16_t coalMaxStack = ItemRegistry::get(coalId).maxStack;
        for (int slot = 0; slot < 14; ++slot) {
            chest.setSlotItem(slot, coalId, coalMaxStack);
        }

        world.setBlockState(
            chestPosition.x,
            chestPosition.y,
            chestPosition.z,
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:chest")));
        placeComparatorOutput(world, y, PropIndices::MODE_COMPARE);

        ecs::RedstoneSystem::processWorld(world, 3, registry);
        if (!powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 8 ||
            !lampLit(world, 4, y, 0)) {
            return fail("compare mode should read half-full chest inventory as signal strength 8");
        }
    }

    {
        const int y = 40;
        const glm::ivec3 barrelPosition(1, y, 0);
        prepareComparatorArea(world, y);
        ecs::GameplayRegistry registry;
        BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
        BlockEntityInventory& barrel = store.getOrCreate(barrelPosition, "minecraft:barrel", 27);
        const ItemID coalId = ItemRegistry::requireIdByName("minecraft:coal");
        const uint16_t coalMaxStack = ItemRegistry::get(coalId).maxStack;
        for (int slot = 0; slot < 14; ++slot) {
            barrel.setSlotItem(slot, coalId, coalMaxStack);
        }

        world.setBlockState(
            barrelPosition.x,
            barrelPosition.y,
            barrelPosition.z,
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:barrel")));
        placeComparatorOutput(world, y, PropIndices::MODE_COMPARE);

        ecs::RedstoneSystem::processWorld(world, 4, registry);
        if (!powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 8 ||
            !lampLit(world, 4, y, 0)) {
            return fail("compare mode should read data-driven barrel inventory as signal strength 8");
        }
    }

    {
        const int y = 32;
        const glm::ivec3 furnacePosition(1, y, 0);
        prepareComparatorArea(world, y);
        ecs::GameplayRegistry registry;
        MachineInventoryStore& store = registry.ctxSet<MachineInventoryStore>();
        MachineInventory& furnace = store.getOrCreate(furnacePosition, "minecraft:furnace", 3);
        const ItemID coalId = ItemRegistry::requireIdByName("minecraft:coal");
        const uint16_t coalMaxStack = ItemRegistry::get(coalId).maxStack;
        furnace.setSlotStack(MachineInventory::DEFAULT_SMELTING_INPUT_SLOT, ItemStack{coalId, coalMaxStack, 0});

        world.setBlockState(
            furnacePosition.x,
            furnacePosition.y,
            furnacePosition.z,
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:furnace")));
        placeComparatorOutput(world, y, PropIndices::MODE_COMPARE);

        ecs::RedstoneSystem::processWorld(world, 5, registry);
        if (!powered(world, 2, y, 0) ||
            wirePower(world, 3, y, 0) != 5 ||
            !lampLit(world, 4, y, 0)) {
            return fail("compare mode should read data-driven furnace inventory as signal strength 5");
        }
    }

    std::cout << "[redstone_comparator_test] PASS\n";
    return EXIT_SUCCESS;
}
