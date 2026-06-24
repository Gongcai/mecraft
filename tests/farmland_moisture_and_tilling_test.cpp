#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/interaction/SoilTillingSystem.h"
#include "../src/ecs/systems/world/FarmlandMoistureSystem.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/fluid/FluidState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[farmland_moisture_and_tilling_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadSpawnChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

uint16_t moistureValue(StateID state) {
    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    if (moisture == BlockStateRegistry::INVALID_INDEX) {
        return BlockStateRegistry::INVALID_INDEX;
    }
    return BlockStateRegistry::getPropertyIndex(state, moisture);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    const BlockID farmland = BlockRegistry::findByName("farmland");
    if (farmland == BlockIds::AIR) {
        return fail("farmland block should be registered");
    }

    const uint16_t moisture = BlockStateRegistry::getPropertyNameIndex("moisture");
    const uint16_t moisture0 = BlockStateRegistry::getPropertyValueIndex(moisture, "0");
    const uint16_t moisture7 = BlockStateRegistry::getPropertyValueIndex(moisture, "7");
    if (moisture == BlockStateRegistry::INVALID_INDEX ||
        moisture0 == BlockStateRegistry::INVALID_INDEX ||
        moisture7 == BlockStateRegistry::INVALID_INDEX) {
        return fail("farmland moisture property should expose values 0 and 7");
    }

    World hydrationWorld;
    hydrationWorld.init(20260624);
    loadSpawnChunks(hydrationWorld);

    const StateID dryFarmland = BlockStateRegistry::getDefaultState(farmland);
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

    World tillWorld;
    tillWorld.init(20260625);
    loadSpawnChunks(tillWorld);

    const glm::ivec3 dirtPos(0, 122, 0);
    tillWorld.setBlockState(dirtPos.x, dirtPos.y, dirtPos.z, BlockIds::DIRT);
    tillWorld.setBlockState(dirtPos.x, dirtPos.y + 1, dirtPos.z, BlockIds::AIR);

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
    hoe.itemId = ItemIds::IRON_HOE;
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

    const StateID tilledState = tillWorld.getBlockState(dirtPos.x, dirtPos.y, dirtPos.z);
    if (BlockStateRegistry::getBlockId(tilledState) != farmland || moistureValue(tilledState) != moisture0) {
        return fail("hoe interaction should convert dirt into dry farmland");
    }

    const ItemStack held = registry.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0);
    if (held.itemId != ItemIds::IRON_HOE || held.count != 1 || held.durability != 1) {
        return fail("successful tilling should consume one point of hoe durability");
    }

    const auto& runtime = registry.get<ecs::BlockInteractionRuntimeComponent>(player);
    if (runtime.placeCooldownRemaining <= 0.0f || runtime.heldItemSwingSequence != 1) {
        return fail("successful tilling should trigger placement cooldown and hand swing");
    }

    std::cout << "[farmland_moisture_and_tilling_test] PASS\n";
    return EXIT_SUCCESS;
}
