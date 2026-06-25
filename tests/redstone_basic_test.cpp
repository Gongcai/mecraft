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
    std::cerr << "[redstone_basic_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareFlatTestLine(World& world, const int y) {
    for (int x = -1; x <= 8; ++x) {
        world.setBlock(x, y - 1, 0, BlockIds::STONE);
        world.setBlock(x, y, 0, BlockIds::AIR);
        world.setBlock(x, y + 1, 0, BlockIds::AIR);
    }
}

StateID leverState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockIds::LEVER,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID redstoneTorchState(const bool lit) {
    return BlockStateRegistry::getState(
        BlockIds::REDSTONE_TORCH,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
            {PropIndices::LIT, lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE}
        });
}

StateID buttonState(const BlockID blockId, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_FLOOR},
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

bool lampLit(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::LIT) == PropIndices::LIT_TRUE;
}

bool torchLit(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::LIT) == PropIndices::LIT_TRUE;
}

bool buttonPowered(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::POWERED) == PropIndices::POWERED_TRUE;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    const BlockDef& stoneDef = BlockRegistry::get(BlockIds::STONE);
    if (!stoneDef.isRedstoneConductor) {
        return fail("stone should conduct redstone power by default");
    }

    const BlockDef& glassDef = BlockRegistry::get(BlockIds::GLASS);
    if (glassDef.isRedstoneConductor) {
        return fail("glass should be marked as non-conductive");
    }

    const BlockDef& leverDef = BlockRegistry::get(BlockIds::LEVER);
    if (leverDef.redstoneBehavior != "lever" ||
        !leverDef.isRedstonePowerSource ||
        leverDef.redstonePowerOutput != 15) {
        return fail("lever should parse its redstone power source metadata");
    }

    const BlockDef& lampDef = BlockRegistry::get(BlockIds::REDSTONE_LAMP);
    if (lampDef.redstoneBehavior != "lamp" || !lampDef.respondsToRedstone) {
        return fail("redstone_lamp should parse its redstone consumer metadata");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockIds::REDSTONE_TORCH);
    if (torchDef.redstoneBehavior != "torch" ||
        !torchDef.isRedstonePowerSource ||
        torchDef.redstonePowerOutput != 15) {
        return fail("redstone_torch should parse its redstone power source metadata");
    }

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    {
        const int quietY = 120;
        prepareFlatTestLine(world, quietY);
        world.setBlockState(0, quietY, 0, leverState(true));
        world.setBlockState(1, quietY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
        world.setBlockState(2, quietY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));
        world.redstoneUpdateQueue().clear();

        if (ecs::RedstoneSystem::processWorld(world, 0) != 0) {
            return fail("redstone processing should not scan the whole world when the dirty queue is empty");
        }
        if (wirePower(world, 1, quietY, 0) != 0 || lampLit(world, 2, quietY, 0)) {
            return fail("redstone processing without dirty entries should leave unrelated circuits unchanged");
        }
    }

    const int y = 96;
    prepareFlatTestLine(world, y);

    world.setBlockState(0, y, 0, leverState(true));
    for (int x = 1; x <= 5; ++x) {
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
    }
    world.setBlockState(6, y, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));

    ecs::RedstoneSystem::processWorld(world, 0);

    for (int x = 1; x <= 5; ++x) {
        const uint8_t expectedPower = static_cast<uint8_t>(16 - x);
        if (wirePower(world, x, y, 0) != expectedPower) {
            return fail("redstone wire power should decay by one after each wire block");
        }
    }
    if (!lampLit(world, 6, y, 0)) {
        return fail("redstone lamp should light when adjacent wire is powered");
    }

    world.setBlockState(0, y, 0, leverState(false));
    ecs::RedstoneSystem::processWorld(world, 1);

    for (int x = 1; x <= 5; ++x) {
        if (wirePower(world, x, y, 0) != 0) {
            return fail("redstone wire power should reset after the source turns off");
        }
    }
    if (lampLit(world, 6, y, 0)) {
        return fail("redstone lamp should turn off after adjacent wire loses power");
    }

    const int torchY = 80;
    prepareFlatTestLine(world, torchY);

    world.setBlockState(0, torchY, 0, redstoneTorchState(true));
    world.setBlockState(1, torchY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
    world.setBlockState(2, torchY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));
    ecs::RedstoneSystem::processWorld(world, 2);

    if (!torchLit(world, 0, torchY, 0) ||
        wirePower(world, 1, torchY, 0) != 15 ||
        !lampLit(world, 2, torchY, 0)) {
        return fail("lit redstone_torch should power adjacent wire and lamp");
    }

    world.setBlockState(-1, torchY - 1, 0, leverState(true));
    ecs::RedstoneSystem::processWorld(world, 3);

    if (torchLit(world, 0, torchY, 0) ||
        wirePower(world, 1, torchY, 0) != 0 ||
        lampLit(world, 2, torchY, 0)) {
        return fail("powered support block should turn off the attached redstone_torch");
    }

    world.setBlockState(-1, torchY - 1, 0, leverState(false));
    ecs::RedstoneSystem::processWorld(world, 4);

    if (!torchLit(world, 0, torchY, 0) ||
        wirePower(world, 1, torchY, 0) != 15 ||
        !lampLit(world, 2, torchY, 0)) {
        return fail("redstone_torch should relight after its support block loses power");
    }

    const int buttonY = 64;
    prepareFlatTestLine(world, buttonY);

    world.setBlockState(0, buttonY, 0, buttonState(BlockIds::STONE_BUTTON, true));
    world.setBlockState(1, buttonY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_WIRE));
    world.setBlockState(2, buttonY, 0, BlockStateRegistry::getDefaultState(BlockIds::REDSTONE_LAMP));
    ecs::RedstoneSystem::processWorld(world, 5);

    if (!buttonPowered(world, 0, buttonY, 0) ||
        wirePower(world, 1, buttonY, 0) != 15 ||
        !lampLit(world, 2, buttonY, 0)) {
        return fail("powered stone_button should immediately power adjacent wire and lamp");
    }

    ecs::RedstoneSystem::processWorld(world, 14);
    if (!buttonPowered(world, 0, buttonY, 0) ||
        wirePower(world, 1, buttonY, 0) != 15 ||
        !lampLit(world, 2, buttonY, 0)) {
        return fail("stone_button pulse should stay active before its release tick");
    }

    ecs::RedstoneSystem::processWorld(world, 15);
    if (buttonPowered(world, 0, buttonY, 0) ||
        wirePower(world, 1, buttonY, 0) != 0 ||
        lampLit(world, 2, buttonY, 0)) {
        return fail("stone_button should release after ten redstone ticks and remove output power");
    }

    std::cout << "[redstone_basic_test] PASS\n";
    return EXIT_SUCCESS;
}
