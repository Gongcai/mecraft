#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PistonBlock.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_piston_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void preparePistonArea(World& world, const int y) {
    for (int x = -1; x <= 18; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.setBlock(x, y - 1, z, BlockIds::STONE);
            world.setBlock(x, y, z, BlockIds::AIR);
            world.setBlock(x, y + 1, z, BlockIds::AIR);
        }
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

StateID pistonState(const BlockID blockId, const bool extended) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, PropIndices::FACING_EAST},
            {PropIndices::EXTENDED, extended ? PropIndices::EXTENDED_TRUE : PropIndices::EXTENDED_FALSE}
        });
}

bool pistonExtended(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::EXTENDED) == PropIndices::EXTENDED_TRUE;
}

bool matchingPistonHead(const World& world,
                        const int x,
                        const int y,
                        const int z,
                        const uint16_t expectedType) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getBlockId(state) == BlockIds::PISTON_HEAD &&
           BlockStateRegistry::getPropertyIndex(state, PropIndices::FACING) == PropIndices::FACING_EAST &&
           BlockStateRegistry::getPropertyIndex(state, PropIndices::TYPE) == expectedType;
}

bool blockIs(const World& world, const int x, const int y, const int z, const BlockID blockId) {
    return BlockStateRegistry::getBlockId(world.getBlockState(x, y, z)) == blockId;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    const BlockID obsidianBlockId = BlockRegistry::findByName("obsidian");
    if (obsidianBlockId == BlockIds::AIR) {
        return fail("obsidian should be registered for piston immovable block validation");
    }

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    {
        const int y = 96;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::PISTON, false));

        ecs::RedstoneSystem::processWorld(world, 0);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL)) {
            return fail("powered piston should extend into empty space and place a normal piston head");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 1);
        if (pistonExtended(world, 1, y, 0) ||
            world.getBlockState(2, y, 0) != BlockIds::AIR) {
            return fail("unpowered piston should retract and remove its piston head");
        }
    }

    {
        const int y = 80;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::STICKY_PISTON, false));

        ecs::RedstoneSystem::processWorld(world, 2);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_STICKY)) {
            return fail("powered sticky piston should place a sticky piston head");
        }
    }

    {
        const int y = 76;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::PISTON, false));

        ecs::RedstoneSystem::processWorld(world, 3);
        std::vector<glm::ivec3> removedPositions;
        const BlockID removedBlock = PistonBlockLogic::removePistonAssembly(
            world,
            glm::ivec3(2, y, 0),
            &removedPositions);
        if (removedBlock != BlockIds::PISTON ||
            removedPositions.size() != 2 ||
            (!removedPositions.empty() && removedPositions.front() != glm::ivec3(2, y, 0)) ||
            world.getBlockState(1, y, 0) != BlockIds::AIR ||
            world.getBlockState(2, y, 0) != BlockIds::AIR) {
            return fail("breaking an extended piston head should remove the piston base as one assembly");
        }
    }

    {
        const int y = 74;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::STICKY_PISTON, false));

        ecs::RedstoneSystem::processWorld(world, 4);
        std::vector<glm::ivec3> removedPositions;
        const BlockID removedBlock = PistonBlockLogic::removePistonAssembly(
            world,
            glm::ivec3(1, y, 0),
            &removedPositions);
        if (removedBlock != BlockIds::STICKY_PISTON ||
            removedPositions.size() != 2 ||
            (!removedPositions.empty() && removedPositions.front() != glm::ivec3(1, y, 0)) ||
            world.getBlockState(1, y, 0) != BlockIds::AIR ||
            world.getBlockState(2, y, 0) != BlockIds::AIR) {
            return fail("breaking an extended piston base should remove the piston head as one assembly");
        }
    }

    {
        const int y = 72;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::PISTON, false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockIds::STONE));
        world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockIds::DIRT));

        ecs::RedstoneSystem::processWorld(world, 5);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL) ||
            !blockIs(world, 3, y, 0, BlockIds::STONE) ||
            !blockIs(world, 4, y, 0, BlockIds::DIRT)) {
            return fail("powered piston should push a movable block chain forward before placing its head");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 6);
        if (pistonExtended(world, 1, y, 0) ||
            world.getBlockState(2, y, 0) != BlockIds::AIR ||
            !blockIs(world, 3, y, 0, BlockIds::STONE) ||
            !blockIs(world, 4, y, 0, BlockIds::DIRT)) {
            return fail("normal piston should retract its head without pulling pushed blocks back");
        }
    }

    {
        const int y = 64;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::STICKY_PISTON, false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockIds::STONE));

        ecs::RedstoneSystem::processWorld(world, 7);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_STICKY) ||
            !blockIs(world, 3, y, 0, BlockIds::STONE)) {
            return fail("powered sticky piston should push a movable front block");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 8);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, BlockIds::STONE) ||
            world.getBlockState(3, y, 0) != BlockIds::AIR) {
            return fail("sticky piston should pull the front block back when retracting");
        }
    }

    {
        const int y = 56;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::PISTON, false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(obsidianBlockId));

        ecs::RedstoneSystem::processWorld(world, 9);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, obsidianBlockId) ||
            world.getBlockState(3, y, 0) != BlockIds::AIR) {
            return fail("piston should not extend when an immovable block is directly in front");
        }
    }

    {
        const int y = 48;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockIds::PISTON, false));
        for (int x = 2; x <= 14; ++x) {
            world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockIds::STONE));
        }

        ecs::RedstoneSystem::processWorld(world, 10);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, BlockIds::STONE) ||
            !blockIs(world, 14, y, 0, BlockIds::STONE) ||
            world.getBlockState(15, y, 0) != BlockIds::AIR) {
            return fail("piston should not push more than twelve movable blocks");
        }
    }

    std::cout << "[redstone_piston_test] PASS\n";
    return EXIT_SUCCESS;
}
