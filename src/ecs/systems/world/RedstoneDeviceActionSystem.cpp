#include "RedstoneDeviceActionSystem.h"

#include "../../components/Components.h"
#include "../../entity/EntityFactory.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/ProjectileDefinitions.h"
#include "../../util/RedstoneEventBuffer.h"
#include "../../../game/inventory/BlockEntityInventoryStore.h"
#include "../../../game/inventory/ContainerBehaviorRegistry.h"
#include "../../../item/ItemUseDispatcher.h"
#include "../../../ui/inventory/ContainerUiRegistry.h"
#include "../../../world/World.h"
#include "../../../world/block/PropIndices.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace ecs {
namespace {

[[noreturn]] void failRedstoneDeviceActionSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr float kDispensedItemSpeed = 4.0f;
constexpr float kDispensedItemOffset = 0.72f;
constexpr float kDispensedItemSpinRadians = 2.6f;

uint16_t requiredPropertyValue(const BlockStateId stateId, const uint16_t property, const char* propertyName) {
    if (property == PropIndices::INVALID) {
        failRedstoneDeviceActionSystem(std::string("Redstone device property is not registered: ") + propertyName);
    }

    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        failRedstoneDeviceActionSystem(std::string("Redstone device state is missing property: ") + propertyName);
    }
    return value;
}

glm::ivec3 directionFromFacingValue(const uint16_t facing) {
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    if (facing == PropIndices::FACING_UP) {
        return {0, 1, 0};
    }
    if (facing == PropIndices::FACING_DOWN) {
        return {0, -1, 0};
    }
    failRedstoneDeviceActionSystem("Redstone device state contains an unknown facing value");
}

glm::ivec3 facingDirection(const BlockStateId stateId) {
    return directionFromFacingValue(requiredPropertyValue(stateId, PropIndices::FACING, "facing"));
}

void mixHash(uint64_t& seed, const uint32_t value) {
    seed ^= static_cast<uint64_t>(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

std::size_t eventHash(const RedstoneDeviceActivationEvent& event) {
    uint64_t seed = event.redstoneTick;
    mixHash(seed, static_cast<uint32_t>(event.position.x));
    mixHash(seed, static_cast<uint32_t>(event.position.y));
    mixHash(seed, static_cast<uint32_t>(event.position.z));
    mixHash(seed, static_cast<uint32_t>(event.blockId));
    return static_cast<std::size_t>(seed);
}

int selectTriggeredSlot(const BlockEntityInventory& inventory, const int slotCount,
                        const RedstoneDeviceActivationEvent& event) {
    std::array<int, BlockEntityInventory::SLOT_COUNT> nonEmptySlots{};
    int nonEmptyCount = 0;
    for (int slot = 0; slot < slotCount; ++slot) {
        if (!inventory.getSlotStack(slot).isEmpty()) {
            nonEmptySlots[static_cast<std::size_t>(nonEmptyCount)] = slot;
            ++nonEmptyCount;
        }
    }

    if (nonEmptyCount == 0) {
        return -1;
    }

    const std::size_t selectedIndex = eventHash(event) % static_cast<std::size_t>(nonEmptyCount);
    return nonEmptySlots[selectedIndex];
}

const ContainerBehaviorDef& storageBehaviorForDevice(const BlockDef& blockDef) {
    if (blockDef.containerUi.empty()) {
        failRedstoneDeviceActionSystem(blockDef.namespacedId.full() +
                                       " redstone device requires a container UI binding");
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.handler != "storage") {
        failRedstoneDeviceActionSystem(behavior.id + " redstone device requires storage container behavior");
    }
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        failRedstoneDeviceActionSystem(behavior.id + " redstone device requires block_entity storage");
    }
    return behavior;
}

BlockEntityInventoryStore& ensureInventoryStore(GameplayRegistry& registry) {
    if (!registry.ctxHas<BlockEntityInventoryStore>()) {
        registry.ctxSet<BlockEntityInventoryStore>();
    }
    return registry.ctxGet<BlockEntityInventoryStore>();
}

void consumeOneFromSlot(BlockEntityInventory& inventory, const int slot, const ItemStack& stack) {
    if (stack.count <= 1) {
        inventory.setSlotStack(slot, {});
        return;
    }

    ItemStack remaining = stack;
    --remaining.count;
    inventory.setSlotStack(slot, remaining);
}

void replaceSingleItemInSlot(BlockEntityInventory& inventory, const int slot, const ItemStack& stack,
                             const ItemID resultItem) {
    if (stack.count != 1) {
        failRedstoneDeviceActionSystem("Single-result redstone device action received a multi-item stack");
    }

    if (resultItem == RUNTIME_ID_NULL) {
        inventory.setSlotStack(slot, {});
        return;
    }

    ItemStack result;
    result.itemId = resultItem;
    result.count = 1;
    result.durability = ItemRegistry::get(resultItem).isTool ? ItemRegistry::get(resultItem).maxDurability : 0;
    inventory.setSlotStack(slot, result);
}

bool placeFluidFromBucket(World& world, GameplayRegistry& registry, BlockEntityInventory& inventory, const int slot,
                          const ItemStack& stack, const ItemDef& itemDef, const glm::ivec3& targetPosition) {
    const ItemUseRule* rule = ItemUseRules::findRule(itemDef, ItemUseBehavior::BucketPlaceFluid);
    if (rule == nullptr) {
        return false;
    }
    if (!ItemUseDispatcher::canPlaceFluid(world, targetPosition, *rule)) {
        return false;
    }

    const BlockStateId sourceFluid = ItemUseDispatcher::makeSourceFluidState(rule->resultBlock);
    world.setFluidState(targetPosition.x, targetPosition.y, targetPosition.z, sourceFluid);
    replaceSingleItemInSlot(inventory, slot, stack, rule->resultItem);
    ensureAudioEventBus(registry).push({"item.bucket.empty", glm::vec3(targetPosition) + glm::vec3(0.5f), true, 1.0f});
    return true;
}

bool pickupFluidIntoBucket(World& world, GameplayRegistry& registry, BlockEntityInventory& inventory, const int slot,
                           const ItemStack& stack, const ItemDef& itemDef, const glm::ivec3& targetPosition) {
    const ItemUseRule* rule = ItemUseRules::findRule(itemDef, ItemUseBehavior::BucketPickupFluid);
    if (rule == nullptr) {
        return false;
    }
    if (!ItemUseDispatcher::canPickupFluid(world, targetPosition, *rule)) {
        return false;
    }

    world.setFluidState(targetPosition.x, targetPosition.y, targetPosition.z, NULL_BLOCK_STATE);
    replaceSingleItemInSlot(inventory, slot, stack, rule->resultItem);
    ensureAudioEventBus(registry).push({"item.bucket.fill", glm::vec3(targetPosition) + glm::vec3(0.5f), true, 1.0f});
    return true;
}

bool launchProjectile(GameplayRegistry& registry, BlockEntityInventory& inventory, const int slot,
                      const ItemStack& stack, const ItemID itemId, const glm::ivec3& devicePosition,
                      const glm::ivec3& direction) {
    std::string projectileLoadError;
    if (!ensureThrowableProjectileDefinitionsLoaded(&projectileLoadError)) {
        failRedstoneDeviceActionSystem("Failed to load projectile definitions: " + projectileLoadError);
    }

    ProjectileDefinition definition;
    if (!getThrowableProjectileDefinition(itemId, definition)) {
        return false;
    }

    const glm::vec3 directionVector = glm::normalize(glm::vec3(direction));
    const glm::vec3 spawnPosition =
        glm::vec3(devicePosition) + glm::vec3(0.5f) + directionVector * definition.spawnForwardOffset;
    const glm::vec3 velocity = directionVector * definition.throwSpeed;
    EntityFactory::createProjectile(registry, entt::null, spawnPosition, velocity, definition);
    consumeOneFromSlot(inventory, slot, stack);
    if (!definition.throwSoundId.empty()) {
        ensureAudioEventBus(registry).push({definition.throwSoundId, spawnPosition, true, 1.0f});
    }
    return true;
}

void ejectItem(GameplayRegistry& registry, BlockEntityInventory& inventory, const int slot, const ItemStack& stack,
               const glm::ivec3& devicePosition, const glm::ivec3& direction) {
    const glm::vec3 directionVector = glm::normalize(glm::vec3(direction));
    ItemDropSpawnParams params;
    params.itemId = stack.itemId;
    params.stackCount = 1;
    params.position = glm::vec3(devicePosition) + glm::vec3(0.5f) + directionVector * kDispensedItemOffset;
    params.velocity = directionVector * kDispensedItemSpeed;
    params.halfExtents = glm::vec3(0.175f);
    params.yawRadians = 0.0f;
    params.spinSpeedRadians = kDispensedItemSpinRadians;
    params.ageSeconds = 0.0f;
    params.lifeTimeSeconds = 30.0f;
    params.grounded = false;
    EntityFactory::createItemDrop(registry, params);
    consumeOneFromSlot(inventory, slot, stack);
}

bool executeDispenser(World& world, GameplayRegistry& registry, const RedstoneDeviceActivationEvent& event,
                      const bool useItemBehaviors) {
    const BlockDef& blockDef = BlockRegistry::getFast(event.blockId);
    const ContainerBehaviorDef& behavior = storageBehaviorForDevice(blockDef);
    BlockEntityInventory& inventory =
        ensureInventoryStore(registry).getOrCreate(event.position, behavior.id, behavior.storage.slots);

    const int slot = selectTriggeredSlot(inventory, behavior.storage.slots, event);
    if (slot < 0) {
        return false;
    }

    const ItemStack stack = inventory.getSlotStack(slot);
    const glm::ivec3 direction = facingDirection(event.stateId);
    const glm::ivec3 targetPosition = event.position + direction;

    if (useItemBehaviors) {
        const ItemDef& itemDef = ItemRegistry::get(stack.itemId);
        if (placeFluidFromBucket(world, registry, inventory, slot, stack, itemDef, targetPosition) ||
            pickupFluidIntoBucket(world, registry, inventory, slot, stack, itemDef, targetPosition) ||
            launchProjectile(registry, inventory, slot, stack, stack.itemId, event.position, direction)) {
            return true;
        }
    }

    ejectItem(registry, inventory, slot, stack, event.position, direction);
    return true;
}

bool executeNoteBlock(GameplayRegistry& registry, const glm::ivec3& position) {
    ensureAudioEventBus(registry).push({"block.note_block.harp", glm::vec3(position) + glm::vec3(0.5f), true, 1.0f});
    return true;
}

bool executeDeviceEvent(World& world, GameplayRegistry& registry, const RedstoneDeviceActivationEvent& event) {
    const BlockStateId currentState = world.getBlockState(event.position.x, event.position.y, event.position.z);
    if (currentState == NULL_BLOCK_STATE || BlockStateRegistry::getBlockId(currentState) != event.blockId) {
        failRedstoneDeviceActionSystem("Redstone device activation event does not match the current block state");
    }

    const BlockDef& blockDef = BlockRegistry::getFast(event.blockId);
    if (blockDef.redstoneBehavior == "note_block") {
        return executeNoteBlock(registry, event.position);
    }
    if (blockDef.redstoneBehavior == "dispenser") {
        return executeDispenser(world, registry, event, true);
    }
    if (blockDef.redstoneBehavior == "dropper") {
        return executeDispenser(world, registry, event, false);
    }
    failRedstoneDeviceActionSystem("Unsupported redstone device action: " + blockDef.redstoneBehavior);
}

} // namespace

void RedstoneDeviceActionSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }

    processEvents(*ctx.services.world, ctx.registry);
}

std::size_t RedstoneDeviceActionSystem::processEvents(World& world, GameplayRegistry& registry) {
    if (!registry.ctxHas<RedstoneDeviceActivationEventBus>()) {
        return 0;
    }

    std::vector<RedstoneDeviceActivationEvent> events = registry.ctxGet<RedstoneDeviceActivationEventBus>().drain();
    std::size_t actionCount = 0;
    for (const RedstoneDeviceActivationEvent& event : events) {
        if (executeDeviceEvent(world, registry, event)) {
            ++actionCount;
        }
    }
    return actionCount;
}

} // namespace ecs
