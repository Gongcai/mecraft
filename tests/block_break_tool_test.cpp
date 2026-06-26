#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/SystemContext.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/interaction/BlockBreakSystem.h"
#include "../src/item/Item.h"
#include "../src/world/World.h"

namespace {

int fail(const char* message) {
    std::cerr << "[block_break_tool_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    World world;
    world.init(20260624);
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }

    const glm::ivec3 stonePos(0, 122, 0);
    world.setBlock(stonePos.x, stonePos.y, stonePos.z, BlockRegistry::requireIdByName("minecraft:stone"));

    ecs::GameplayRegistry registry;
    ecs::GameplayServices services;
    services.world = &world;
    services.worldView = &world;

    const entt::entity player = registry.create();
    registry.emplace<ecs::LocalPlayerTag>(player);

    ecs::BlockActionIntentComponent intent;
    intent.wantsBreak = true;
    registry.emplace<ecs::BlockActionIntentComponent>(player, intent);

    ecs::BlockTargetComponent target;
    target.hasTarget = true;
    target.targetBlock = stonePos;
    target.placeBlock = stonePos + glm::ivec3(0, 1, 0);
    registry.emplace<ecs::BlockTargetComponent>(player, target);

    registry.emplace<ecs::BlockBreakComponent>(player);
    registry.emplace<ecs::BlockInteractionRuntimeComponent>(player);

    ecs::InventoryComponent inventoryComponent;
    inventoryComponent.selectedHotbarSlot = 0;
    registry.emplace<ecs::InventoryComponent>(player, inventoryComponent);

    ecs::InventoryDataComponent inventoryData;
    ItemStack pickaxe;
    pickaxe.itemId = ItemRegistry::requireIdByName("minecraft:iron_pickaxe");
    pickaxe.count = 1;
    pickaxe.durability = 2;
    inventoryData.inventory.setSlotStack(0, pickaxe);
    registry.emplace<ecs::InventoryDataComponent>(player, inventoryData);

    ecs::TransformComponent transform;
    transform.position = glm::vec3(stonePos) + glm::vec3(0.5f, 0.5f, 2.5f);
    registry.emplace<ecs::TransformComponent>(player, transform);

    ecs::SystemContext ctx{registry, services, 0.30f, 1};
    ecs::BlockBreakSystem breakSystem;
    breakSystem.update(ctx);

    if (world.getBlock(stonePos.x, stonePos.y, stonePos.z) != RUNTIME_ID_NULL) {
        return fail("iron pickaxe should break stone within the accelerated duration");
    }

    const ItemStack held = registry.get<ecs::InventoryDataComponent>(player).inventory.getSlotStack(0);
    if (held.itemId != ItemRegistry::requireIdByName("minecraft:iron_pickaxe") || held.count != 1 || held.durability != 1) {
        return fail("successful block break should consume one point of tool durability");
    }

    std::cout << "[block_break_tool_test] PASS\n";
    return EXIT_SUCCESS;
}
