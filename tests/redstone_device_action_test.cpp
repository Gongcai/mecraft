#include <cstdlib>
#include <iostream>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include "../src/ecs/GameplayRegistry.h"
#include "../src/ecs/components/Components.h"
#include "../src/ecs/systems/world/RedstoneDeviceActionSystem.h"
#include "../src/ecs/util/RedstoneEventBuffer.h"
#include "../src/game/inventory/BlockEntityInventoryStore.h"
#include "../src/game/inventory/ContainerBehaviorRegistry.h"
#include "../src/item/Item.h"
#include "../src/ui/inventory/ContainerUiRegistry.h"
#include "../src/world/World.h"
#include "../src/world/block/PropIndices.h"
#include "../src/world/fluid/FluidState.h"

namespace {

int fail(const char* message) {
    std::cerr << "[redstone_device_action_test] FAIL: " << message << '\n';
    return EXIT_FAILURE;
}

void loadOriginChunks(World& world) {
    world.setRenderDistance(1);
    for (int i = 0; i < 8; ++i) {
        world.update(glm::vec3(0.0f), 1.0f / 20.0f);
    }
}

BlockStateId deviceState(const BlockID blockId, const uint16_t facing) {
    return BlockStateRegistry::getState(
        blockId, {{PropIndices::FACING, facing}, {PropIndices::POWERED, PropIndices::POWERED_FALSE}});
}

void pushDeviceEvent(ecs::GameplayRegistry& registry, const glm::ivec3& position, const BlockID blockId,
                     const BlockStateId stateId, const uint64_t redstoneTick) {
    ecs::ensureRedstoneDeviceActivationEventBus(registry).push({position, blockId, stateId, redstoneTick});
}

BlockEntityInventory& inventoryFor(ecs::GameplayRegistry& registry, const glm::ivec3& position,
                                   const char* behaviorId) {
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(behaviorId);
    BlockEntityInventoryStore& store = registry.ctxHas<BlockEntityInventoryStore>()
                                           ? registry.ctxGet<BlockEntityInventoryStore>()
                                           : registry.ctxSet<BlockEntityInventoryStore>();
    return store.getOrCreate(position, behavior.id, behavior.storage.slots);
}

bool isSourceWaterAt(const World& world, const glm::ivec3& position) {
    const BlockStateId fluidState = world.getFluidState(position.x, position.y, position.z);
    return FluidState::isWater(fluidState) && FluidState::isSource(fluidState);
}

} // namespace

int main() {
    BlockRegistry::init(nullptr);
    ItemRegistry::init();
    ContainerBehaviorRegistry::init();
    ui::ContainerUiRegistry::init();

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        const BlockID dispenser = BlockRegistry::requireIdByName("minecraft:dispenser");
        const BlockStateId state = deviceState(dispenser, PropIndices::FACING_EAST);
        const glm::ivec3 position(0, 122, 0);
        const glm::ivec3 target = position + glm::ivec3(1, 0, 0);
        world.setBlockState(position.x, position.y, position.z, state);
        world.setBlockState(target.x, target.y, target.z, NULL_BLOCK_STATE);
        world.setFluidState(target.x, target.y, target.z, NULL_BLOCK_STATE);

        BlockEntityInventory& inventory = inventoryFor(registry, position, "minecraft:dispenser");
        inventory.setSlotStack(0, ItemStack{ItemRegistry::requireIdByName("minecraft:water_bucket"), 1, 0});

        pushDeviceEvent(registry, position, dispenser, state, 10);
        const std::size_t actions = ecs::RedstoneDeviceActionSystem::processEvents(world, registry);
        const ItemStack slot = inventory.getSlotStack(0);
        if (actions != 1 || !ecs::ensureRedstoneDeviceActivationEventBus(registry).empty() ||
            !isSourceWaterAt(world, target) || slot.itemId != ItemRegistry::requireIdByName("minecraft:bucket") ||
            slot.count != 1) {
            return fail("dispenser should place water from a water bucket and leave an empty bucket");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        const BlockID dispenser = BlockRegistry::requireIdByName("minecraft:dispenser");
        const BlockStateId state = deviceState(dispenser, PropIndices::FACING_EAST);
        const glm::ivec3 position(0, 122, 0);
        const glm::ivec3 target = position + glm::ivec3(1, 0, 0);
        world.setBlockState(position.x, position.y, position.z, state);
        world.setBlockState(target.x, target.y, target.z, NULL_BLOCK_STATE);
        world.setFluidState(target.x, target.y, target.z, FluidState::makeWater(0, false));

        BlockEntityInventory& inventory = inventoryFor(registry, position, "minecraft:dispenser");
        inventory.setSlotStack(0, ItemStack{ItemRegistry::requireIdByName("minecraft:bucket"), 1, 0});

        pushDeviceEvent(registry, position, dispenser, state, 11);
        const std::size_t actions = ecs::RedstoneDeviceActionSystem::processEvents(world, registry);
        const ItemStack slot = inventory.getSlotStack(0);
        if (actions != 1 || world.getFluidState(target.x, target.y, target.z) != NULL_BLOCK_STATE ||
            slot.itemId != ItemRegistry::requireIdByName("minecraft:water_bucket") || slot.count != 1) {
            return fail("dispenser should pick up source water into an empty bucket");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        const BlockID dispenser = BlockRegistry::requireIdByName("minecraft:dispenser");
        const BlockStateId state = deviceState(dispenser, PropIndices::FACING_EAST);
        const glm::ivec3 position(0, 122, 0);
        world.setBlockState(position.x, position.y, position.z, state);

        const ItemID apple = ItemRegistry::requireIdByName("minecraft:apple");
        BlockEntityInventory& inventory = inventoryFor(registry, position, "minecraft:dispenser");
        inventory.setSlotStack(0, ItemStack{apple, 2, 0});

        pushDeviceEvent(registry, position, dispenser, state, 12);
        const std::size_t actions = ecs::RedstoneDeviceActionSystem::processEvents(world, registry);
        int projectileCount = 0;
        auto view =
            registry.view<ecs::ProjectileTag, ecs::ItemComponent, ecs::VelocityComponent, ecs::TransformComponent>();
        for (const entt::entity entity : view) {
            static_cast<void>(entity);
            ++projectileCount;
            const ecs::ItemComponent& item = view.get<ecs::ItemComponent>(entity);
            const ecs::VelocityComponent& velocity = view.get<ecs::VelocityComponent>(entity);
            if (item.itemId != apple || item.stackCount != 1 || velocity.velocity.x <= 0.0f ||
                glm::length(velocity.velocity) <= 0.0f) {
                return fail("dispenser projectile should carry the configured item and velocity");
            }
        }

        const ItemStack slot = inventory.getSlotStack(0);
        if (actions != 1 || projectileCount != 1 || slot.itemId != apple || slot.count != 1) {
            return fail("dispenser should launch configured throwable items and consume one item");
        }
    }

    {
        World world;
        world.init(20260627);
        loadOriginChunks(world);

        ecs::GameplayRegistry registry;
        const BlockID dropper = BlockRegistry::requireIdByName("minecraft:dropper");
        const BlockStateId state = deviceState(dropper, PropIndices::FACING_EAST);
        const glm::ivec3 position(0, 122, 0);
        const glm::ivec3 target = position + glm::ivec3(1, 0, 0);
        world.setBlockState(position.x, position.y, position.z, state);
        world.setBlockState(target.x, target.y, target.z, NULL_BLOCK_STATE);
        world.setFluidState(target.x, target.y, target.z, NULL_BLOCK_STATE);

        const ItemID waterBucket = ItemRegistry::requireIdByName("minecraft:water_bucket");
        BlockEntityInventory& inventory = inventoryFor(registry, position, "minecraft:dropper");
        inventory.setSlotStack(0, ItemStack{waterBucket, 1, 0});

        pushDeviceEvent(registry, position, dropper, state, 13);
        const std::size_t actions = ecs::RedstoneDeviceActionSystem::processEvents(world, registry);
        int dropCount = 0;
        auto view = registry.view<ecs::DropItemTag, ecs::ItemComponent, ecs::VelocityComponent>();
        for (const entt::entity entity : view) {
            static_cast<void>(entity);
            ++dropCount;
            const ecs::ItemComponent& item = view.get<ecs::ItemComponent>(entity);
            const ecs::VelocityComponent& velocity = view.get<ecs::VelocityComponent>(entity);
            if (item.itemId != waterBucket || item.stackCount != 1 || velocity.velocity.x <= 0.0f) {
                return fail("dropper should eject the water bucket as an item entity");
            }
        }

        if (actions != 1 || dropCount != 1 || !inventory.getSlotStack(0).isEmpty() ||
            world.getFluidState(target.x, target.y, target.z) != NULL_BLOCK_STATE) {
            return fail("dropper should not execute dispenser bucket behavior");
        }
    }

    std::cout << "[redstone_device_action_test] PASS\n";
    return EXIT_SUCCESS;
}
