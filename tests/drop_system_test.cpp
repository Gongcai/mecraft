#include <cstdlib>
#include <iostream>

#include <glm/vec3.hpp>

#include "../src/world/DropSystem.h"
#include "../src/world/World.h"
#include "../src/item/Item.h"

namespace {
int fail(const char* message) {
    std::cerr << "[drop_system_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
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
        world.update(glm::vec3(0.0f));
    }

    // Build a deterministic platform so the drop can land on a known height.
    world.setBlock(0, 120, 0, BlockIds::STONE);
    world.setBlock(0, 121, 0, BlockIds::AIR);
    world.setBlock(0, 122, 0, BlockIds::AIR);

    DropSystem dropSystem;
    dropSystem.spawnBlockDrop(BlockIds::STONE, glm::ivec3(0, 122, 0));
    dropSystem.spawnBlockDrop(BlockIds::STONE, glm::ivec3(0, 122, 0));

    if (dropSystem.getDrops().empty()) {
        return fail("spawnBlockDrop should create one block drop");
    }

    if (dropSystem.getDrops().size() != 1) {
        return fail("same-position same-type drops should merge into one entity");
    }

    if (dropSystem.getDrops().front().stackCount != 2) {
        return fail("merged drop should accumulate stack count");
    }

    if (dropSystem.getDrops().front().itemId != BlockDropTable::getDropItem(BlockIds::STONE)) {
        return fail("drop payload should preserve spawned item id from BlockDropTable");
    }

    // Test coal_ore drops coal item (not itself)
    DropSystem coalDropSystem;
    coalDropSystem.spawnBlockDrop(BlockIds::COAL_ORE, glm::ivec3(0, 122, 0));
    if (coalDropSystem.getDrops().empty()) {
        return fail("coal_ore should spawn a drop");
    }
    if (coalDropSystem.getDrops().front().itemId != ItemIds::COAL) {
        return fail("coal_ore should drop coal item, not itself");
    }

    DropSystem placementDropSystem;
    const glm::ivec3 placementCell(2, 130, 2);
    placementDropSystem.spawnBlockDrop(BlockIds::STONE, placementCell);
    if (placementDropSystem.getDrops().empty()) {
        return fail("placement test setup failed to spawn drop");
    }

    world.setBlock(placementCell.x, placementCell.y, placementCell.z, BlockIds::STONE);
    placementDropSystem.onBlockPlaced(placementCell, world);

    const DropEntity& moved = placementDropSystem.getDrops().front();
    if (!moved.grounded) {
        return fail("drop should be grounded after being lifted above placed block");
    }

    const float expectedLiftedY = static_cast<float>(placementCell.y + 1) + moved.halfExtents.y;
    if (moved.position.y < expectedLiftedY || moved.position.y > expectedLiftedY + 0.01f) {
        return fail("drop should be moved to top of newly placed block");
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
