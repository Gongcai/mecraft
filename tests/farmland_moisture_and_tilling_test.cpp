#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/interaction/SoilTillingSystem.h"
#include "../src/ecs/systems/world/FarmlandMoistureSystem.h"
#include "../src/ecs/systems/world/RandomTickSystem.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockRandomTick.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/fluid/FluidState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[farmland_moisture_and_tilling_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

BlockID blockIdOf(const BlockStateId stateId) {
    return BlockStateRegistry::getBlockId(stateId);
}

void loadSpawnChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

void loadWideSpawnChunks(World& world) {
    world.setRenderDistance(5);
    for (int i = 0; i < 64; ++i) {
        world.updateForInitialLoad(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

uint16_t moistureValue(BlockStateId state) {
    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moisture == BlockStateRegistry::INVALID_INDEX) {
        return BlockStateRegistry::INVALID_INDEX;
    }
    return BlockStateRegistry::getPropertyIndex(state, moisture);
}

void fillSubChunkWithState(World& world, const glm::ivec3& origin, const BlockStateId state) {
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                world.setBlockState(origin.x + x, origin.y + y, origin.z + z, state);
            }
        }
    }
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    const BlockID farmland = BlockRegistry::findByName("farmland");
    if (farmland == RUNTIME_ID_NULL) {
        return fail("farmland block should be registered");
    }

    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    const uint16_t moisture0 = BlockStateRegistry::getPropertyValueIndex(moisture, "0");
    const uint16_t moisture7 = BlockStateRegistry::getPropertyValueIndex(moisture, "7");
    if (moisture == BlockStateRegistry::INVALID_INDEX || moisture0 == BlockStateRegistry::INVALID_INDEX ||
        moisture7 == BlockStateRegistry::INVALID_INDEX) {
        return fail("farmland moisture property should expose values 0 and 7");
    }

    World hydrationWorld;
    hydrationWorld.init(20260624);
    loadSpawnChunks(hydrationWorld);

    const BlockStateId dryFarmland = BlockStateRegistry::getDefaultState(farmland);
    const glm::ivec3 hydratedPos(0, 122, 0);
    const glm::ivec3 dryPos(0, 122, 10);
    hydrationWorld.setBlockState(hydratedPos.x, hydratedPos.y, hydratedPos.z, dryFarmland);
    hydrationWorld.setBlockState(dryPos.x, dryPos.y, dryPos.z, dryFarmland);
    hydrationWorld.setFluidState(4, 122, 0, FluidState::makeWater(0, false));
    hydrationWorld.setFluidState(5, 122, 10, FluidState::makeWater(0, false));

    const size_t hydratedCount = ecs::FarmlandMoistureSystem::hydrateLoadedFarmland(hydrationWorld);
    if (hydratedCount != 1) {
        return fail("only farmland within four horizontal blocks of water should hydrate");
    }
    if (moistureValue(hydrationWorld.getBlockState(hydratedPos.x, hydratedPos.y, hydratedPos.z)) != moisture7) {
        return fail("farmland within hydration range should become fully moist");
    }
    if (moistureValue(hydrationWorld.getBlockState(dryPos.x, dryPos.y, dryPos.z)) != moisture0) {
        return fail("farmland outside hydration range should remain dry");
    }

    World simulationRangeWorld;
    simulationRangeWorld.init(20260626);
    simulationRangeWorld.setSimulationDistance(1);
    loadWideSpawnChunks(simulationRangeWorld);
    const glm::ivec3 outsideSimulationPos(48, 122, 0);
    if (!simulationRangeWorld.isChunkLoadedForBlock(outsideSimulationPos.x, outsideSimulationPos.y,
                                                    outsideSimulationPos.z)) {
        return fail("wide render range should load the chunk used by the simulation range test");
    }
    simulationRangeWorld.setBlockState(outsideSimulationPos.x, outsideSimulationPos.y, outsideSimulationPos.z,
                                       dryFarmland);
    simulationRangeWorld.setFluidState(outsideSimulationPos.x + 1, outsideSimulationPos.y, outsideSimulationPos.z,
                                       FluidState::makeWater(0, false));
    if (ecs::FarmlandMoistureSystem::hydrateLoadedFarmland(simulationRangeWorld) != 0 ||
        moistureValue(simulationRangeWorld.getBlockState(outsideSimulationPos.x, outsideSimulationPos.y,
                                                         outsideSimulationPos.z)) != moisture0) {
        return fail("farmland moisture updates should only process chunks inside simulation distance");
    }

    const BlockID wheat = BlockRegistry::findByName("wheat");
    if (wheat == RUNTIME_ID_NULL) {
        return fail("wheat block should be registered");
    }
    const BlockStateId wheatAge0 = BlockStateRegistry::getDefaultState(wheat);
    const glm::ivec3 outsideRandomTickOrigin(48, 128, 0);
    fillSubChunkWithState(simulationRangeWorld, outsideRandomTickOrigin, wheatAge0);
    if (ecs::RandomTickSystem::processWorld(simulationRangeWorld, 99, 4096) != 0) {
        return fail("random ticks should skip chunks outside simulation distance");
    }

    const glm::ivec3 insideRandomTickOrigin(0, 128, 0);
    fillSubChunkWithState(simulationRangeWorld, insideRandomTickOrigin, wheatAge0);
    if (ecs::RandomTickSystem::processWorld(simulationRangeWorld, 100, 4096) == 0) {
        return fail("random ticks should process random-tick blocks inside simulation distance");
    }

    const BlockStateId moistFarmland = BlockStateRegistry::withProperty(
        dryFarmland, moisture, BlockStateRegistry::getPropertyValueIndex(moisture, "2"));
    hydrationWorld.setBlockState(dryPos.x, dryPos.y, dryPos.z, moistFarmland);
    BlockRandomTickContext dryTickCtx{hydrationWorld, dryPos, moistFarmland, 2, 0};
    if (!BlockRandomTick::dispatch(BlockRegistry::get(farmland).randomTick, dryTickCtx) ||
        moistureValue(hydrationWorld.getBlockState(dryPos.x, dryPos.y, dryPos.z)) !=
            BlockStateRegistry::getPropertyValueIndex(moisture, "1")) {
        return fail("farmland random tick should reduce moisture when no water is nearby");
    }

    const glm::ivec3 revertPos(-8, 122, 0);
    hydrationWorld.setBlockState(revertPos.x, revertPos.y, revertPos.z, dryFarmland);
    BlockRandomTickContext revertTickCtx{hydrationWorld, revertPos, dryFarmland, 3, 0};
    if (!BlockRandomTick::dispatch(BlockRegistry::get(farmland).randomTick, revertTickCtx) ||
        blockIdOf(hydrationWorld.getBlock(revertPos.x, revertPos.y, revertPos.z)) !=
            BlockRegistry::requireIdByName("minecraft:dirt")) {
        return fail("dry farmland random tick should revert to dirt when no crop is above");
    }

    const glm::ivec3 wheatPos(1, 123, 1);
    hydrationWorld.setBlockState(wheatPos.x, wheatPos.y - 1, wheatPos.z, dryFarmland);
    hydrationWorld.setBlockState(wheatPos.x, wheatPos.y, wheatPos.z, wheatAge0);
    BlockRandomTickContext wheatTickCtx{hydrationWorld, wheatPos, wheatAge0, 4, 0};
    const uint16_t age = BlockStateRegistry::getPropertyNameIndex("age");
    if (!BlockRandomTick::dispatch(BlockRegistry::get(wheat).randomTick, wheatTickCtx) ||
        BlockStateRegistry::getPropertyIndex(hydrationWorld.getBlockState(wheatPos.x, wheatPos.y, wheatPos.z), age) !=
            BlockStateRegistry::getPropertyValueIndex(age, "1")) {
        return fail("generic random tick property increment should advance crop age");
    }

    World tillWorld;
    tillWorld.init(20260625);
    loadSpawnChunks(tillWorld);

    const glm::ivec3 dirtPos(0, 122, 0);
    tillWorld.setBlock(dirtPos.x, dirtPos.y, dirtPos.z, BlockRegistry::requireIdByName("minecraft:dirt"));
    tillWorld.setBlockState(dirtPos.x, dirtPos.y + 1, dirtPos.z, NULL_BLOCK_STATE);

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &tillWorld;
    services.worldView = &tillWorld;

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);

    ecs::BlockActionIntentComponent intent;
    intent.wantsPlace = true;
    registry.emplace<ecs::BlockActionIntentComponent>(player, intent);

    ecs::TransformComponent transform;
    transform.position = glm::vec3(dirtPos) + glm::vec3(0.5f, 0.5f, 2.5f);
    registry.emplace<ecs::TransformComponent>(player, transform);

    ecs::InventoryComponent inventoryComponent;
    inventoryComponent.selectedHotbarSlot = 0;
    registry.emplace<ecs::InventoryComponent>(player, inventoryComponent);

    ecs::InventoryDataComponent inventoryData;
    ItemStack hoe;
    hoe.itemId = ItemRegistry::requireIdByName("minecraft:iron_hoe");
    hoe.count = 1;
    hoe.durability = 2;
    inventoryData.inventory.setSlotStack(0, hoe);
    registry.emplace<ecs::InventoryDataComponent>(player, inventoryData);

    ecs::BlockTargetComponent target;
    target.hasTarget = true;
    target.targetBlock = dirtPos;
    target.placeBlock = dirtPos + glm::ivec3(0, 1, 0);
    target.hitNormal = glm::ivec3(0, 1, 0);
    registry.emplace<ecs::BlockTargetComponent>(player, target);
    registry.emplace<ecs::BlockInteractionRuntimeComponent>(player);

    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 1};
    ecs::SoilTillingSystem tillingSystem;
    tillingSystem.update(ctx);

    const BlockStateId tilledState = tillWorld.getBlockState(dirtPos.x, dirtPos.y, dirtPos.z);
    if (BlockStateRegistry::getBlockId(tilledState) != farmland || moistureValue(tilledState) != moisture0) {
        return fail("hoe interaction should convert dirt into dry farmland");
    }

    const ItemStack held = registry.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0);
    if (held.itemId != ItemRegistry::requireIdByName("minecraft:iron_hoe") || held.count != 1 || held.durability != 1) {
        return fail("successful tilling should consume one point of hoe durability");
    }

    const auto& runtime = registry.get<ecs::BlockInteractionRuntimeComponent>(player);
    if (runtime.placeCooldownRemaining <= 0.0f || runtime.heldItemSwingSequence != 1) {
        return fail("successful tilling should trigger placement cooldown and hand swing");
    }

    std::cout << "[farmland_moisture_and_tilling_test] PASS\n";
    return EXIT_SUCCESS;
}
