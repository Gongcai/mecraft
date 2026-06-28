#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_advanced_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadTestChunks(World& world) {
    world.setRenderDistance(5);
    for (int i = 0; i < 80; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareFlatLine(World& world, const int y, const int minX, const int maxX, const int z = 0) {
    for (int x = minX; x <= maxX; ++x) {
        world.setBlock(x, y - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlockState(x, y, z, NULL_BLOCK_STATE);
        world.setBlockState(x, y + 1, z, NULL_BLOCK_STATE);
        world.setBlockState(x, y + 2, z, NULL_BLOCK_STATE);
    }
}

void processRedstoneTicks(World& world, uint64_t& tick, const int count) {
    for (int i = 0; i < count; ++i) {
        ecs::RedstoneSystem::processWorld(world, tick);
        ++tick;
    }
}

BlockStateId leverState(const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

BlockStateId leverState(const bool powered) {
    return leverState(PropIndices::FACING_FLOOR, powered);
}

BlockStateId buttonState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:stone_button"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

BlockStateId redstoneTorchState(const bool lit) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:redstone_torch"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::LIT, lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE}
        });
}

BlockStateId repeaterState(const uint16_t facing, const uint16_t delay, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::DELAY, delay}
        });
}

uint8_t powerFromState(const BlockStateId state) {
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

    const uint16_t value = BlockStateRegistry::getPropertyIndex(state, PropIndices::POWER);
    for (uint8_t power = 0; power < kPowerValues.size(); ++power) {
        if (value == kPowerValues[power]) {
            return power;
        }
    }
    throw std::runtime_error("State does not contain a valid redstone power value");
}

uint8_t wirePower(const World& world, const int x, const int y, const int z) {
    return powerFromState(world.getBlockState(x, y, z));
}

bool lit(const World& world, const int x, const int y, const int z) {
    return BlockStateRegistry::getPropertyIndex(world.getBlockState(x, y, z), PropIndices::LIT) ==
           PropIndices::LIT_TRUE;
}

bool powered(const World& world, const int x, const int y, const int z) {
    return BlockStateRegistry::getPropertyIndex(world.getBlockState(x, y, z), PropIndices::POWERED) ==
           PropIndices::POWERED_TRUE;
}

int testTorchBurnout() {
    World world;
    world.init(20260627);
    loadTestChunks(world);

    const int y = 96;
    prepareFlatLine(world, y, -2, 2);
    world.setBlockState(0, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
    world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, false));
    world.setBlockState(0, y + 1, 0, redstoneTorchState(true));

    uint64_t tick = 0;
    for (int i = 0; i < 8; ++i) {
        world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, true));
        processRedstoneTicks(world, tick, 1);
        if (lit(world, 0, y + 1, 0)) {
            return fail("rapid input high should turn the torch off");
        }

        world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, false));
        processRedstoneTicks(world, tick, 1);
        if (i < 7 && !lit(world, 0, y + 1, 0)) {
            return fail("torch should relight before the burnout turn-off threshold is reached");
        }
    }

    if (lit(world, 0, y + 1, 0)) {
        return fail("torch should remain off after eight rapid turn-offs");
    }

    processRedstoneTicks(world, tick, 78);
    if (lit(world, 0, y + 1, 0)) {
        return fail("burned-out torch should stay off before its cooldown update");
    }

    processRedstoneTicks(world, tick, 1);
    if (!lit(world, 0, y + 1, 0)) {
        return fail("burned-out torch should relight after cooldown when its input is low");
    }

    return EXIT_SUCCESS;
}

int testSlowTorchTogglesDoNotBurnOut() {
    World world;
    world.init(20260627);
    loadTestChunks(world);

    const int y = 88;
    prepareFlatLine(world, y, -2, 2);
    world.setBlockState(0, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
    world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, false));
    world.setBlockState(0, y + 1, 0, redstoneTorchState(true));

    uint64_t tick = 200;
    for (int i = 0; i < 10; ++i) {
        world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, true));
        processRedstoneTicks(world, tick, 1);
        if (lit(world, 0, y + 1, 0)) {
            return fail("slow input high should still turn the torch off");
        }

        processRedstoneTicks(world, tick, 35);
        world.setBlockState(-1, y, 0, leverState(PropIndices::FACING_WEST, false));
        processRedstoneTicks(world, tick, 1);
        if (!lit(world, 0, y + 1, 0)) {
            return fail("slowly toggled torch should relight without entering burnout");
        }
        processRedstoneTicks(world, tick, 35);
    }

    return EXIT_SUCCESS;
}

int testCrossChunkLongRepeaterChain() {
    World world;
    world.init(20260627);
    loadTestChunks(world);

    const int y = 72;
    prepareFlatLine(world, y, -1, 62);
    world.setBlockState(0, y, 0, leverState(true));
    for (int x = 1; x <= 14; ++x) {
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    }
    world.setBlockState(15, y, 0, repeaterState(PropIndices::FACING_EAST, PropIndices::DELAY_1, false));
    for (int x = 16; x <= 29; ++x) {
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    }
    world.setBlockState(30, y, 0, repeaterState(PropIndices::FACING_EAST, PropIndices::DELAY_1, false));
    for (int x = 31; x <= 44; ++x) {
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    }
    world.setBlockState(45, y, 0, repeaterState(PropIndices::FACING_EAST, PropIndices::DELAY_1, false));
    for (int x = 46; x <= 59; ++x) {
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    }
    world.setBlockState(60, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

    uint64_t tick = 400;
    processRedstoneTicks(world, tick, 8);
    if (!powered(world, 15, y, 0) ||
        !powered(world, 30, y, 0) ||
        !powered(world, 45, y, 0) ||
        wirePower(world, 16, y, 0) != 15 ||
        wirePower(world, 31, y, 0) != 15 ||
        wirePower(world, 46, y, 0) != 15 ||
        !lit(world, 60, y, 0)) {
        return fail("cross-chunk repeater chain should carry power across chunk borders");
    }

    world.setBlockState(0, y, 0, leverState(false));
    processRedstoneTicks(world, tick, 8);
    if (powered(world, 15, y, 0) ||
        powered(world, 30, y, 0) ||
        powered(world, 45, y, 0) ||
        wirePower(world, 16, y, 0) != 0 ||
        wirePower(world, 31, y, 0) != 0 ||
        wirePower(world, 46, y, 0) != 0 ||
        lit(world, 60, y, 0)) {
        return fail("cross-chunk repeater chain should depower deterministically");
    }

    return EXIT_SUCCESS;
}

int testButtonPulseThroughRepeaterDelayLine() {
    World world;
    world.init(20260627);
    loadTestChunks(world);

    const int y = 64;
    prepareFlatLine(world, y, -1, 8);
    world.setBlockState(0, y, 0, buttonState(true));
    world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(2, y, 0, repeaterState(PropIndices::FACING_EAST, PropIndices::DELAY_2, false));
    world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(4, y, 0, repeaterState(PropIndices::FACING_EAST, PropIndices::DELAY_2, false));
    world.setBlockState(5, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(6, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

    uint64_t tick = 500;
    processRedstoneTicks(world, tick, 1);
    if (lit(world, 6, y, 0)) {
        return fail("delayed repeater line should not power the output immediately");
    }

    processRedstoneTicks(world, tick, 4);
    if (!lit(world, 6, y, 0)) {
        return fail("delayed repeater line should power the output after both repeater delays");
    }

    processRedstoneTicks(world, tick, 5);
    if (!powered(world, 0, y, 0) || !lit(world, 6, y, 0)) {
        return fail("button pulse should remain active before its scheduled release tick");
    }

    processRedstoneTicks(world, tick, 1);
    if (powered(world, 0, y, 0) || !lit(world, 6, y, 0)) {
        return fail("button release should not immediately erase delayed repeater output");
    }

    processRedstoneTicks(world, tick, 3);
    if (!lit(world, 6, y, 0)) {
        return fail("delayed repeater output should persist until the final delayed turn-off");
    }

    processRedstoneTicks(world, tick, 1);
    if (lit(world, 6, y, 0)) {
        return fail("delayed repeater output should turn off after both turn-off delays");
    }

    return EXIT_SUCCESS;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    if (const int result = testTorchBurnout(); result != EXIT_SUCCESS) {
        return result;
    }
    if (const int result = testSlowTorchTogglesDoNotBurnOut(); result != EXIT_SUCCESS) {
        return result;
    }
    if (const int result = testCrossChunkLongRepeaterChain(); result != EXIT_SUCCESS) {
        return result;
    }
    if (const int result = testButtonPulseThroughRepeaterDelayLine(); result != EXIT_SUCCESS) {
        return result;
    }

    std::cout << "[redstone_advanced_test] PASS\n";
    return EXIT_SUCCESS;
}
