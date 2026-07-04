#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/world/PressurePlateSystem.h"
#include "../src/ecs/systems/world/RedstoneSystem.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/Block.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_pressure_plate_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f, 0.0f, 0.0f));
    }
}

void prepareFlatTestLine(World& world, const int y) {
    for (int x = -1; x <= 4; ++x) {
        world.setBlock(x, y - 1, 0, BlockRegistry::requireIdByName("minecraft:stone"));
        world.setBlock(x, y, 0, RUNTIME_ID_NULL);
        world.setBlock(x, y + 1, 0, RUNTIME_ID_NULL);
    }
}

void processRedstoneTicks(World& world, uint64_t& tick, const int count) {
    for (int i = 0; i < count; ++i) {
        ecs::RedstoneSystem::processWorld(world, tick);
        ++tick;
    }
}

BlockStateId pressurePlateState(const BlockID blockId, const bool powered) {
    return BlockStateRegistry::getState(
        blockId,
        std::vector<std::pair<uint16_t, uint16_t>>{
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

    const BlockStateId state = world.getBlockState(x, y, z);
    const uint16_t value = BlockStateRegistry::getPropertyIndex(state, PropIndices::POWER);
    for (uint8_t power = 0; power < kPowerValues.size(); ++power) {
        if (value == kPowerValues[power]) {
            return power;
        }
    }

    std::cerr << "[redstone_pressure_plate_test] FAIL: Wire state does not contain a valid power value\n";
    std::abort();
}

bool powered(const World& world, const int x, const int y, const int z) {
    const BlockStateId state = world.getBlockState(x, y, z);
    return BlockStateRegistry::getPropertyIndex(state, PropIndices::POWERED) == PropIndices::POWERED_TRUE;
}

entt::entity createStandingPlayer(ecs::GameplayRegistry& registry, const glm::vec3& footPosition) {
    auto& raw = registry.registry();
    const entt::entity player = raw.create();
    raw.emplace<ecs::LocalPlayerTag>(player);
    raw.emplace<ecs::TransformComponent>(player, footPosition, 1.62f);
    auto& physics = raw.emplace<ecs::PhysicsBodyComponent>(player);
    physics.body.position = footPosition;
    physics.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
    physics.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
    return player;
}

entt::entity createDrop(ecs::GameplayRegistry& registry, const glm::vec3& centerPosition) {
    auto& raw = registry.registry();
    const entt::entity drop = raw.create();
    raw.emplace<ecs::DropItemTag>(drop);
    raw.emplace<ecs::TransformComponent>(drop, centerPosition, 0.0f);
    raw.emplace<ecs::BoundsComponent>(drop, glm::vec3(0.175f));
    raw.emplace<ecs::ItemComponent>(drop, ItemRegistry::requireIdByName("minecraft:coal"), 1u);
    return drop;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    World world;
    world.init(20260625);
    loadOriginChunks(world);

    ecs::GameplayRegistry registry;
    uint64_t redstoneTick = 0;

    const int stoneY = 96;
    prepareFlatTestLine(world, stoneY);
    world.setBlockState(0, stoneY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), false));
    world.setBlockState(1, stoneY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));

    const entt::entity player = createStandingPlayer(
        registry,
        glm::vec3(0.5f, static_cast<float>(stoneY) + (1.0f / 16.0f), 0.5f));
    ecs::PressurePlateSystem::processWorldEntities(world, registry);
    processRedstoneTicks(world, redstoneTick, 2);

    if (!powered(world, 0, stoneY, 0) ||
        wirePower(world, 1, stoneY, 0) != 15) {
        return fail("standing player should power a stone pressure plate and adjacent redstone wire");
    }

    auto& playerTransform = registry.registry().get<ecs::TransformComponent>(player);
    auto& playerPhysics = registry.registry().get<ecs::PhysicsBodyComponent>(player);
    playerTransform.position = glm::vec3(3.5f, static_cast<float>(stoneY) + (1.0f / 16.0f), 0.5f);
    playerPhysics.body.position = playerTransform.position;
    ecs::PressurePlateSystem::processWorldEntities(world, registry);
    processRedstoneTicks(world, redstoneTick, 2);

    if (powered(world, 0, stoneY, 0) ||
        wirePower(world, 1, stoneY, 0) != 0) {
        return fail("stone pressure plate should release when the player leaves");
    }

    const int itemStoneY = 80;
    prepareFlatTestLine(world, itemStoneY);
    world.setBlockState(0, itemStoneY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:stone_pressure_plate"), false));
    const entt::entity stoneDrop = createDrop(
        registry,
        glm::vec3(0.5f, static_cast<float>(itemStoneY) + (1.0f / 16.0f) + 0.175f, 0.5f));
    ecs::PressurePlateSystem::processWorldEntities(world, registry);

    if (powered(world, 0, itemStoneY, 0)) {
        return fail("stone pressure plate should ignore item drops");
    }

    registry.registry().destroy(stoneDrop);

    const int oakY = 64;
    prepareFlatTestLine(world, oakY);
    world.setBlockState(0, oakY, 0, pressurePlateState(BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"), false));
    world.setBlockState(1, oakY, 0, BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:redstone_wire")));
    createDrop(
        registry,
        glm::vec3(0.5f, static_cast<float>(oakY) + (1.0f / 16.0f) + 0.175f, 0.5f));
    ecs::PressurePlateSystem::processWorldEntities(world, registry);
    processRedstoneTicks(world, redstoneTick, 2);

    if (!powered(world, 0, oakY, 0) ||
        wirePower(world, 1, oakY, 0) != 15) {
        return fail("oak pressure plate should respond to item drops and power redstone wire");
    }

    std::cout << "[redstone_pressure_plate_test] PASS\n";
    return EXIT_SUCCESS;
}
