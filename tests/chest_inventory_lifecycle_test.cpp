#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/interaction/BlockBreakSystem.h"
#include "../src/ecs/systems/item/ItemSpawnSystem.h"
#include "../src/ecs/util/GameplayRuntimeContext.h"
#include "../src/game/inventory/BlockEntityInventoryStore.h"
#include "../src/game/modes/GameplayModeRules.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"
#include "../src/world/block/BlockStateRegistry.h"

namespace {

int fail(const char* message) {
    std::cerr << "[chest_inventory_lifecycle_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

uint32_t countDroppedItems(ecs::GameplayRegistry& registry, const ItemID itemId) {
    uint32_t total = 0;
    auto view = registry.view<ecs::DropItemTag, ecs::ItemComponent>();
    for (const entt::entity e : view) {
        const auto& item = view.get<ecs::ItemComponent>(e);
        if (item.itemId == itemId) {
            total += item.stackCount;
        }
    }
    return total;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    {
        BlockEntityInventoryStore store;
        const glm::ivec3 pos(3, 64, -7);
        BlockEntityInventory& chest = store.getOrCreate(pos, "minecraft:chest", 27);
        chest.setSlotItem(0, ItemRegistry::requireIdByName("minecraft:apple"), 4);
        chest.setSlotItem(7, ItemRegistry::requireIdByName("minecraft:coal"), 2);

        const auto contents = store.extractAndErase(pos);
        if (contents[0].itemId != ItemRegistry::requireIdByName("minecraft:apple") || contents[0].count != 4) {
            return fail("extractAndErase should preserve the first stored stack");
        }
        if (contents[7].itemId != ItemRegistry::requireIdByName("minecraft:coal") || contents[7].count != 2) {
            return fail("extractAndErase should preserve later stored stacks");
        }
        if (store.find(pos) != nullptr) {
            return fail("extractAndErase should remove the chest inventory from the store");
        }
    }

    World world;
    world.init(20260608);
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }

    const glm::ivec3 chestPos(0, 122, 0);
    const BlockStateId chestState =
        BlockStateRegistry::getDefaultState(BlockRegistry::requireIdByName("minecraft:chest"));
    world.setBlockState(chestPos.x, chestPos.y, chestPos.z, chestState);
    if (BlockStateRegistry::getBlockId(world.getBlock(chestPos.x, chestPos.y, chestPos.z)) !=
        BlockRegistry::requireIdByName("minecraft:chest")) {
        return fail("test setup should place a chest block in the world");
    }

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;
    services.worldView = &world;

    BlockEntityInventoryStore& store = registry.ctxSet<BlockEntityInventoryStore>();
    BlockEntityInventory& chest = store.getOrCreate(chestPos, "minecraft:chest", 27);
    chest.setSlotItem(0, ItemRegistry::requireIdByName("minecraft:apple"), 5);
    chest.setSlotItem(1, ItemRegistry::requireIdByName("minecraft:coal"), 3);

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);
    ecs::BlockActionIntentComponent intent;
    intent.wantsBreak = true;
    registry.emplace<ecs::BlockActionIntentComponent>(player, intent);

    ecs::BlockTargetComponent target;
    target.hasTarget = true;
    target.targetState = chestState;
    target.targetBlock = chestPos;
    target.placeBlock = chestPos + glm::ivec3(0, 1, 0);
    registry.emplace<ecs::BlockTargetComponent>(player, target);

    registry.emplace<ecs::BlockBreakComponent>(player);
    registry.emplace<ecs::BlockInteractionRuntimeComponent>(player);
    registry.emplace<ecs::InventoryComponent>(player);
    registry.emplace<ecs::InventoryDataComponent>(player);

    ecs::TransformComponent transform;
    transform.position = glm::vec3(chestPos) + glm::vec3(0.5f, 0.5f, 2.5f);
    registry.emplace<ecs::TransformComponent>(player, transform);

    ecs::SystemContext ctx{registry, services, 10.0f, 1};
    ecs::BlockBreakSystem breakSystem;
    breakSystem.update(ctx);

    ecs::ItemSpawnSystem itemSpawnSystem;
    itemSpawnSystem.update(ctx);

    if (world.getBlock(chestPos.x, chestPos.y, chestPos.z) != NULL_BLOCK_STATE) {
        return fail("breaking a chest should remove the block from the world");
    }
    if (store.find(chestPos) != nullptr) {
        return fail("breaking a chest should erase its stored inventory");
    }
    if (countDroppedItems(registry, ItemRegistry::requireIdByName("minecraft:apple")) != 5) {
        return fail("breaking a chest should drop stored apple stacks");
    }
    if (countDroppedItems(registry, ItemRegistry::requireIdByName("minecraft:coal")) != 3) {
        return fail("breaking a chest should drop stored coal stacks");
    }
    if (countDroppedItems(registry, ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:chest"))) != 1) {
        return fail("survival chest break should still drop the chest item itself");
    }

    {
        World creativeWorld;
        creativeWorld.init(20260608);
        creativeWorld.setRenderDistance(1);
        for (int i = 0; i < 8; ++i) {
            creativeWorld.update(glm::vec3(0.0f), 1.0f / 20.0f);
        }

        const glm::ivec3 dispenserPos(2, 122, 0);
        const BlockID dispenserBlock = BlockRegistry::requireIdByName("minecraft:dispenser");
        const BlockStateId dispenserState = BlockStateRegistry::getDefaultState(dispenserBlock);
        creativeWorld.setBlockState(dispenserPos.x, dispenserPos.y, dispenserPos.z, dispenserState);

        ecs::GameplayRegistry creativeRegistry;
        ecs::GameplayServices creativeServices;
        creativeServices.world = &creativeWorld;
        creativeServices.worldView = &creativeWorld;

        auto& runtime = creativeRegistry.ctxSet<ecs::GameplayRuntimeContext>();
        runtime.modeRules = &CreativeModeRules::instance();
        runtime.gameplayMode = GameplayMode::Creative;

        BlockEntityInventoryStore& creativeStore = creativeRegistry.ctxSet<BlockEntityInventoryStore>();
        BlockEntityInventory& dispenser = creativeStore.getOrCreate(dispenserPos, "minecraft:dispenser", 9);
        dispenser.setSlotItem(0, ItemRegistry::requireIdByName("minecraft:apple"), 4);

        const entt::entity creativePlayer = creativeRegistry.create();
        creativeRegistry.emplace<ecs::LocalPlayerTag>(creativePlayer);
        ecs::BlockActionIntentComponent creativeIntent;
        creativeIntent.wantsBreak = true;
        creativeRegistry.emplace<ecs::BlockActionIntentComponent>(creativePlayer, creativeIntent);

        ecs::BlockTargetComponent creativeTarget;
        creativeTarget.hasTarget = true;
        creativeTarget.targetState = dispenserState;
        creativeTarget.targetBlock = dispenserPos;
        creativeTarget.placeBlock = dispenserPos + glm::ivec3(0, 1, 0);
        creativeRegistry.emplace<ecs::BlockTargetComponent>(creativePlayer, creativeTarget);

        creativeRegistry.emplace<ecs::BlockBreakComponent>(creativePlayer);
        creativeRegistry.emplace<ecs::BlockInteractionRuntimeComponent>(creativePlayer);
        creativeRegistry.emplace<ecs::InventoryComponent>(creativePlayer);
        creativeRegistry.emplace<ecs::InventoryDataComponent>(creativePlayer);

        ecs::TransformComponent creativeTransform;
        creativeTransform.position = glm::vec3(dispenserPos) + glm::vec3(0.5f, 0.5f, 2.5f);
        creativeRegistry.emplace<ecs::TransformComponent>(creativePlayer, creativeTransform);

        ecs::SystemContext creativeCtx{creativeRegistry, creativeServices, 1.0f / 60.0f, 2};
        breakSystem.update(creativeCtx);

        if (creativeWorld.getBlock(dispenserPos.x, dispenserPos.y, dispenserPos.z) != NULL_BLOCK_STATE) {
            return fail("creative dispenser break should remove the block from the world");
        }
        if (creativeStore.find(dispenserPos) != nullptr) {
            return fail("creative dispenser break should erase its stored inventory");
        }
        if (countDroppedItems(creativeRegistry, ItemRegistry::requireIdByName("minecraft:apple")) != 4) {
            return fail("creative dispenser break should drop stored contents");
        }
        if (countDroppedItems(creativeRegistry, ItemRegistry::fromBlock(dispenserBlock)) != 0) {
            return fail("creative dispenser break should not drop the dispenser block item");
        }
    }

    std::cout << "[chest_inventory_lifecycle_test] PASS\n";
    return EXIT_SUCCESS;
}
