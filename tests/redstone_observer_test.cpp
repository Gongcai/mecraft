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
    std::cerr << "[redstone_observer_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareObserverTestArea(World& world, const int y) {
    for (int x = -1; x <= 4; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.setBlock(x, y - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
            world.setBlock(x, y, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 1, z, RUNTIME_ID_NULL);
        }
    }
}

StateID observerState(const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:observer"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
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
    prepareObserverTestArea(world, y);
    world.setBlockState(1, y, 0, observerState(PropIndices::FACING_WEST, false));
    world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
    world.redstoneUpdateQueue().clear();
    world.redstoneChangedBlockQueue().clear();
    world.redstoneScheduledUpdateQueue().clear();

    world.setBlock(1, y, 1, BlockRegistry::requireIdByName("minecraft:stone"));
    ecs::RedstoneSystem::processWorld(world, 0);
    ecs::RedstoneSystem::processWorld(world, 1);
    ecs::RedstoneSystem::processWorld(world, 2);
    if (powered(world, 1, y, 0) ||
        wirePower(world, 2, y, 0) != 0 ||
        lampLit(world, 3, y, 0)) {
        return fail("observer should ignore changes outside its facing direction");
    }

    world.setBlock(0, y, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    ecs::RedstoneSystem::processWorld(world, 3);
    if (powered(world, 1, y, 0) ||
        wirePower(world, 2, y, 0) != 0 ||
        lampLit(world, 3, y, 0)) {
        return fail("observer should not pulse until the scheduled redstone tick");
    }

    ecs::RedstoneSystem::processWorld(world, 4);
    if (!powered(world, 1, y, 0) ||
        wirePower(world, 2, y, 0) != 15 ||
        !lampLit(world, 3, y, 0)) {
        return fail("observer should emit a powered pulse from its back face");
    }

    ecs::RedstoneSystem::processWorld(world, 5);
    if (powered(world, 1, y, 0) ||
        wirePower(world, 2, y, 0) != 0 ||
        lampLit(world, 3, y, 0)) {
        return fail("observer pulse should release after one redstone tick");
    }

    std::cout << "[redstone_observer_test] PASS\n";
    return EXIT_SUCCESS;
}
