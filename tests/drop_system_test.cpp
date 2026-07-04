#include <cstdlib>
#include <cmath>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/GameplayServices.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/components/NetworkComponents.h"
#include "../src/world/block/BlockStateRegistry.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/DropSystem.h"
#include "../src/world/World.h"
#include "../src/player/Inventory.h"
#include "../src/item/Item.h"

namespace {
int fail(const char* message) {
    std::cerr << "[drop_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

uint32_t inventoryItemCount(const Inventory& inventory, const ItemID itemId) {
    uint32_t total = 0;
    for (int slot = 0; slot < Inventory::INVENTORY_SIZE; ++slot) {
        const ItemStack stack = inventory.getSlotStack(slot);
        if (stack.itemId == itemId) {
            total += stack.count;
        }
    }
    return total;
}

bool nearlyEqual(const float a, const float b) {
    return std::fabs(a - b) < 0.0001f;
}
}

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    BlockDropTable::init();

    World world;
    world.init(20260410);
    world.setRenderDistance(1);

    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }

    // Build a deterministic platform so the drop can land on a known height.
    world.setBlock(0, 120, 0, BlockRegistry::requireIdByName("minecraft:stone"));
    world.setBlock(0, 121, 0, RUNTIME_ID_NULL);
    world.setBlock(0, 122, 0, RUNTIME_ID_NULL);

    ecs::GameplayServices services;
    services.world = &world;

    ecs::GameplayRegistry dropRegistry;
    DropSystem dropSystem;
    dropSystem.bindRegistry(dropRegistry);
    dropSystem.bindServices(services);
    dropSystem.spawnBlockDrop(BlockRegistry::requireIdByName("minecraft:stone"), glm::ivec3(0, 122, 0));
    dropSystem.spawnBlockDrop(BlockRegistry::requireIdByName("minecraft:stone"), glm::ivec3(0, 122, 0));

    if (dropSystem.getDrops().empty()) {
        return fail("spawnBlockDrop should create one block drop");
    }

    if (dropSystem.getDrops().size() != 1) {
        return fail("same-position same-type drops should merge into one entity");
    }

    if (dropSystem.getDrops().front().stackCount != 2) {
        return fail("merged drop should accumulate stack count");
    }

    if (dropSystem.getDrops().front().itemId != BlockDropTable::getDropItem(BlockRegistry::requireIdByName("minecraft:stone"))) {
        return fail("drop payload should preserve spawned item id from BlockDropTable");
    }

    auto dropView = dropRegistry.view<ecs::DropItemTag, ecs::TransformComponent, ecs::VelocityComponent>();
    for (const entt::entity e : dropView) {
        dropView.get<ecs::TransformComponent>(e).position = glm::vec3(0.5f, 122.42f, 0.5f);
        dropView.get<ecs::VelocityComponent>(e).velocity = glm::vec3(0.0f);
    }

    // Test coal_ore drops coal item (not itself)
    ecs::GameplayRegistry coalDropRegistry;
    DropSystem coalDropSystem;
    coalDropSystem.bindRegistry(coalDropRegistry);
    coalDropSystem.spawnBlockDrop(BlockRegistry::requireIdByName("minecraft:coal_ore"), glm::ivec3(0, 122, 0));
    if (coalDropSystem.getDrops().empty()) {
        return fail("coal_ore should spawn a drop");
    }
    if (coalDropSystem.getDrops().front().itemId != ItemRegistry::requireIdByName("minecraft:coal")) {
        return fail("coal_ore should drop coal item, not itself");
    }

    ecs::GameplayRegistry stateDropRegistry;
    DropSystem stateDropSystem;
    stateDropSystem.bindRegistry(stateDropRegistry);
    const BlockStateId birchLogX = BlockStateRegistry::getState(
        BlockRegistry::requireIdByName("minecraft:birch_log"),
        std::vector<std::pair<uint16_t, uint16_t>>{
            {PropIndices::AXIS, PropIndices::AXIS_X}
        });
    stateDropSystem.spawnBlockDrop(BlockStateRegistry::getBlockId(birchLogX), glm::ivec3(1, 122, 1));
    if (stateDropSystem.getDrops().empty()) {
        return fail("stateful blocks should still spawn drops");
    }
    if (stateDropSystem.getDrops().front().itemId != ItemRegistry::fromBlock(BlockRegistry::requireIdByName("minecraft:birch_log"))) {
        return fail("stateful blocks should resolve drops through their owning base block");
    }

    ecs::GameplayRegistry placementDropRegistry;
    DropSystem placementDropSystem;
    placementDropSystem.bindRegistry(placementDropRegistry);
    const glm::ivec3 placementCell(2, 130, 2);
    placementDropSystem.spawnBlockDrop(BlockRegistry::requireIdByName("minecraft:stone"), placementCell);
    if (placementDropSystem.getDrops().empty()) {
        return fail("placement test setup failed to spawn drop");
    }

    world.setBlock(placementCell.x, placementCell.y, placementCell.z, BlockRegistry::requireIdByName("minecraft:stone"));
    placementDropSystem.onBlockPlaced(placementCell, world);

    const DropEntity& moved = placementDropSystem.getDrops().front();
    if (!moved.grounded) {
        return fail("drop should be grounded after being lifted above placed block");
    }

    const float expectedLiftedY = static_cast<float>(placementCell.y + 1) + moved.halfExtents.y;
    if (moved.position.y < expectedLiftedY || moved.position.y > expectedLiftedY + 0.01f) {
        return fail("drop should be moved to top of newly placed block");
    }

    DropSystem itemSpawnDropSystem;
    ecs::GameplayRegistry itemSpawnRegistry;
    itemSpawnDropSystem.bindRegistry(itemSpawnRegistry);
    itemSpawnDropSystem.spawnItemDrop(ItemRegistry::requireIdByName("minecraft:coal"), glm::ivec3(4, 122, 4), 1);
    itemSpawnDropSystem.spawnItemDrop(ItemRegistry::requireIdByName("minecraft:coal"), glm::ivec3(4, 122, 4), 2);
    if (itemSpawnDropSystem.getDrops().size() != 1 || itemSpawnDropSystem.getDrops().front().stackCount != 3) {
        return fail("spawnItemDrop should merge same-position same-item stacks");
    }

    DropSystem collectDropSystem;
    ecs::GameplayRegistry collectDropRegistry;
    collectDropSystem.bindRegistry(collectDropRegistry);
    collectDropSystem.spawnItemDrop(ItemRegistry::requireIdByName("minecraft:coal"), glm::ivec3(6, 122, 6), 2);
    Inventory inventory;
    const uint32_t coalBefore = inventoryItemCount(inventory, ItemRegistry::requireIdByName("minecraft:coal"));
    const uint32_t collected = collectDropSystem.collectNearbyDrops(glm::vec3(6.5f, 122.42f, 6.5f), 1.0f, inventory);
    if (collected != 2) {
        return fail("collectNearbyDrops should collect stacks within radius");
    }
    if (!collectDropSystem.getDrops().empty()) {
        return fail("collectNearbyDrops should remove fully collected drops");
    }
    if (inventoryItemCount(inventory, ItemRegistry::requireIdByName("minecraft:coal")) != coalBefore + 2) {
        return fail("collectNearbyDrops should add items into inventory stacks");
    }

    world.setSimulationDistance(1);
    world.ticketManager().updatePlayerPosition(0, 0);
    DropSystem inactiveDropSystem;
    ecs::GameplayRegistry inactiveDropRegistry;
    inactiveDropSystem.bindRegistry(inactiveDropRegistry);
    inactiveDropSystem.bindServices(services);
    inactiveDropSystem.spawnItemDrop(ItemRegistry::requireIdByName("minecraft:coal"), glm::ivec3(48, 122, 0), 1);
    auto inactiveView = inactiveDropRegistry.view<ecs::DropItemTag,
                                                   ecs::TransformComponent,
                                                   ecs::VelocityComponent,
                                                   ecs::LifetimeComponent>();
    if (inactiveView.begin() == inactiveView.end()) {
        return fail("inactive simulation setup should spawn a drop entity");
    }
    const entt::entity inactiveEntity = *inactiveView.begin();
    inactiveView.get<ecs::TransformComponent>(inactiveEntity).position = glm::vec3(48.5f, 122.42f, 0.5f);
    inactiveView.get<ecs::VelocityComponent>(inactiveEntity).velocity = glm::vec3(0.0f, 5.0f, 0.0f);
    inactiveView.get<ecs::LifetimeComponent>(inactiveEntity).ageSeconds = 4.0f;
    inactiveDropSystem.update(1.0f, world);
    const DropEntity& inactiveDrop = inactiveDropSystem.getDrops().front();
    if (!nearlyEqual(inactiveDrop.position.y, 122.42f) ||
        !nearlyEqual(inactiveDrop.velocity.y, 5.0f) ||
        !nearlyEqual(inactiveDrop.ageSeconds, 4.0f)) {
        return fail("drops outside simulation distance should not run physics or lifetime updates");
    }

    DropSystem restoreDropSystem;
    ecs::GameplayRegistry restoreDropRegistry;
    restoreDropSystem.bindRegistry(restoreDropRegistry);

    DropEntity savedDrop;
    savedDrop.id = 42;
    savedDrop.itemId = ItemRegistry::requireIdByName("minecraft:coal");
    savedDrop.stackCount = 3;
    savedDrop.position = glm::vec3(8.25f, 123.0f, 8.75f);
    savedDrop.velocity = glm::vec3(0.1f, 0.2f, 0.3f);
    savedDrop.halfExtents = glm::vec3(0.2f);
    savedDrop.yawRadians = 1.25f;
    savedDrop.spinSpeedRadians = 2.5f;
    savedDrop.ageSeconds = 4.0f;
    savedDrop.lifeTimeSeconds = 30.0f;
    savedDrop.grounded = true;
    restoreDropSystem.restoreDrops({savedDrop});

    auto restoredDrops = restoreDropSystem.getDrops();
    if (restoredDrops.size() != 1) {
        return fail("restoreDrops should restore a visible ECS drop");
    }
    const DropEntity& restored = restoredDrops.front();
    if (restored.id != savedDrop.id || restored.itemId != savedDrop.itemId || restored.stackCount != savedDrop.stackCount) {
        return fail("restoreDrops should preserve saved drop identity and item stack");
    }
    if (!nearlyEqual(restored.position.x, savedDrop.position.x) ||
        !nearlyEqual(restored.halfExtents.x, savedDrop.halfExtents.x) ||
        !nearlyEqual(restored.ageSeconds, savedDrop.ageSeconds) ||
        !restored.grounded) {
        return fail("restoreDrops should preserve saved transform, bounds, lifetime, and grounded state");
    }

    auto restoredView = restoreDropRegistry.registry().view<ecs::DropItemTag,
                                                            ecs::DropEntityIdComponent,
                                                            ecs::BoundsComponent,
                                                            ecs::NetworkSyncTag>();
    if (restoredView.begin() == restoredView.end()) {
        return fail("restored drops should include required ECS and network sync components");
    }

    restoreDropSystem.spawnItemDrop(ItemRegistry::requireIdByName("minecraft:coal"), glm::ivec3(20, 122, 20), 1);
    restoredDrops = restoreDropSystem.getDrops();
    bool sawFutureId = false;
    for (const DropEntity& drop : restoredDrops) {
        sawFutureId = sawFutureId || drop.id > savedDrop.id;
    }
    if (!sawFutureId) {
        return fail("new drops after restore should not reuse saved drop ids");
    }

    const float initialYaw = dropSystem.getDrops().front().yawRadians;

    for (int i = 0; i < 300; ++i) {
        dropSystem.update(1.0f / 60.0f, world);
    }

    const auto& dropsAfterFall = dropSystem.getDrops();
    if (dropsAfterFall.empty()) {
        return fail("drop expired too early");
    }

    if (dropsAfterFall.front().stackCount != 2) {
        return fail("stack count should be preserved after physics updates");
    }

    const DropEntity& landed = dropsAfterFall.front();
    if (!landed.grounded) {
        return fail("drop should be grounded after falling onto platform");
    }

    if (landed.position.y < 121.10f || landed.position.y > 121.35f) {
        return fail("grounded drop y-position out of expected resting range");
    }

    if (landed.yawRadians == initialYaw) {
        return fail("drop rotation should advance over time");
    }

    for (int i = 0; i < 1900; ++i) {
        dropSystem.update(1.0f / 60.0f, world);
    }

    if (!dropSystem.getDrops().empty()) {
        return fail("drop should be removed after lifetime expires");
    }

    std::cout << "[drop_system_test] PASS\n";
    return EXIT_SUCCESS;
}
