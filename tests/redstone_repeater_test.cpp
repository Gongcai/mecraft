#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_repeater_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareFlatTestLine(World& world, const int y) {
    for (int x = -1; x <= 5; ++x) {
        world.setBlock(x, y - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(x, y, 0, RUNTIME_ID_NULL);
        world.setBlock(x, y + 1, 0, RUNTIME_ID_NULL);
    }
    world.setBlock(2, y - 1, 1, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setBlock(2, y, 1, RUNTIME_ID_NULL);
    world.setBlock(2, y + 1, 1, RUNTIME_ID_NULL);
}

StateID leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID repeaterState(const uint16_t delay, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::DELAY, delay}
        });
}

StateID repeaterState(const uint16_t facing, const uint16_t delay, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::DELAY, delay}
        });
}

StateID comparatorState(const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:comparator"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::MODE, PropIndices::MODE_COMPARE}
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

bool locked(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::LOCKED) == PropIndices::LOCKED_TRUE;
}

bool lampLit(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::LIT) == PropIndices::LIT_TRUE;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    const int y = 96;
    prepareFlatTestLine(world, y);
    world.setBlockState(0, y, 0, leverState(true));
    world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(2, y, 0, repeaterState(PropIndices::DELAY_2, false));
    world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(4, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
    world.setBlockState(2, y, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

    ecs::RedstoneSystem::processWorld(world, 0);
    if (wirePower(world, 1, y, 0) != 15 ||
        powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 0 ||
        lampLit(world, 4, y, 0)) {
        return fail("repeater should see input immediately but keep output off before its delay expires");
    }

    ecs::RedstoneSystem::processWorld(world, 1);
    if (powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 0 ||
        lampLit(world, 4, y, 0)) {
        return fail("delay=2 repeater should remain off one redstone tick after input turns on");
    }

    ecs::RedstoneSystem::processWorld(world, 2);
    if (!powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 15 ||
        !lampLit(world, 4, y, 0) ||
        lampLit(world, 2, y, 1)) {
        return fail("delay=2 repeater should power only its facing direction on the second redstone tick");
    }

    world.setBlockState(0, y, 0, leverState(false));
    ecs::RedstoneSystem::processWorld(world, 3);
    if (wirePower(world, 1, y, 0) != 0 ||
        !powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 15 ||
        !lampLit(world, 4, y, 0)) {
        return fail("repeater should keep output on until the delayed turn-off tick");
    }

    ecs::RedstoneSystem::processWorld(world, 4);
    if (!powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 15 ||
        !lampLit(world, 4, y, 0)) {
        return fail("delay=2 repeater should remain on one redstone tick after input turns off");
    }

    ecs::RedstoneSystem::processWorld(world, 5);
    if (powered(world, 2, y, 0) ||
        wirePower(world, 3, y, 0) != 0 ||
        lampLit(world, 4, y, 0)) {
        return fail("delay=2 repeater should turn off on the second redstone tick after input turns off");
    }

    {
        const int lockY = 80;
        prepareFlatTestLine(world, lockY);
        world.setBlockState(0, lockY, 0, leverState(true));
        world.setBlockState(1, lockY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, lockY, 0, repeaterState(PropIndices::DELAY_1, false));
        world.setBlockState(3, lockY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(4, lockY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        world.setBlock(2, lockY - 1, 2, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(2, lockY, 2, RUNTIME_ID_NULL);
        world.setBlockState(2, lockY, 1, repeaterState(PropIndices::FACING_NORTH, PropIndices::DELAY_1, false));
        world.setBlockState(2, lockY, 2, leverState(false));

        ecs::RedstoneSystem::processWorld(world, 20);
        ecs::RedstoneSystem::processWorld(world, 21);
        if (!powered(world, 2, lockY, 0) ||
            wirePower(world, 3, lockY, 0) != 15 ||
            !lampLit(world, 4, lockY, 0) ||
            locked(world, 2, lockY, 0)) {
            return fail("repeater should power normally before side locking");
        }

        world.setBlockState(2, lockY, 2, leverState(true));
        ecs::RedstoneSystem::processWorld(world, 22);
        ecs::RedstoneSystem::processWorld(world, 23);
        if (!powered(world, 2, lockY, 1) ||
            !locked(world, 2, lockY, 0) ||
            !powered(world, 2, lockY, 0)) {
            return fail("powered side repeater should lock the main repeater without changing its output");
        }

        world.setBlockState(0, lockY, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 24);
        ecs::RedstoneSystem::processWorld(world, 25);
        ecs::RedstoneSystem::processWorld(world, 26);
        if (!locked(world, 2, lockY, 0) ||
            !powered(world, 2, lockY, 0) ||
            wirePower(world, 3, lockY, 0) != 15 ||
            !lampLit(world, 4, lockY, 0)) {
            return fail("locked repeater should keep its powered output after rear input turns off");
        }

        world.setBlockState(2, lockY, 2, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 27);
        ecs::RedstoneSystem::processWorld(world, 28);
        if (locked(world, 2, lockY, 0) ||
            !powered(world, 2, lockY, 0) ||
            wirePower(world, 3, lockY, 0) != 15 ||
            !lampLit(world, 4, lockY, 0)) {
            return fail("unlocked repeater should keep its output until its own delayed evaluation tick");
        }

        ecs::RedstoneSystem::processWorld(world, 29);
        if (locked(world, 2, lockY, 0) ||
            powered(world, 2, lockY, 0) ||
            wirePower(world, 3, lockY, 0) != 0 ||
            lampLit(world, 4, lockY, 0)) {
            return fail("unlocked repeater should apply the delayed rear-input state");
        }
    }

    {
        const int comparatorLockY = 72;
        prepareFlatTestLine(world, comparatorLockY);
        world.setBlockState(0, comparatorLockY, 0, leverState(true));
        world.setBlockState(1, comparatorLockY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, comparatorLockY, 0, repeaterState(PropIndices::DELAY_1, false));
        world.setBlockState(3, comparatorLockY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));

        world.setBlock(2, comparatorLockY - 1, 2, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(2, comparatorLockY, 2, RUNTIME_ID_NULL);
        world.setBlockState(2, comparatorLockY, 1, comparatorState(PropIndices::FACING_NORTH, false));
        world.setBlockState(2, comparatorLockY, 2, leverState(false));

        ecs::RedstoneSystem::processWorld(world, 30);
        ecs::RedstoneSystem::processWorld(world, 31);
        if (!powered(world, 2, comparatorLockY, 0) ||
            locked(world, 2, comparatorLockY, 0)) {
            return fail("main repeater should power normally before comparator side locking");
        }

        world.setBlockState(2, comparatorLockY, 2, leverState(true));
        ecs::RedstoneSystem::processWorld(world, 32);
        if (!powered(world, 2, comparatorLockY, 1) ||
            !locked(world, 2, comparatorLockY, 0) ||
            !powered(world, 2, comparatorLockY, 0)) {
            return fail("powered side comparator output should lock the main repeater");
        }
    }

    {
        const int wireSideY = 64;
        prepareFlatTestLine(world, wireSideY);
        world.setBlockState(0, wireSideY, 0, leverState(true));
        world.setBlockState(1, wireSideY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, wireSideY, 0, repeaterState(PropIndices::DELAY_1, false));
        world.setBlockState(3, wireSideY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));

        world.setBlockState(2, wireSideY, 2, leverState(true));
        world.setBlockState(2, wireSideY, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));

        ecs::RedstoneSystem::processWorld(world, 40);
        ecs::RedstoneSystem::processWorld(world, 41);
        if (!powered(world, 2, wireSideY, 0) ||
            wirePower(world, 2, wireSideY, 1) == 0 ||
            locked(world, 2, wireSideY, 0)) {
            return fail("powered side redstone wire should not lock a repeater");
        }
    }

    {
        const int coloredWireY = 56;
        prepareFlatTestLine(world, coloredWireY);
        world.setBlockState(0, coloredWireY, 0, leverState(true));
        world.setBlockState(1, coloredWireY, 0, BlockStateRegistry::getDefaultState(
            BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, coloredWireY, 0, BlockStateRegistry::getDefaultState(
            BlockRegistry::requireIdByName("minecraft:blue_redstone_wire")));
        world.setBlockState(3, coloredWireY, 0, BlockStateRegistry::getDefaultState(
            BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        ecs::RedstoneSystem::processWorld(world, 50);
        if (wirePower(world, 1, coloredWireY, 0) != 15 ||
            wirePower(world, 2, coloredWireY, 0) != 0 ||
            lampLit(world, 3, coloredWireY, 0)) {
            return fail("redstone wires with different channels should not share propagated power");
        }
    }

    std::cout << "[redstone_repeater_test] PASS\n";
    return EXIT_SUCCESS;
}
