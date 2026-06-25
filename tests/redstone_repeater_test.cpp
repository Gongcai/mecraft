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
        world.setBlock(x, y - 1, 0, BlockIds::STONE);
        world.setBlock(x, y, 0, BlockIds::AIR);
        world.setBlock(x, y + 1, 0, BlockIds::AIR);
    }
    world.setBlock(2, y - 1, 1, BlockIds::STONE);
    world.setBlock(2, y, 1, BlockIds::AIR);
    world.setBlock(2, y + 1, 1, BlockIds::AIR);
}

StateID leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID repeaterState(const uint16_t delay, const bool powered) {
    return BlockStateRegistry::getState(
        BlockIds::REPEATER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::DELAY, delay}
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

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    const int y = 96;
    prepareFlatTestLine(world, y);
    world.setBlockState(0, y, 0, leverState(true));
    world.setBlockState(1, y, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
    world.setBlockState(2, y, 0, repeaterState(PropIndices::DELAY_2, false));
    world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
    world.setBlockState(4, y, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));
    world.setBlockState(2, y, 1, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));

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

    std::cout << "[redstone_repeater_test] PASS\n";
    return EXIT_SUCCESS;
}
