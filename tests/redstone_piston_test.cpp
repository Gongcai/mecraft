#include <cstdlib>
#include <cmath>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/world/MovingBlockSystem.h"
#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockCollision.h"
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
    for (int x = -3; x <= 18; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.setBlock(x, y - 1, z, BlockRegistry::requireIdByName("minecraft:stone"));
            world.setBlock(x, y, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 1, z, RUNTIME_ID_NULL);
            world.setBlock(x, y + 2, z, RUNTIME_ID_NULL);
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

StateID pistonState(const BlockID blockId, const uint16_t facing, const bool extended) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::FACING, facing},
            {PropIndices::EXTENDED, extended ? PropIndices::EXTENDED_TRUE : PropIndices::EXTENDED_FALSE}
        });
}

StateID pistonState(const BlockID blockId, const bool extended) {
    return pistonState(blockId, PropIndices::FACING_EAST, extended);
}

bool pistonExtended(const World& world, const int x, const int y, const int z) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::EXTENDED) == PropIndices::EXTENDED_TRUE;
}

bool matchingPistonHead(const World& world,
                        const int x,
                        const int y,
                        const int z,
                        const uint16_t expectedFacing,
                        const uint16_t expectedType) {
    const StateID state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getBlockId(state) == BlockRegistry::requireIdByName("minecraft:piston_head") &&
           BlockStateRegistry::getPropertyIndex(state, PropIndices::FACING) == expectedFacing &&
           BlockStateRegistry::getPropertyIndex(state, PropIndices::TYPE) == expectedType;
}

bool matchingPistonHead(const World& world,
                        const int x,
                        const int y,
                        const int z,
                        const uint16_t expectedType) {
    return matchingPistonHead(world, x, y, z, PropIndices::FACING_EAST, expectedType);
}

bool blockIs(const World& world, const int x, const int y, const int z, const BlockID blockId) {
    return BlockStateRegistry::getBlockId(world.getBlockState(x, y, z)) == blockId;
}

entt::entity createPhysicsPlayer(ecs::GameplayRegistry& registry, const glm::vec3& feetPosition) {
    entt::registry& raw = registry.registry();
    const entt::entity player = raw.create();
    auto& transform = raw.emplace<ecs::TransformComponent>(player);
    transform.position = feetPosition;
    transform.eyeHeight = 1.62f;
    auto& physics = raw.emplace<ecs::PhysicsBodyComponent>(player);
    physics.body.position = feetPosition;
    physics.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
    physics.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
    return player;
}

bool physicsBodyOverlapsWorld(const World& world, const ecs::PhysicsBodyComponent& physics) {
    const glm::vec3 center = physics.body.position + physics.body.colliderOffset;
    const glm::vec3 queryMin = center - physics.body.halfExtents;
    const glm::vec3 queryMax = center + physics.body.halfExtents;
    const int minX = static_cast<int>(std::floor(queryMin.x));
    const int maxX = static_cast<int>(std::floor(queryMax.x - 0.001f));
    const int minY = static_cast<int>(std::floor(queryMin.y));
    const int maxY = static_cast<int>(std::floor(queryMax.y - 0.001f));
    const int minZ = static_cast<int>(std::floor(queryMin.z));
    const int maxZ = static_cast<int>(std::floor(queryMax.z - 0.001f));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                const StateID state = world.getBlockState(x, y, z);
                if (BlockCollision::intersects(state, glm::ivec3(x, y, z), queryMin, queryMax)) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::size_t countMovingBlocks(const ecs::GameplayRegistry& registry) {
    std::size_t count = 0;
    const auto view = registry.view<ecs::MovingBlockTag>();
    for (const entt::entity entity : view) {
        static_cast<void>(entity);
        ++count;
    }
    return count;
}

void advanceMovingBlocks(World& world, ecs::GameplayRegistry& registry) {
    for (int i = 0; i < 8; ++i) {
        ecs::MovingBlockSystem::processWorld(world, registry, 1.0f / 60.0f);
    }
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    const BlockID obsidianBlockId = BlockRegistry::findByName("obsidian");
    if (obsidianBlockId == RUNTIME_ID_NULL) {
        return fail("obsidian should be registered for piston immovable block validation");
    }

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    {
        const int y = 96;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));

        ecs::RedstoneSystem::processWorld(world, 0);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL)) {
            return fail("powered piston should extend into empty space and place a normal piston head");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 1);
        if (pistonExtended(world, 1, y, 0) ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL) {
            return fail("unpowered piston should retract and remove its piston head");
        }
    }

    {
        const int y = 80;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:sticky_piston"), false));

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
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));

        ecs::RedstoneSystem::processWorld(world, 3);
        std::vector<glm::ivec3> removedPositions;
        const BlockID removedBlock = PistonBlockLogic::removePistonAssembly(
            world,
            glm::ivec3(2, y, 0),
            &removedPositions);
        if (removedBlock != BlockRegistry::requireIdByName("minecraft:piston") ||
            removedPositions.size() != 2 ||
            (!removedPositions.empty() && removedPositions.front() != glm::ivec3(2, y, 0)) ||
            world.getBlockState(1, y, 0) != RUNTIME_ID_NULL ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL) {
            return fail("breaking an extended piston head should remove the piston base as one assembly");
        }
    }

    {
        const int y = 74;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:sticky_piston"), false));

        ecs::RedstoneSystem::processWorld(world, 4);
        std::vector<glm::ivec3> removedPositions;
        const BlockID removedBlock = PistonBlockLogic::removePistonAssembly(
            world,
            glm::ivec3(1, y, 0),
            &removedPositions);
        if (removedBlock != BlockRegistry::requireIdByName("minecraft:sticky_piston") ||
            removedPositions.size() != 2 ||
            (!removedPositions.empty() && removedPositions.front() != glm::ivec3(1, y, 0)) ||
            world.getBlockState(1, y, 0) != RUNTIME_ID_NULL ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL) {
            return fail("breaking an extended piston base should remove the piston head as one assembly");
        }
    }

    {
        const int y = 72;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:dirt")));

        ecs::RedstoneSystem::processWorld(world, 5);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL) ||
            !blockIs(world, 3, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 4, y, 0, BlockRegistry::requireIdByName("minecraft:dirt"))) {
            return fail("powered piston should push a movable block chain forward before placing its head");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 6);
        if (pistonExtended(world, 1, y, 0) ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL ||
            !blockIs(world, 3, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 4, y, 0, BlockRegistry::requireIdByName("minecraft:dirt"))) {
            return fail("normal piston should retract its head without pulling pushed blocks back");
        }
    }

    {
        const int y = 64;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:sticky_piston"), false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));

        ecs::RedstoneSystem::processWorld(world, 7);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_STICKY) ||
            !blockIs(world, 3, y, 0, BlockRegistry::requireIdByName("minecraft:stone"))) {
            return fail("powered sticky piston should push a movable front block");
        }

        world.setBlockState(0, y, 0, leverState(false));
        ecs::RedstoneSystem::processWorld(world, 8);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            world.getBlockState(3, y, 0) != RUNTIME_ID_NULL) {
            return fail("sticky piston should pull the front block back when retracting");
        }
    }

    {
        const int y = 56;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(obsidianBlockId));

        ecs::RedstoneSystem::processWorld(world, 9);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, obsidianBlockId) ||
            world.getBlockState(3, y, 0) != RUNTIME_ID_NULL) {
            return fail("piston should not extend when an immovable block is directly in front");
        }
    }

    {
        const int y = 48;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));
        for (int x = 2; x <= 14; ++x) {
            world.setBlockState(x, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        }

        ecs::RedstoneSystem::processWorld(world, 10);
        if (pistonExtended(world, 1, y, 0) ||
            !blockIs(world, 2, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 14, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            world.getBlockState(15, y, 0) != RUNTIME_ID_NULL) {
            return fail("piston should not push more than twelve movable blocks");
        }
    }

    {
        const int y = 40;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));

        ecs::GameplayRegistry registry;
        const entt::entity player = createPhysicsPlayer(
            registry,
            glm::vec3(2.5f, static_cast<float>(y), 0.5f));

        ecs::RedstoneSystem::processWorld(world, 11, registry);
        if (countMovingBlocks(registry) != 1 ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL) {
            return fail("extending piston should create a moving head before final placement");
        }
        advanceMovingBlocks(world, registry);
        const auto& transform = registry.get<ecs::TransformComponent>(player);
        const auto& physics = registry.get<ecs::PhysicsBodyComponent>(player);
        if (!pistonExtended(world, 1, y, 0) ||
            !matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL) ||
            transform.position.x <= 3.29f ||
            physicsBodyOverlapsWorld(world, physics)) {
            return fail("extending piston head should push an intersecting player out of the head block");
        }
    }

    {
        const int y = 36;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));

        ecs::GameplayRegistry registry;
        const entt::entity player = createPhysicsPlayer(
            registry,
            glm::vec3(3.5f, static_cast<float>(y), 0.5f));

        ecs::RedstoneSystem::processWorld(world, 12, registry);
        if (countMovingBlocks(registry) != 2 ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL ||
            world.getBlockState(3, y, 0) != RUNTIME_ID_NULL) {
            return fail("piston-pushed block should move as entities before final placement");
        }
        advanceMovingBlocks(world, registry);
        const auto& transform = registry.get<ecs::TransformComponent>(player);
        const auto& physics = registry.get<ecs::PhysicsBodyComponent>(player);
        if (!blockIs(world, 3, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            transform.position.x <= 4.29f ||
            physicsBodyOverlapsWorld(world, physics)) {
            return fail("piston-pushed block should push an intersecting player out of the target block");
        }
    }

    {
        const int y = 28;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));

        ecs::GameplayRegistry registry;
        const entt::entity player = createPhysicsPlayer(
            registry,
            glm::vec3(2.1f, static_cast<float>(y), 0.05f));

        ecs::RedstoneSystem::processWorld(world, 14, registry);
        if (countMovingBlocks(registry) != 1 ||
            world.getBlockState(2, y, 0) != RUNTIME_ID_NULL) {
            return fail("extending piston head should be animated through its swept path");
        }
        advanceMovingBlocks(world, registry);
        const auto& transform = registry.get<ecs::TransformComponent>(player);
        const auto& physics = registry.get<ecs::PhysicsBodyComponent>(player);
        if (!matchingPistonHead(world, 2, y, 0, PropIndices::TYPE_NORMAL) ||
            transform.position.x <= 3.29f ||
            physicsBodyOverlapsWorld(world, physics)) {
            return fail("extending piston head should push a player from the full swept head path");
        }
    }

    {
        const int y = 24;
        preparePistonArea(world, y);
        world.setBlockState(0, y, 0, leverState(true));
        world.setBlockState(1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), false));
        world.setBlockState(2, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));

        ecs::GameplayRegistry registry;
        const entt::entity player = createPhysicsPlayer(
            registry,
            glm::vec3(2.5f, static_cast<float>(y + 1), 0.5f));

        ecs::RedstoneSystem::processWorld(world, 15, registry);
        if (countMovingBlocks(registry) != 2) {
            return fail("horizontal piston-pushed block should create moving block entities");
        }
        advanceMovingBlocks(world, registry);
        const auto& transform = registry.get<ecs::TransformComponent>(player);
        const auto& physics = registry.get<ecs::PhysicsBodyComponent>(player);
        if (!blockIs(world, 3, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            transform.position.x <= 3.29f ||
            physicsBodyOverlapsWorld(world, physics)) {
            return fail("horizontal piston-pushed block should carry a player standing on it");
        }
    }

    {
        const int y = 32;
        preparePistonArea(world, y);
        world.setBlockState(-2, y, 0, leverState(true));
        world.setBlockState(-1, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), PropIndices::FACING_EAST, false));
        world.setBlockState(0, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(-2, y + 1, 0, leverState(true));
        world.setBlockState(-1, y + 1, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), PropIndices::FACING_EAST, false));
        world.setBlockState(0, y + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(5, y, 0, leverState(true));
        world.setBlockState(4, y, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), PropIndices::FACING_WEST, false));
        world.setBlockState(3, y, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));
        world.setBlockState(5, y + 1, 0, leverState(true));
        world.setBlockState(4, y + 1, 0, pistonState(BlockRegistry::requireIdByName("minecraft:piston"), PropIndices::FACING_WEST, false));
        world.setBlockState(3, y + 1, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:stone")));

        ecs::GameplayRegistry registry;
        const entt::entity player = createPhysicsPlayer(
            registry,
            glm::vec3(1.5f, static_cast<float>(y), 0.5f));

        ecs::RedstoneSystem::processWorld(world, 16, registry);
        if (countMovingBlocks(registry) != 8) {
            return fail("two-high opposing piston door should animate every pushed head and block");
        }
        advanceMovingBlocks(world, registry);
        const auto& transform = registry.get<ecs::TransformComponent>(player);
        const auto& physics = registry.get<ecs::PhysicsBodyComponent>(player);
        const bool pushedOutsideDoorDepth = transform.position.z <= -0.29f || transform.position.z >= 1.29f ||
                                            transform.position.x <= 0.69f || transform.position.x >= 3.31f;
        if (!matchingPistonHead(world, 0, y, 0, PropIndices::FACING_EAST, PropIndices::TYPE_NORMAL) ||
            !matchingPistonHead(world, 0, y + 1, 0, PropIndices::FACING_EAST, PropIndices::TYPE_NORMAL) ||
            !matchingPistonHead(world, 3, y, 0, PropIndices::FACING_WEST, PropIndices::TYPE_NORMAL) ||
            !matchingPistonHead(world, 3, y + 1, 0, PropIndices::FACING_WEST, PropIndices::TYPE_NORMAL) ||
            !blockIs(world, 1, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 1, y + 1, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 2, y, 0, BlockRegistry::requireIdByName("minecraft:stone")) ||
            !blockIs(world, 2, y + 1, 0, BlockRegistry::requireIdByName("minecraft:stone"))) {
            return fail("two-high opposing piston door should close all pushed blocks");
        }
        if (!pushedOutsideDoorDepth) {
            return fail("closing two-high opposing piston door should move the player outside the closed doorway depth");
        }
        if (physicsBodyOverlapsWorld(world, physics)) {
            return fail("closing two-high opposing piston door should not leave the player intersecting block collision");
        }
    }

    std::cout << "[redstone_piston_test] PASS\n";
    return EXIT_SUCCESS;
}
