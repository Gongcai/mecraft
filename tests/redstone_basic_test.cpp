#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/entity/EntityFactory.h"
#include "../src/ecs/systems/combat/ProjectileSystem.h"
#include "../src/ecs/systems/world/RedstoneDeviceActionSystem.h"
#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/ecs/util/AudioEventBuffer.h"
#include "../src/ecs/util/RedstoneEventBuffer.h"
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
        world.setBlock(x, y - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(x, y, 0, RUNTIME_ID_NULL);
        world.setBlock(x, y + 1, 0, RUNTIME_ID_NULL);
    }
}

void prepareFlatTestArea(World& world,
                         const int y,
                         const int minX,
                         const int maxX,
                         const int minZ,
                         const int maxZ) {
    for (int x = minX; x <= maxX; ++x) {
        for (int z = minZ; z <= maxZ; ++z) {
            world.setBlock(x, y - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
            world.setBlock(x, y, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 1, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 2, z, RUNTIME_ID_NULL);
        }
    }
}

void processRedstoneTicks(World& world, uint64_t& tick, const int count) {
    for (int i = 0; i < count; ++i) {
        ecs::RedstoneSystem::processWorld(world, tick);
        ++tick;
    }
}

struct BinaryGateCase {
    bool inputA = false;
    bool inputB = false;
    bool expectedOutput = false;
};

StateID leverState(const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:lever"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID leverState(const bool powered) {
    return leverState(PropIndices::FACING_FLOOR, powered);
}

StateID redstoneTorchState(const uint16_t facing, const bool lit) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:redstone_torch"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::LIT, lit ? PropIndices::LIT_TRUE : PropIndices::LIT_FALSE}
        });
}

StateID redstoneTorchState(const bool lit) {
    return redstoneTorchState(PropIndices::FACING_FLOOR, lit);
}

StateID buttonState(const BlockID blockId, const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

StateID buttonState(const BlockID blockId, const bool powered) {
    return buttonState(blockId, PropIndices::FACING_FLOOR, powered);
}

StateID pressurePlateState(const BlockID blockId, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE}
        });
}

uint16_t powerPropertyValue(const uint8_t power) {
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
    if (power >= kPowerValues.size()) {
        throw std::runtime_error("Redstone power test value must be in the range 0 through 15");
    }
    return kPowerValues[power];
}

StateID repeaterState(const uint16_t facing, const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:repeater"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::POWERED, powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE},
            {PropIndices::DELAY, PropIndices::DELAY_1}
        });
}

StateID hopperState(const bool enabled) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:hopper"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_DOWN},
            {PropIndices::ENABLED, enabled ? PropIndices::ENABLED_TRUE : PropIndices::ENABLED_FALSE}
        });
}

StateID targetState(const uint8_t power) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:target"),
        PropIndices::POWER,
        powerPropertyValue(power));
}

StateID wireState(const BlockID blockId, const uint16_t facing, const uint8_t power = 0) {
    StateID state = BlockStateRegistry::getDefaultState(blockId);
    state = BlockStateRegistry::withProperty(state, PropIndices::FACING, facing);
    return BlockStateRegistry::withProperty(state, PropIndices::POWER, powerPropertyValue(power));
}

StateID redWireState(const uint16_t facing, const uint8_t power = 0) {
    return wireState(BlockRegistry::requireIdByName("minecraft:redstone_wire"), facing, power);
}

StateID blueWireState(const uint16_t facing, const uint8_t power = 0) {
    return wireState(BlockRegistry::requireIdByName("minecraft:blue_redstone_wire"), facing, power);
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

bool powered(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::POWERED) == PropIndices::POWERED_TRUE;
}

bool noteBlockPowered(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::POWERED) == PropIndices::POWERED_TRUE;
}

bool hopperEnabled(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::ENABLED) == PropIndices::ENABLED_TRUE;
}

uint16_t blockProperty(const World& world, const int x, const int y, const int z, const uint16_t property) {
    return BlockStateRegistry::getPropertyIndex(world.getBlockState(x, y, z), property);
}

uint8_t targetPower(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    const uint16_t value = BlockStateRegistry::getPropertyIndex(state, PropIndices::POWER);
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
    for (uint8_t power = 0; power < kPowerValues.size(); ++power) {
        if (value == kPowerValues[power]) {
            return power;
        }
    }
    throw std::runtime_error("Target state does not contain a valid power value");
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    const BlockDef& stoneDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:stone"));
    if (!stoneDef.isRedstoneConductor) {
        return fail("stone should conduct redstone power by default");
    }

    const BlockDef& glassDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:glass"));
    if (glassDef.isRedstoneConductor) {
        return fail("glass should be marked as non-conductive");
    }

    const BlockDef& leverDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:lever"));
    if (leverDef.redstoneBehavior != "lever" ||
        !leverDef.isRedstonePowerSource ||
        leverDef.redstonePowerOutput != 15) {
        return fail("lever should parse its redstone power source metadata");
    }

    const BlockDef& lampDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_lamp"));
    if (lampDef.redstoneBehavior != "lamp" ||
        !lampDef.respondsToRedstone ||
        lampDef.redstoneControlledProperty != "lit") {
        return fail("redstone_lamp should parse its redstone consumer metadata");
    }

    const BlockDef& torchDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:redstone_torch"));
    if (torchDef.redstoneBehavior != "torch" ||
        !torchDef.isRedstonePowerSource ||
        torchDef.redstonePowerOutput != 15) {
        return fail("redstone_torch should parse its redstone power source metadata");
    }

    const BlockDef& targetDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:target"));
    if (targetDef.redstoneBehavior != "target" || !targetDef.isRedstonePowerSource) {
        return fail("target should parse its variable redstone power source metadata");
    }

    const BlockDef& hopperDef = BlockRegistry::get(BlockRegistry::requireIdByName("minecraft:hopper"));
    if (hopperDef.redstoneBehavior != "hopper" ||
        !hopperDef.respondsToRedstone ||
        hopperDef.redstoneControlledProperty != "enabled" ||
        !hopperDef.redstoneControlledPowerInverted) {
        return fail("hopper should declare inverted enabled redstone control metadata");
    }

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    {
        const int quietY = 120;
        prepareFlatTestLine(world, quietY);
        world.setBlockState(0, quietY, 0, leverState(true));
        world.setBlockState(1, quietY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, quietY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
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
        world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    }
    world.setBlockState(6, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

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

    {
        World faceWorld;
        faceWorld.init(20260628);
        loadOriginChunks(faceWorld);

        const int supportY = 72;
        for (int x = -2; x <= 2; ++x) {
            for (int yOffset = -1; yOffset <= 2; ++yOffset) {
                for (int z = -2; z <= 2; ++z) {
                    faceWorld.setBlockState(x, supportY + yOffset, z, RUNTIME_ID_NULL);
                }
            }
        }

        faceWorld.setBlockState(0, supportY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        faceWorld.setBlockState(-1, supportY + 1, 0, leverState(true));
        faceWorld.setBlockState(0, supportY + 1, 0, redWireState(PropIndices::FACING_FLOOR));
        faceWorld.setBlockState(0, supportY, -1, redWireState(PropIndices::FACING_NORTH));
        ecs::RedstoneSystem::processWorld(faceWorld, 24);

        if (wirePower(faceWorld, 0, supportY + 1, 0) != 15 ||
            wirePower(faceWorld, 0, supportY, -1) != 14) {
            return fail("same-color redstone wire should carry power around a shared support-block edge");
        }
        if (blockProperty(faceWorld, 0, supportY + 1, 0, PropIndices::NORTH) != PropIndices::NORTH_SIDE ||
            blockProperty(faceWorld, 0, supportY, -1, PropIndices::NORTH) != PropIndices::NORTH_SIDE) {
            return fail("same-color redstone wire should show side connections around a support-block edge");
        }

        faceWorld.setBlockState(0, supportY + 1, 0, RUNTIME_ID_NULL);
        ecs::RedstoneSystem::processWorld(faceWorld, 25);
        if (wirePower(faceWorld, 0, supportY, -1) != 0 ||
            blockProperty(faceWorld, 0, supportY, -1, PropIndices::NORTH) != PropIndices::NORTH_NONE) {
            return fail("removing a corner-connected wire should dirty and refresh the remaining face wire");
        }

        faceWorld.setBlockState(0, supportY + 1, 0, redWireState(PropIndices::FACING_FLOOR));
        faceWorld.setBlockState(0, supportY, -1, blueWireState(PropIndices::FACING_NORTH));
        ecs::RedstoneSystem::processWorld(faceWorld, 26);
        if (wirePower(faceWorld, 0, supportY + 1, 0) != 15 ||
            wirePower(faceWorld, 0, supportY, -1) != 0) {
            return fail("different-color redstone wire should not carry power around a shared support-block edge");
        }
        if (blockProperty(faceWorld, 0, supportY + 1, 0, PropIndices::NORTH) != PropIndices::NORTH_NONE ||
            blockProperty(faceWorld, 0, supportY, -1, PropIndices::NORTH) != PropIndices::NORTH_NONE) {
            return fail("different-color redstone wire should not show corner visual connections");
        }
    }

    {
        const int conductorY = 112;
        prepareFlatTestLine(world, conductorY);
        world.setBlockState(0, conductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, conductorY + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(-1, conductorY, 0, leverState(PropIndices::FACING_WEST, true));
        ecs::RedstoneSystem::processWorld(world, 20);

        if (wirePower(world, 0, conductorY + 1, 0) != 15) {
            return fail("wall lever should power redstone wire through its supporting conductor block");
        }

        world.setBlockState(-1, conductorY, 0, leverState(PropIndices::FACING_WEST, false));
        ecs::RedstoneSystem::processWorld(world, 21);
        if (wirePower(world, 0, conductorY + 1, 0) != 0) {
            return fail("wall lever should remove conducted power from redstone wire when switched off");
        }
    }

    {
        const int conductorButtonY = 48;
        prepareFlatTestLine(world, conductorButtonY);
        world.setBlockState(0, conductorButtonY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, conductorButtonY + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(-1, conductorButtonY, 0, buttonState(
            BlockRegistry::requireIdByName("minecraft:stone_button"),
            PropIndices::FACING_WEST,
            true));
        ecs::RedstoneSystem::processWorld(world, 30);

        if (!buttonPowered(world, -1, conductorButtonY, 0) ||
            wirePower(world, 0, conductorButtonY + 1, 0) != 15) {
            return fail("wall button should power redstone wire through its supporting conductor block");
        }

        ecs::RedstoneSystem::processWorld(world, 40);
        if (buttonPowered(world, -1, conductorButtonY, 0) ||
            wirePower(world, 0, conductorButtonY + 1, 0) != 0) {
            return fail("wall button release should remove conducted power from redstone wire");
        }
    }

    {
        const int torchConductorY = 104;
        prepareFlatTestLine(world, torchConductorY);
        world.setBlockState(0, torchConductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(
            0,
            torchConductorY + 1,
            0,
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(-1, torchConductorY, 0, redstoneTorchState(PropIndices::FACING_WEST, true));
        ecs::RedstoneSystem::processWorld(world, 31);

        if (!torchLit(world, -1, torchConductorY, 0) ||
            wirePower(world, 0, torchConductorY + 1, 0) != 0) {
            return fail("wall redstone_torch should not power wire through its supporting conductor block");
        }
    }

    {
        const int sharedSupportTorchY = 88;
        prepareFlatTestLine(world, sharedSupportTorchY);
        world.setBlockState(0, sharedSupportTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(-1, sharedSupportTorchY, 0, redstoneTorchState(PropIndices::FACING_WEST, true));
        world.setBlockState(1, sharedSupportTorchY, 0, redstoneTorchState(PropIndices::FACING_EAST, true));
        ecs::RedstoneSystem::processWorld(world, 32);

        if (!torchLit(world, -1, sharedSupportTorchY, 0) ||
            !torchLit(world, 1, sharedSupportTorchY, 0)) {
            return fail("redstone_torches on the same supporting block should not power that support block");
        }
    }

    {
        const int torchBelowWireY = 116;
        prepareFlatTestLine(world, torchBelowWireY);
        world.setBlockState(0, torchBelowWireY - 2, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, torchBelowWireY - 1, 0, redstoneTorchState(true));
        world.setBlockState(0, torchBelowWireY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(
            0,
            torchBelowWireY + 1,
            0,
            BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        ecs::RedstoneSystem::processWorld(world, 34);

        if (!torchLit(world, 0, torchBelowWireY - 1, 0) ||
            wirePower(world, 0, torchBelowWireY + 1, 0) != 15) {
            return fail("redstone_torch below a conductor should power wire on top of that conductor");
        }
    }

    {
        const int verticalTorchY = 100;
        prepareFlatTestLine(world, verticalTorchY);
        world.setBlockState(0, verticalTorchY - 2, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, verticalTorchY - 1, 0, redstoneTorchState(true));
        world.setBlockState(0, verticalTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, verticalTorchY + 1, 0, redstoneTorchState(true));
        ecs::RedstoneSystem::processWorld(world, 35);

        if (!torchLit(world, 0, verticalTorchY - 1, 0) ||
            torchLit(world, 0, verticalTorchY + 1, 0)) {
            return fail("redstone_torch above a conductor should turn off when a lower torch powers that conductor");
        }
    }

    {
        const int sideTorchY = 52;
        prepareFlatTestLine(world, sideTorchY);
        world.setBlockState(0, sideTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, sideTorchY + 1, 0, redstoneTorchState(true));
        world.setBlockState(-1, sideTorchY, 0, redstoneTorchState(PropIndices::FACING_WEST, true));
        ecs::RedstoneSystem::processWorld(world, 36);

        if (!torchLit(world, -1, sideTorchY, 0) ||
            !torchLit(world, 0, sideTorchY + 1, 0)) {
            return fail("side redstone_torch should not power the block it is attached to");
        }
    }

    {
        const int adjacentTorchY = 56;
        prepareFlatTestLine(world, adjacentTorchY);
        world.setBlockState(0, adjacentTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, adjacentTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, adjacentTorchY + 1, 0, redstoneTorchState(true));
        world.setBlockState(1, adjacentTorchY + 1, 0, redstoneTorchState(true));
        ecs::RedstoneSystem::processWorld(world, 33);

        if (!torchLit(world, 0, adjacentTorchY + 1, 0) ||
            !torchLit(world, 1, adjacentTorchY + 1, 0)) {
            return fail("adjacent redstone_torches should not charge neighboring conductor blocks");
        }
    }

    {
        const int linkedTorchesY = 72;
        prepareFlatTestLine(world, linkedTorchesY);
        world.setBlockState(0, linkedTorchesY - 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(3, linkedTorchesY - 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, linkedTorchesY, 0, redstoneTorchState(true));
        world.setBlockState(1, linkedTorchesY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, linkedTorchesY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(3, linkedTorchesY, 0, redstoneTorchState(true));

        for (uint64_t tick = 39; tick < 45; ++tick) {
            ecs::RedstoneSystem::processWorld(world, tick);
            if (!torchLit(world, 0, linkedTorchesY, 0) ||
                !torchLit(world, 3, linkedTorchesY, 0) ||
                wirePower(world, 1, linkedTorchesY, 0) != 15 ||
                wirePower(world, 2, linkedTorchesY, 0) != 15) {
                return fail("redstone_torches linked by dust should remain stable across redstone ticks");
            }
        }
    }

    {
        const int repeaterTorchY = 44;
        prepareFlatTestLine(world, repeaterTorchY);
        world.setBlockState(0, repeaterTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, repeaterTorchY + 1, 0, redstoneTorchState(true));
        world.setBlockState(-1, repeaterTorchY, 0, repeaterState(PropIndices::FACING_EAST, true));
        ecs::RedstoneSystem::processWorld(world, 37);

        if (torchLit(world, 0, repeaterTorchY + 1, 0)) {
            return fail("repeater directly powering a torch support block should turn that torch off");
        }
    }

    {
        const int repeaterChainY = 28;
        prepareFlatTestLine(world, repeaterChainY);
        world.setBlockState(0, repeaterChainY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, repeaterChainY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, repeaterChainY + 1, 0, redstoneTorchState(true));
        world.setBlockState(-1, repeaterChainY, 0, repeaterState(PropIndices::FACING_EAST, true));
        ecs::RedstoneSystem::processWorld(world, 38);

        if (!torchLit(world, 1, repeaterChainY + 1, 0)) {
            return fail("repeater power should not chain through one conductor into another torch support block");
        }
    }

    {
        const int plateWireTorchY = 132;
        prepareFlatTestLine(world, plateWireTorchY);
        world.setBlockState(0, plateWireTorchY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), true));
        world.setBlockState(1, plateWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, plateWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(3, plateWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(3, plateWireTorchY + 1, 0, redstoneTorchState(true));
        ecs::RedstoneSystem::processWorld(world, 39);

        if (!powered(world, 0, plateWireTorchY, 0) ||
            wirePower(world, 2, plateWireTorchY, 0) == 0 ||
            torchLit(world, 3, plateWireTorchY + 1, 0)) {
            return fail("pressure plate power carried by redstone wire should turn off a torch support block");
        }

        world.setBlockState(0, plateWireTorchY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), false));
        ecs::RedstoneSystem::processWorld(world, 40);
        if (wirePower(world, 2, plateWireTorchY, 0) != 0 ||
            !torchLit(world, 3, plateWireTorchY + 1, 0)) {
            return fail("torch should relight when wire-carried pressure plate power is removed");
        }
    }

    {
        const int repeaterWireTorchY = 136;
        prepareFlatTestLine(world, repeaterWireTorchY);
        world.setBlockState(0, repeaterWireTorchY, 0, repeaterState(PropIndices::FACING_EAST, true));
        world.setBlockState(1, repeaterWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, repeaterWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(3, repeaterWireTorchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(3, repeaterWireTorchY + 1, 0, redstoneTorchState(true));
        ecs::RedstoneSystem::processWorld(world, 41);

        if (wirePower(world, 2, repeaterWireTorchY, 0) == 0 ||
            torchLit(world, 3, repeaterWireTorchY + 1, 0)) {
            return fail("repeater output carried by redstone wire should turn off a torch support block");
        }
    }

    {
        const int weakConductorY = 140;
        prepareFlatTestLine(world, weakConductorY);
        world.setBlockState(0, weakConductorY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), true));
        world.setBlockState(1, weakConductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, weakConductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(3, weakConductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(4, weakConductorY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
        ecs::RedstoneSystem::processWorld(world, 42);

        if (wirePower(world, 1, weakConductorY, 0) == 0 ||
            wirePower(world, 3, weakConductorY, 0) != 0 ||
            lampLit(world, 4, weakConductorY, 0)) {
            return fail("wire weak-powered conductor should not strongly power redstone dust on another face");
        }
    }

    const int torchY = 80;
    prepareFlatTestLine(world, torchY);

    world.setBlockState(0, torchY, 0, redstoneTorchState(true));
    world.setBlockState(1, torchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(2, torchY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
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

    {
        const int notGateY = 124;
        prepareFlatTestArea(world, notGateY, -1, 5, -1, 1);
        world.setBlockState(1, notGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(2, notGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, notGateY, 0, leverState(PropIndices::FACING_WEST, false));
        world.setBlockState(1, notGateY + 1, 0, redstoneTorchState(true));
        world.setBlockState(2, notGateY + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(3, notGateY + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        uint64_t logicTick = 70;
        world.setBlockState(0, notGateY, 0, leverState(PropIndices::FACING_WEST, false));
        processRedstoneTicks(world, logicTick, 1);
        if (!torchLit(world, 1, notGateY + 1, 0) ||
            wirePower(world, 2, notGateY + 1, 0) != 15 ||
            !lampLit(world, 3, notGateY + 1, 0)) {
            return fail("NOT gate should output high when the input is low");
        }

        world.setBlockState(0, notGateY, 0, leverState(PropIndices::FACING_WEST, true));
        processRedstoneTicks(world, logicTick, 1);
        if (torchLit(world, 1, notGateY + 1, 0) ||
            wirePower(world, 2, notGateY + 1, 0) != 0 ||
            lampLit(world, 3, notGateY + 1, 0)) {
            return fail("NOT gate should output low when the input is high");
        }
    }

    {
        const int orGateY = 126;
        prepareFlatTestArea(world, orGateY, -1, 5, -2, 2);
        world.setBlockState(0, orGateY, -1, leverState(false));
        world.setBlockState(0, orGateY, 1, leverState(false));
        world.setBlockState(1, orGateY, -1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(1, orGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(1, orGateY, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, orGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(3, orGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        uint64_t logicTick = 80;
        const std::array<BinaryGateCase, 4> truthTable = {{
            {false, false, false},
            {true, false, true},
            {false, true, true},
            {true, true, true},
        }};
        for (const BinaryGateCase& gateCase : truthTable) {
            world.setBlockState(0, orGateY, -1, leverState(gateCase.inputA));
            world.setBlockState(0, orGateY, 1, leverState(gateCase.inputB));
            processRedstoneTicks(world, logicTick, 1);
            const bool outputLit = lampLit(world, 3, orGateY, 0);
            const bool outputWirePowered = wirePower(world, 2, orGateY, 0) > 0;
            if (outputLit != gateCase.expectedOutput || outputWirePowered != gateCase.expectedOutput) {
                return fail("OR gate truth table should match A OR B");
            }
        }
    }

    {
        const int norGateY = 128;
        prepareFlatTestArea(world, norGateY, 0, 5, -2, 1);
        world.setBlockState(2, norGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, norGateY, 0, leverState(PropIndices::FACING_WEST, false));
        world.setBlockState(2, norGateY, -1, leverState(PropIndices::FACING_NORTH, false));
        world.setBlockState(3, norGateY, 0, redstoneTorchState(PropIndices::FACING_EAST, true));
        world.setBlockState(4, norGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        uint64_t logicTick = 90;
        const std::array<BinaryGateCase, 4> truthTable = {{
            {false, false, true},
            {true, false, false},
            {false, true, false},
            {true, true, false},
        }};
        for (const BinaryGateCase& gateCase : truthTable) {
            world.setBlockState(1, norGateY, 0, leverState(PropIndices::FACING_WEST, gateCase.inputA));
            world.setBlockState(2, norGateY, -1, leverState(PropIndices::FACING_NORTH, gateCase.inputB));
            processRedstoneTicks(world, logicTick, 1);
            const bool outputLit = lampLit(world, 4, norGateY, 0);
            if (torchLit(world, 3, norGateY, 0) != gateCase.expectedOutput ||
                outputLit != gateCase.expectedOutput) {
                return fail("NOR gate truth table should match NOT (A OR B)");
            }
        }
    }

    {
        const int andGateY = 130;
        prepareFlatTestArea(world, andGateY, -2, 5, -2, 2);
        world.setBlockState(0, andGateY, -1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(0, andGateY, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, andGateY, -1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, andGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(1, andGateY, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(-1, andGateY, -1, leverState(PropIndices::FACING_WEST, false));
        world.setBlockState(-1, andGateY, 1, leverState(PropIndices::FACING_WEST, false));
        world.setBlockState(0, andGateY + 1, -1, redstoneTorchState(true));
        world.setBlockState(0, andGateY + 1, 1, redstoneTorchState(true));
        world.setBlockState(1, andGateY + 1, -1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(1, andGateY + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(1, andGateY + 1, 1, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, andGateY, 0, redstoneTorchState(PropIndices::FACING_EAST, true));
        world.setBlockState(3, andGateY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        uint64_t logicTick = 100;
        const std::array<BinaryGateCase, 4> truthTable = {{
            {false, false, false},
            {true, false, false},
            {false, true, false},
            {true, true, true},
        }};
        for (const BinaryGateCase& gateCase : truthTable) {
            world.setBlockState(-1, andGateY, -1, leverState(PropIndices::FACING_WEST, gateCase.inputA));
            world.setBlockState(-1, andGateY, 1, leverState(PropIndices::FACING_WEST, gateCase.inputB));
            processRedstoneTicks(world, logicTick, 2);
            const bool outputLit = lampLit(world, 3, andGateY, 0);
            const bool intermediateNandPowered = wirePower(world, 1, andGateY + 1, 0) > 0;
            if (outputLit != gateCase.expectedOutput ||
                torchLit(world, 2, andGateY, 0) != gateCase.expectedOutput ||
                intermediateNandPowered != !gateCase.expectedOutput) {
                return fail("AND gate truth table should match A AND B with a NAND intermediate line");
            }
        }
    }

    const int buttonY = 64;
    prepareFlatTestLine(world, buttonY);

    world.setBlockState(0, buttonY, 0, buttonState(BlockRegistry::requireIdByName("minecraft:stone_button"), true));
    world.setBlockState(1, buttonY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    world.setBlockState(2, buttonY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
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

    {
        const int targetY = 40;
        prepareFlatTestLine(world, targetY);
        world.setBlockState(0, targetY, 0, targetState(12));
        world.setBlockState(1, targetY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, targetY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));
        world.redstoneScheduledUpdateQueue().schedule(
            52,
            glm::ivec3(0, targetY, 0),
            RedstoneScheduledAction::ReleaseTargetPulse);
        world.redstoneScheduledUpdateQueue().reschedule(
            54,
            glm::ivec3(0, targetY, 0),
            RedstoneScheduledAction::ReleaseTargetPulse);

        ecs::RedstoneSystem::processWorld(world, 50);
        if (targetPower(world, 0, targetY, 0) != 12 ||
            wirePower(world, 1, targetY, 0) != 12 ||
            !lampLit(world, 2, targetY, 0)) {
            return fail("target should output its stored variable redstone power");
        }

        ecs::RedstoneSystem::processWorld(world, 52);
        if (targetPower(world, 0, targetY, 0) != 12 ||
            wirePower(world, 1, targetY, 0) != 12 ||
            !lampLit(world, 2, targetY, 0)) {
            return fail("target pulse reschedule should ignore the stale release tick");
        }

        ecs::RedstoneSystem::processWorld(world, 54);
        if (targetPower(world, 0, targetY, 0) != 0 ||
            wirePower(world, 1, targetY, 0) != 0 ||
            lampLit(world, 2, targetY, 0)) {
            return fail("target pulse release should clear its output power");
        }
    }

    {
        const int hopperY = 36;
        prepareFlatTestLine(world, hopperY);
        world.setBlockState(0, hopperY, 0, leverState(true));
        world.setBlockState(1, hopperY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, hopperY, 0, hopperState(true));
        ecs::RedstoneSystem::processWorld(world, 55);

        if (hopperEnabled(world, 2, hopperY, 0)) {
            return fail("powered hopper should set enabled=false through inverted redstone control");
        }

        world.setBlockState(0, hopperY, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 56);
        if (!hopperEnabled(world, 2, hopperY, 0)) {
            return fail("unpowered hopper should restore enabled=true through inverted redstone control");
        }
    }

    {
        const int projectileTargetY = 24;
        prepareFlatTestLine(world, projectileTargetY);
        world.setBlockState(2, projectileTargetY, 0, targetState(0));
        world.setBlockState(3, projectileTargetY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(4, projectileTargetY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        ecs::GameplayRegistry registry;
        ecs::GameplayServices services;
        services.world = &world;
        services.worldView = &world;

        ecs::ProjectileDefinition appleProjectile;
        appleProjectile.itemId = ItemRegistry::requireIdByName("minecraft:apple");
        appleProjectile.gravity = 0.0f;
        appleProjectile.throwSpeed = 0.0f;
        appleProjectile.upwardBias = 0.0f;
        appleProjectile.spawnForwardOffset = 0.0f;
        appleProjectile.entityImpactParticleCount = 0;
        appleProjectile.impactSoundId.clear();

        ecs::EntityFactory::createProjectile(
            registry,
            entt::null,
            glm::vec3(0.25f, static_cast<float>(projectileTargetY) + 0.5f, 0.5f),
            glm::vec3(12.5f, 0.0f, 0.0f),
            appleProjectile);

        ecs::ProjectileSystem projectileSystem;
        ecs::SystemContext projectileContext{registry, services, 0.2f, 100};
        projectileSystem.update(projectileContext);
        projectileSystem.update(projectileContext);

        if (targetPower(world, 2, projectileTargetY, 0) == 0) {
            return fail("apple projectile impact should store redstone power in the target block");
        }

        ecs::RedstoneSystem::processWorld(world, 50, registry);
        if (wirePower(world, 3, projectileTargetY, 0) == 0 ||
            !lampLit(world, 4, projectileTargetY, 0)) {
            return fail("target hit by an apple projectile should power adjacent redstone components");
        }
    }

    {
        const int projectileTargetEdgeY = 16;
        prepareFlatTestLine(world, projectileTargetEdgeY);
        for (int x = -1; x <= 8; ++x) {
            world.setBlockState(x, projectileTargetEdgeY, -1, RUNTIME_ID_NULL);
            world.setBlockState(x, projectileTargetEdgeY + 1, -1, RUNTIME_ID_NULL);
        }
        world.setBlockState(2, projectileTargetEdgeY, 0, targetState(0));
        world.setBlockState(3, projectileTargetEdgeY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(4, projectileTargetEdgeY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        ecs::GameplayRegistry registry;
        ecs::GameplayServices services;
        services.world = &world;
        services.worldView = &world;

        ecs::ProjectileDefinition appleProjectile;
        appleProjectile.itemId = ItemRegistry::requireIdByName("minecraft:apple");
        appleProjectile.gravity = 0.0f;
        appleProjectile.throwSpeed = 0.0f;
        appleProjectile.upwardBias = 0.0f;
        appleProjectile.spawnForwardOffset = 0.0f;
        appleProjectile.entityImpactParticleCount = 0;
        appleProjectile.impactSoundId.clear();

        ecs::EntityFactory::createProjectile(
            registry,
            entt::null,
            glm::vec3(0.25f, static_cast<float>(projectileTargetEdgeY) + 0.5f, -0.12f),
            glm::vec3(12.5f, 0.0f, 0.0f),
            appleProjectile);

        ecs::ProjectileSystem projectileSystem;
        ecs::SystemContext projectileContext{registry, services, 0.2f, 120};
        projectileSystem.update(projectileContext);
        projectileSystem.update(projectileContext);

        if (targetPower(world, 2, projectileTargetEdgeY, 0) == 0) {
            return fail("apple projectile body intersection should activate the target block");
        }

        ecs::RedstoneSystem::processWorld(world, 60, registry);
        if (wirePower(world, 3, projectileTargetEdgeY, 0) == 0 ||
            !lampLit(world, 4, projectileTargetEdgeY, 0)) {
            return fail("edge hit on target should power adjacent redstone components");
        }
    }

    {
        const int projectileClockSkewY = 14;
        prepareFlatTestLine(world, projectileClockSkewY);
        world.redstoneUpdateQueue().clear();
        world.redstoneChangedBlockQueue().clear();
        world.redstoneScheduledUpdateQueue().clear();
        world.setBlockState(2, projectileClockSkewY, 0, targetState(0));
        world.setBlockState(3, projectileClockSkewY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(4, projectileClockSkewY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_lamp")));

        ecs::RedstoneSystem::processWorld(world, 100);

        ecs::GameplayRegistry registry;
        ecs::GameplayServices services;
        services.world = &world;
        services.worldView = &world;

        ecs::ProjectileDefinition appleProjectile;
        appleProjectile.itemId = ItemRegistry::requireIdByName("minecraft:apple");
        appleProjectile.gravity = 0.0f;
        appleProjectile.throwSpeed = 0.0f;
        appleProjectile.upwardBias = 0.0f;
        appleProjectile.spawnForwardOffset = 0.0f;
        appleProjectile.entityImpactParticleCount = 0;
        appleProjectile.impactSoundId.clear();

        ecs::EntityFactory::createProjectile(
            registry,
            entt::null,
            glm::vec3(0.25f, static_cast<float>(projectileClockSkewY) + 0.5f, 0.5f),
            glm::vec3(12.5f, 0.0f, 0.0f),
            appleProjectile);

        ecs::ProjectileSystem projectileSystem;
        ecs::SystemContext projectileContext{registry, services, 0.2f, 0};
        projectileSystem.update(projectileContext);
        projectileSystem.update(projectileContext);

        if (targetPower(world, 2, projectileClockSkewY, 0) == 0) {
            return fail("target projectile impact should not be released by a stale gameplay tick");
        }

        services.gameClient = reinterpret_cast<client::GameClient*>(static_cast<uintptr_t>(1));
        ecs::SystemContext clientRedstoneContext{registry, services, 0.0f, 10000};
        ecs::RedstoneSystem clientRedstoneSystem;
        clientRedstoneSystem.update(clientRedstoneContext);
        if (targetPower(world, 2, projectileClockSkewY, 0) == 0) {
            return fail("client redstone tick should not release server-authoritative target pulses");
        }
        services.gameClient = nullptr;

        ecs::RedstoneSystem::processWorld(world, 101, registry);
        if (wirePower(world, 3, projectileClockSkewY, 0) == 0 ||
            !lampLit(world, 4, projectileClockSkewY, 0)) {
            return fail("target projectile pulse should propagate when gameplay and redstone ticks are skewed");
        }

        ecs::RedstoneSystem::processWorld(world, 104, registry);
        if (targetPower(world, 2, projectileClockSkewY, 0) != 0 ||
            wirePower(world, 3, projectileClockSkewY, 0) != 0 ||
            lampLit(world, 4, projectileClockSkewY, 0)) {
            return fail("target projectile pulse should release on the redstone clock");
        }
    }

    {
        const int noteY = 32;
        prepareFlatTestLine(world, noteY);
        world.setBlockState(0, noteY, 0, leverState(true));
        world.setBlockState(1, noteY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        world.setBlockState(2, noteY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:note_block")));
        ecs::RedstoneSystem::processWorld(world, 60);

        if (!noteBlockPowered(world, 2, noteY, 0)) {
            return fail("note_block should store powered=true while receiving redstone power");
        }

        world.redstoneUpdateQueue().enqueue(glm::ivec3(2, noteY, 0));
        ecs::RedstoneSystem::processWorld(world, 61);
        if (!noteBlockPowered(world, 2, noteY, 0)) {
            return fail("note_block should remain powered while the input stays high");
        }

        world.setBlockState(0, noteY, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 62);
        if (noteBlockPowered(world, 2, noteY, 0)) {
            return fail("note_block should reset powered=false after redstone power is removed");
        }

        world.setBlockState(0, noteY, 0, leverState(true));
        ecs::RedstoneSystem::processWorld(world, 63);
        if (!noteBlockPowered(world, 2, noteY, 0)) {
            return fail("note_block should accept a second rising redstone edge");
        }
    }

    {
        World audioWorld;
        audioWorld.init(20260626);
        loadOriginChunks(audioWorld);

        const int noteAudioY = 72;
        prepareFlatTestLine(audioWorld, noteAudioY);
        audioWorld.setBlockState(0, noteAudioY, 0, leverState(true));
        audioWorld.setBlockState(1, noteAudioY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        audioWorld.setBlockState(2, noteAudioY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:note_block")));

        ecs::GameplayRegistry registry;
        ecs::GameplayServices services;
        services.world = &audioWorld;
        ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 200};
        ecs::RedstoneSystem redstoneSystem;
        ecs::RedstoneDeviceActionSystem deviceActionSystem;
        redstoneSystem.update(ctx);
        deviceActionSystem.update(ctx);

        auto& audioEvents = ecs::ensureAudioEventBus(registry);
        if (audioEvents.size() != 1 ||
            audioEvents.peek().front().clipName != "block.note_block.harp") {
            return fail("note_block should emit one harp sound on a rising redstone edge");
        }

        ctx.tickIndex = 202;
        audioWorld.redstoneUpdateQueue().enqueue(glm::ivec3(2, noteAudioY, 0));
        redstoneSystem.update(ctx);
        deviceActionSystem.update(ctx);
        if (audioEvents.size() != 1) {
            return fail("note_block should not emit another sound while power remains high");
        }

        audioWorld.setBlockState(0, noteAudioY, 0, leverState(false));
        ctx.tickIndex = 204;
        redstoneSystem.update(ctx);
        deviceActionSystem.update(ctx);
        if (audioEvents.size() != 1 ||
            noteBlockPowered(audioWorld, 2, noteAudioY, 0)) {
            return fail("note_block falling edge should reset state without emitting sound");
        }

        audioWorld.setBlockState(0, noteAudioY, 0, leverState(true));
        ctx.tickIndex = 206;
        redstoneSystem.update(ctx);
        deviceActionSystem.update(ctx);
        if (audioEvents.size() != 2) {
            return fail("note_block should emit again after power falls and rises");
        }
    }

    {
        World edgeWorld;
        edgeWorld.init(20260626);
        loadOriginChunks(edgeWorld);

        const int dispenserY = 68;
        prepareFlatTestLine(edgeWorld, dispenserY);
        edgeWorld.setBlockState(0, dispenserY, 0, leverState(true));
        edgeWorld.setBlockState(1, dispenserY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        edgeWorld.setBlockState(2, dispenserY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:dispenser")));

        ecs::GameplayRegistry registry;
        ecs::RedstoneSystem::processWorld(edgeWorld, 210, registry);
        auto& activationEvents = ecs::ensureRedstoneDeviceActivationEventBus(registry);
        if (!powered(edgeWorld, 2, dispenserY, 0) ||
            activationEvents.size() != 1 ||
            activationEvents.peek().front().blockId != BlockRegistry::requireIdByName("minecraft:dispenser") ||
            activationEvents.peek().front().position != glm::ivec3(2, dispenserY, 0) ||
            activationEvents.peek().front().redstoneTick != 210) {
            return fail("dispenser should store powered=true and emit one rising-edge redstone event");
        }

        edgeWorld.redstoneUpdateQueue().enqueue(glm::ivec3(2, dispenserY, 0));
        ecs::RedstoneSystem::processWorld(edgeWorld, 211, registry);
        if (!powered(edgeWorld, 2, dispenserY, 0) ||
            activationEvents.size() != 1) {
            return fail("dispenser should not emit another event while power remains high");
        }

        edgeWorld.setBlockState(0, dispenserY, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(edgeWorld, 212, registry);
        if (powered(edgeWorld, 2, dispenserY, 0) ||
            activationEvents.size() != 1) {
            return fail("dispenser falling edge should reset powered state without emitting an event");
        }

        edgeWorld.setBlockState(0, dispenserY, 0, leverState(true));
        ecs::RedstoneSystem::processWorld(edgeWorld, 213, registry);
        if (!powered(edgeWorld, 2, dispenserY, 0) ||
            activationEvents.size() != 2 ||
            activationEvents.peek().back().blockId != BlockRegistry::requireIdByName("minecraft:dispenser") ||
            activationEvents.peek().back().redstoneTick != 213) {
            return fail("dispenser should emit again after power falls and rises");
        }

        activationEvents.clear();

        const int dropperY = 60;
        prepareFlatTestLine(edgeWorld, dropperY);
        edgeWorld.setBlockState(0, dropperY, 0, leverState(true));
        edgeWorld.setBlockState(1, dropperY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
        edgeWorld.setBlockState(2, dropperY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:dropper")));

        ecs::RedstoneSystem::processWorld(edgeWorld, 220, registry);
        if (!powered(edgeWorld, 2, dropperY, 0) ||
            activationEvents.size() != 1 ||
            activationEvents.peek().front().blockId != BlockRegistry::requireIdByName("minecraft:dropper") ||
            activationEvents.peek().front().position != glm::ivec3(2, dropperY, 0) ||
            activationEvents.peek().front().redstoneTick != 220) {
            return fail("dropper should store powered=true and emit one rising-edge redstone event");
        }
    }

    std::cout << "[redstone_basic_test] PASS\n";
    return EXIT_SUCCESS;
}
