#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/entity/EntityFactory.h"
#include "../src/ecs/systems/world/BlockSupportSystem.h"
#include "../src/ecs/systems/world/FallingBlockSpawnSystem.h"
#include "../src/ecs/systems/world/FallingBlockTickSystem.h"
#include "../src/ecs/systems/world/FarmlandMoistureSystem.h"
#include "../src/ecs/systems/world/FluidTickSystem.h"
#include "../src/ecs/systems/world/PressurePlateSystem.h"
#include "../src/ecs/systems/world/RandomTickSystem.h"
#include "../src/ecs/util/FallingBlockEventBuffer.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/fluid/FluidState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[client_server_authority_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadSpawnChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

ecs::GameplayServices makeClientServices(World& world) {
    ecs::GameplayServices services;
    services.world = &world;
    services.worldView = &world;
    services.gameClient = reinterpret_cast<client::GameClient*>(static_cast<uintptr_t>(1));
    return services;
}

uint16_t moistureValue(const StateID state) {
    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moisture == BlockStateRegistry::INVALID_INDEX) {
        return BlockStateRegistry::INVALID_INDEX;
    }
    return BlockStateRegistry::getPropertyIndex(state, moisture);
}

void fillSubChunk(World& world, const glm::ivec3& origin, const StateID state) {
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                world.setBlockState(origin.x + x, origin.y + y, origin.z + z, state);
            }
        }
    }
}

size_t countStatesWithPropertyValue(World& world,
                                    const glm::ivec3& origin,
                                    const uint16_t property,
                                    const uint16_t value) {
    size_t count = 0;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const StateID state = world.getBlockState(origin.x + x, origin.y + y, origin.z + z);
                if (BlockStateRegistry::getPropertyIndex(state, property) == value) {
                    ++count;
                }
            }
        }
    }
    return count;
}

StateID poweredPlateState(const bool powered) {
    return BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:oak_pressure_plate"),
        PropIndices::POWERED,
        powered ? PropIndices::POWERED_TRUE : PropIndices::POWERED_FALSE);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);

    World world;
    world.init(20260625);
    loadSpawnChunks(world);

    ecs::GameplayRegistry registry;
    ecs::GameplayServices clientServices = makeClientServices(world);
    ecs::SystemContext clientCtx{registry, clientServices, 0.0f, 200};

    world.fluidSystem().scheduleBlockTick(glm::ivec3(0, 96, 0), 1);
    const size_t pendingFluidTicks = world.fluidSystem().pendingTickCount();
    ecs::FluidTickSystem fluidTickSystem;
    fluidTickSystem.update(clientCtx);
    if (world.fluidSystem().pendingTickCount() != pendingFluidTicks) {
        return fail("client fluid tick must not consume authoritative scheduled fluid updates");
    }

    const BlockID farmland = BlockRegistry::findByName("farmland");
    if (farmland == RUNTIME_ID_NULL) {
        return fail("farmland block should be registered");
    }
    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    const uint16_t moisture0 = BlockStateRegistry::getPropertyValueIndex(moisture, "0");
    const uint16_t moisture1 = BlockStateRegistry::getPropertyValueIndex(moisture, "1");
    const uint16_t moisture2 = BlockStateRegistry::getPropertyValueIndex(moisture, "2");
    const uint16_t moisture7 = BlockStateRegistry::getPropertyValueIndex(moisture, "7");
    const StateID dryFarmland = BlockStateRegistry::getDefaultState(farmland);

    const glm::ivec3 hydratedPos(0, 122, 0);
    world.setBlockState(hydratedPos.x, hydratedPos.y, hydratedPos.z, dryFarmland);
    world.setFluidState(1, 122, 0, FluidState::makeWater(0, false));
    ecs::FarmlandMoistureSystem farmlandMoistureSystem;
    farmlandMoistureSystem.update(clientCtx);
    if (moistureValue(world.getBlockState(hydratedPos.x, hydratedPos.y, hydratedPos.z)) != moisture0) {
        return fail("client farmland moisture tick must not hydrate authoritative farmland");
    }
    if (ecs::FarmlandMoistureSystem::hydrateLoadedFarmland(world) == 0 ||
        moistureValue(world.getBlockState(hydratedPos.x, hydratedPos.y, hydratedPos.z)) != moisture7) {
        return fail("server farmland moisture tick should hydrate farmland");
    }

    const glm::ivec3 randomTickOrigin(0, 128, 0);
    const StateID moistFarmland = BlockStateRegistry::withProperty(dryFarmland, moisture, moisture2);
    fillSubChunk(world, randomTickOrigin, moistFarmland);
    ecs::RandomTickSystem randomTickSystem;
    randomTickSystem.update(clientCtx);
    if (countStatesWithPropertyValue(world, randomTickOrigin, moisture, moisture1) != 0) {
        return fail("client random tick must not mutate authoritative block states");
    }
    if (ecs::RandomTickSystem::processWorld(world, 201, 16) == 0 ||
        countStatesWithPropertyValue(world, randomTickOrigin, moisture, moisture1) == 0) {
        return fail("server random tick should mutate random-tick block states");
    }

    const glm::ivec3 platePos(2, 122, 0);
    world.setBlockState(platePos.x, platePos.y, platePos.z, poweredPlateState(false));
    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);
    ecs::PhysicsBodyComponent body;
    body.body.position = glm::vec3(platePos) + glm::vec3(0.5f, 0.0f, 0.5f);
    body.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
    body.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
    registry.emplace<ecs::PhysicsBodyComponent>(player, body);

    ecs::PressurePlateSystem pressurePlateSystem;
    pressurePlateSystem.update(clientCtx);
    if (world.getBlockState(platePos.x, platePos.y, platePos.z) != poweredPlateState(false)) {
        return fail("client pressure plate tick must not power authoritative plates");
    }
    if (ecs::PressurePlateSystem::processWorldEntities(world, registry) == 0 ||
        world.getBlockState(platePos.x, platePos.y, platePos.z) != poweredPlateState(true)) {
        return fail("server pressure plate tick should power plates from entity contacts");
    }

    world.neighborUpdateQueue().clear();
    const glm::ivec3 sandPos(0, 140, 0);
    world.setBlock(sandPos.x, sandPos.y - 1, sandPos.z, RUNTIME_ID_NULL);
    world.setBlock(sandPos.x, sandPos.y, sandPos.z, BlockRegistry::requireIdByName("minecraft:sand"));
    const size_t queuedSupportUpdates = world.neighborUpdateQueue().size();
    ecs::BlockSupportSystem blockSupportSystem;
    blockSupportSystem.update(clientCtx);
    if (world.neighborUpdateQueue().size() != queuedSupportUpdates ||
        world.getBlock(sandPos.x, sandPos.y, sandPos.z) != BlockRegistry::requireIdByName("minecraft:sand")) {
        return fail("client block support tick must not drain authoritative support updates");
    }

    if (ecs::BlockSupportSystem::processWorldQueue(world, registry, 1024) == 0 ||
        world.getBlock(sandPos.x, sandPos.y, sandPos.z) != RUNTIME_ID_NULL) {
        return fail("server block support tick should clear unsupported gravity blocks");
    }
    if (!registry.ctxHas<ecs::FallingBlockSpawnEventBus>() ||
        registry.ctxGet<ecs::FallingBlockSpawnEventBus>().size() == 0) {
        return fail("server block support tick should emit a falling block event");
    }

    ecs::FallingBlockSpawnSystem fallingBlockSpawnSystem;
    fallingBlockSpawnSystem.update(clientCtx);
    if (registry.ctxGet<ecs::FallingBlockSpawnEventBus>().size() == 0) {
        return fail("client falling block spawn tick must not consume server falling block events");
    }
    if (ecs::FallingBlockSpawnSystem::processEvents(registry) == 0) {
        return fail("server falling block spawn should create falling block entities");
    }

    auto fallingView = registry.registry().view<ecs::FallingBlockTag, ecs::FallingBlockComponent>();
    if (fallingView.begin() == fallingView.end()) {
        return fail("falling block entity should exist after server spawn processing");
    }
    const entt::entity fallingEntity = *fallingView.begin();
    const glm::ivec3 beforeClientTick = registry.registry().get<ecs::FallingBlockComponent>(fallingEntity).gridPosition;
    ecs::FallingBlockTickSystem fallingBlockTickSystem;
    fallingBlockTickSystem.update(clientCtx);
    if (registry.registry().get<ecs::FallingBlockComponent>(fallingEntity).gridPosition != beforeClientTick) {
        return fail("client falling block tick must not advance authoritative falling blocks");
    }
    ecs::FallingBlockTickSystem::tickWorld(world, registry);
    if (registry.registry().valid(fallingEntity) &&
        registry.registry().get<ecs::FallingBlockComponent>(fallingEntity).gridPosition == beforeClientTick) {
        return fail("server falling block tick should advance falling blocks");
    }

    std::cout << "[client_server_authority_test] PASS\n";
    return EXIT_SUCCESS;
}
