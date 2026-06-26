#include <cstdlib>
#include <iostream>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/interaction/BlockTargetSystem.h"
#include "../src/ecs/systems/interaction/BucketUseSystem.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/fluid/FluidState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[bucket_use_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadSpawnChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

struct BucketRunResult {
    ItemStack selectedStack{};
    float placeCooldownRemaining = 0.0f;
    uint32_t heldItemSwingSequence = 0;
    bool hadTarget = false;
    glm::ivec3 targetBlock{};
};

BucketRunResult runBucketUse(World& world,
                             const ItemID selectedItem,
                             const glm::ivec3& targetBlock,
                             const glm::ivec3& placeBlock,
                             const glm::ivec3& hitNormal) {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;
    services.worldView = &world;

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);

    ecs::BlockActionIntentComponent intent;
    intent.wantsPlace = true;
    registry.emplace<ecs::BlockActionIntentComponent>(player, intent);

    ecs::TransformComponent transform;
    transform.position = glm::vec3(targetBlock) + glm::vec3(0.5f, 0.5f, 2.5f);
    registry.emplace<ecs::TransformComponent>(player, transform);

    ecs::InventoryComponent inventoryComponent;
    inventoryComponent.selectedHotbarSlot = 0;
    registry.emplace<ecs::InventoryComponent>(player, inventoryComponent);

    ecs::InventoryDataComponent inventoryData;
    ItemStack held;
    held.itemId = selectedItem;
    held.count = 1;
    inventoryData.inventory.setSlotStack(0, held);
    registry.emplace<ecs::InventoryDataComponent>(player, inventoryData);

    ecs::BlockTargetComponent target;
    target.hasTarget = true;
    target.targetBlock = targetBlock;
    target.placeBlock = placeBlock;
    target.hitNormal = hitNormal;
    target.targetState = world.getBlockState(targetBlock.x, targetBlock.y, targetBlock.z);
    registry.emplace<ecs::BlockTargetComponent>(player, target);
    registry.emplace<ecs::BlockInteractionRuntimeComponent>(player);

    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 1};
    ecs::BucketUseSystem system;
    system.update(ctx);

    const auto& updatedInventory = registry.get<ecs::InventoryDataComponent>(player).inventory;
    const auto& runtime = registry.get<ecs::BlockInteractionRuntimeComponent>(player);
    const auto& updatedTarget = registry.get<ecs::BlockTargetComponent>(player);
    return {
        updatedInventory.getSlotStack(0),
        runtime.placeCooldownRemaining,
        runtime.heldItemSwingSequence,
        updatedTarget.hasTarget,
        updatedTarget.targetBlock
    };
}

BucketRunResult runBucketUseFromRaycast(World& world,
                                        const ItemID selectedItem,
                                        const glm::vec3& eyePosition,
                                        const glm::vec3& viewDirection) {
    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;
    services.worldView = &world;

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);

    ecs::BlockActionIntentComponent intent;
    intent.wantsPlace = true;
    registry.emplace<ecs::BlockActionIntentComponent>(player, intent);

    ecs::TransformComponent transform;
    transform.eyeHeight = 1.62f;
    transform.position = eyePosition - glm::vec3(0.0f, transform.eyeHeight, 0.0f);
    registry.emplace<ecs::TransformComponent>(player, transform);

    ecs::CameraStateComponent camera;
    camera.front = glm::normalize(viewDirection);
    registry.emplace<ecs::CameraStateComponent>(player, camera);

    ecs::InventoryComponent inventoryComponent;
    inventoryComponent.selectedHotbarSlot = 0;
    registry.emplace<ecs::InventoryComponent>(player, inventoryComponent);

    ecs::InventoryDataComponent inventoryData;
    ItemStack held;
    held.itemId = selectedItem;
    held.count = 1;
    inventoryData.inventory.setSlotStack(0, held);
    registry.emplace<ecs::InventoryDataComponent>(player, inventoryData);

    registry.emplace<ecs::BlockTargetComponent>(player);
    registry.emplace<ecs::BlockInteractionRuntimeComponent>(player);

    ecs::SystemContext ctx{registry, services, 1.0f / 60.0f, 1};
    ecs::BlockTargetSystem targetSystem;
    targetSystem.update(ctx);
    ecs::BucketUseSystem bucketSystem;
    bucketSystem.update(ctx);

    const auto& updatedInventory = registry.get<ecs::InventoryDataComponent>(player).inventory;
    const auto& runtime = registry.get<ecs::BlockInteractionRuntimeComponent>(player);
    const auto& updatedTarget = registry.get<ecs::BlockTargetComponent>(player);
    return {
        updatedInventory.getSlotStack(0),
        runtime.placeCooldownRemaining,
        runtime.heldItemSwingSequence,
        updatedTarget.hasTarget,
        updatedTarget.targetBlock
    };
}

bool isSourceWaterAt(const World& world, const glm::ivec3& pos) {
    const StateID fluidState = world.getFluidState(pos.x, pos.y, pos.z);
    return FluidState::isWater(fluidState) && FluidState::isSource(fluidState);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();

    World world;
    world.init(20260626);
    loadSpawnChunks(world);

    const glm::ivec3 sourcePos(0, 122, 0);
    world.setBlockState(sourcePos.x, sourcePos.y, sourcePos.z, RUNTIME_ID_NULL);
    world.setFluidState(sourcePos.x, sourcePos.y, sourcePos.z, FluidState::makeWater(0, false));

    BucketRunResult pickupResult = runBucketUse(
        world,
        ItemRegistry::requireIdByName("minecraft:bucket"),
        sourcePos,
        sourcePos + glm::ivec3(0, 1, 0),
        glm::ivec3(0, 1, 0));
    if (world.getFluidState(sourcePos.x, sourcePos.y, sourcePos.z) != RUNTIME_ID_NULL ||
        pickupResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:water_bucket") ||
        pickupResult.selectedStack.count != 1 ||
        pickupResult.placeCooldownRemaining <= 0.0f ||
        pickupResult.heldItemSwingSequence != 1) {
        return fail("empty bucket should pick up source water and become a water bucket");
    }

    const glm::ivec3 raycastSourcePos(1, 122, 0);
    world.setBlockState(raycastSourcePos.x, raycastSourcePos.y, raycastSourcePos.z, RUNTIME_ID_NULL);
    world.setFluidState(raycastSourcePos.x,
                        raycastSourcePos.y,
                        raycastSourcePos.z,
                        FluidState::makeWater(0, false));
    BucketRunResult raycastPickupResult = runBucketUseFromRaycast(
        world,
        ItemRegistry::requireIdByName("minecraft:bucket"),
        glm::vec3(raycastSourcePos) + glm::vec3(0.5f, 0.5f, 2.5f),
        glm::vec3(0.0f, 0.0f, -1.0f));
    if (!raycastPickupResult.hadTarget ||
        raycastPickupResult.targetBlock != raycastSourcePos ||
        world.getFluidState(raycastSourcePos.x, raycastSourcePos.y, raycastSourcePos.z) != RUNTIME_ID_NULL ||
        raycastPickupResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:water_bucket") ||
        raycastPickupResult.heldItemSwingSequence != 1) {
        return fail("empty bucket should pick up source water selected by world raycast");
    }

    const glm::ivec3 blockLayerSourcePos(4, 122, 0);
    world.setBlockState(blockLayerSourcePos.x,
                        blockLayerSourcePos.y,
                        blockLayerSourcePos.z,
                        FluidState::makeWater(0, false));
    BucketRunResult blockLayerPickupResult = runBucketUseFromRaycast(
        world,
        ItemRegistry::requireIdByName("minecraft:bucket"),
        glm::vec3(blockLayerSourcePos) + glm::vec3(0.5f, 0.5f, 2.5f),
        glm::vec3(0.0f, 0.0f, -1.0f));
    if (!blockLayerPickupResult.hadTarget ||
        blockLayerPickupResult.targetBlock != blockLayerSourcePos ||
        world.getBlockState(blockLayerSourcePos.x, blockLayerSourcePos.y, blockLayerSourcePos.z) != RUNTIME_ID_NULL ||
        blockLayerPickupResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:water_bucket") ||
        blockLayerPickupResult.heldItemSwingSequence != 1) {
        return fail("empty bucket should pick up source water stored in the block layer");
    }

    const glm::ivec3 placePos(2, 122, 0);
    world.setBlockState(placePos.x, placePos.y, placePos.z, RUNTIME_ID_NULL);
    world.setFluidState(placePos.x, placePos.y, placePos.z, RUNTIME_ID_NULL);

    BucketRunResult placeResult = runBucketUse(
        world,
        ItemRegistry::requireIdByName("minecraft:water_bucket"),
        placePos + glm::ivec3(0, -1, 0),
        placePos,
        glm::ivec3(0, 1, 0));
    if (!isSourceWaterAt(world, placePos) ||
        placeResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:bucket") ||
        placeResult.selectedStack.count != 1 ||
        placeResult.placeCooldownRemaining <= 0.0f ||
        placeResult.heldItemSwingSequence != 1) {
        return fail("water bucket should place source water and become an empty bucket");
    }

    const glm::ivec3 flowingPos(4, 122, 0);
    world.setBlockState(flowingPos.x, flowingPos.y, flowingPos.z, RUNTIME_ID_NULL);
    world.setFluidState(flowingPos.x, flowingPos.y, flowingPos.z, FluidState::makeWater(3, false));

    BucketRunResult flowingPickupResult = runBucketUse(
        world,
        ItemRegistry::requireIdByName("minecraft:bucket"),
        flowingPos,
        flowingPos + glm::ivec3(0, 1, 0),
        glm::ivec3(0, 1, 0));
    if (flowingPickupResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:bucket") ||
        flowingPickupResult.heldItemSwingSequence != 0 ||
        world.getFluidState(flowingPos.x, flowingPos.y, flowingPos.z) != FluidState::makeWater(3, false)) {
        return fail("empty bucket should not pick up non-source water");
    }

    const glm::ivec3 blockedPos(6, 122, 0);
    world.setBlockState(blockedPos.x, blockedPos.y, blockedPos.z, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setFluidState(blockedPos.x, blockedPos.y, blockedPos.z, RUNTIME_ID_NULL);

    BucketRunResult blockedPlaceResult = runBucketUse(
        world,
        ItemRegistry::requireIdByName("minecraft:water_bucket"),
        blockedPos + glm::ivec3(0, -1, 0),
        blockedPos,
        glm::ivec3(0, 1, 0));
    if (blockedPlaceResult.selectedStack.itemId != ItemRegistry::requireIdByName("minecraft:water_bucket") ||
        blockedPlaceResult.heldItemSwingSequence != 0 ||
        world.getBlockState(blockedPos.x, blockedPos.y, blockedPos.z) != BlockRegistry::requireIdByName("minecraft:stone") ||
        world.getFluidState(blockedPos.x, blockedPos.y, blockedPos.z) != RUNTIME_ID_NULL) {
        return fail("water bucket should not place water into a non-coexisting solid block");
    }

    std::cout << "[bucket_use_test] PASS\n";
    return EXIT_SUCCESS;
}
