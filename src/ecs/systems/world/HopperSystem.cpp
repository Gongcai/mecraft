#include "HopperSystem.h"

#include "../../GameplayRegistry.h"
#include "../../components/Components.h"
#include "../../../crafting/SmeltingSystem.h"
#include "../../../game/inventory/BlockEntityInventoryStore.h"
#include "../../../game/inventory/ContainerBehaviorRegistry.h"
#include "../../../game/inventory/MachineInventoryStore.h"
#include "../../../ui/inventory/ContainerUiRegistry.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/chunk/Chunk.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace ecs {
namespace {

[[noreturn]] void failHopperSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

constexpr const char* kHopperBehaviorId = "minecraft:hopper";
constexpr int kHopperSlotCount = 5;
constexpr std::uint64_t kHopperTransferIntervalTicks = 8;

enum class InventoryKind : std::uint8_t {
    Storage,
    Machine
};

struct InventoryAccess {
    InventoryKind kind = InventoryKind::Storage;
    BlockEntityInventory* storage = nullptr;
    MachineInventory* machine = nullptr;
    const ContainerBehaviorDef* behavior = nullptr;
    int slotCount = 0;
};

bool positionLess(const glm::ivec3& lhs, const glm::ivec3& rhs) {
    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    if (lhs.z != rhs.z) {
        return lhs.z < rhs.z;
    }
    return lhs.x < rhs.x;
}

int blockToChunkCoord(const int value, const int chunkSize) {
    const int quotient = value / chunkSize;
    const int remainder = value % chunkSize;
    return remainder < 0 ? quotient - 1 : quotient;
}

bool blockPositionTicks(const World& world, const glm::ivec3& position) {
    if (!world.isChunkLoadedForBlock(position.x, position.y, position.z)) {
        return false;
    }

    const int chunkX = blockToChunkCoord(position.x, Chunk::SIZE_X);
    const int chunkZ = blockToChunkCoord(position.z, Chunk::SIZE_Z);
    return world.ticketManager().shouldTick(chunkX, chunkZ);
}

bool positionTicks(const World& world, const glm::vec3& position) {
    const int blockX = static_cast<int>(std::floor(position.x));
    const int blockY = static_cast<int>(std::floor(position.y));
    const int blockZ = static_cast<int>(std::floor(position.z));
    if (!world.isChunkLoadedForBlock(blockX, blockY, blockZ)) {
        return false;
    }

    const int chunkX = blockToChunkCoord(blockX, Chunk::SIZE_X);
    const int chunkZ = blockToChunkCoord(blockZ, Chunk::SIZE_Z);
    return world.ticketManager().shouldTick(chunkX, chunkZ);
}

uint16_t requiredProperty(const BlockStateId stateId, const uint16_t property, const char* propertyName) {
    if (property == PropIndices::INVALID) {
        failHopperSystem(std::string("Hopper state requires registered property: ") + propertyName);
    }

    const uint16_t value = BlockStateRegistry::getPropertyIndex(stateId, property);
    if (value == BlockStateRegistry::INVALID_INDEX) {
        failHopperSystem(std::string("Hopper state is missing property: ") + propertyName);
    }
    return value;
}

bool isHopperState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "hopper";
}

bool hopperEnabled(const BlockStateId stateId) {
    const uint16_t enabled = requiredProperty(stateId, PropIndices::ENABLED, "enabled");
    if (PropIndices::ENABLED_TRUE == PropIndices::INVALID ||
        PropIndices::ENABLED_FALSE == PropIndices::INVALID) {
        failHopperSystem("Hopper state requires registered enabled boolean values");
    }
    if (enabled != PropIndices::ENABLED_TRUE && enabled != PropIndices::ENABLED_FALSE) {
        failHopperSystem("Hopper enabled property must be a boolean value");
    }
    return enabled == PropIndices::ENABLED_TRUE;
}

glm::ivec3 hopperFacingDirection(const BlockStateId stateId) {
    const uint16_t facing = requiredProperty(stateId, PropIndices::FACING, "facing");
    if (facing == PropIndices::FACING_DOWN) {
        return {0, -1, 0};
    }
    if (facing == PropIndices::FACING_NORTH) {
        return {0, 0, -1};
    }
    if (facing == PropIndices::FACING_SOUTH) {
        return {0, 0, 1};
    }
    if (facing == PropIndices::FACING_EAST) {
        return {1, 0, 0};
    }
    if (facing == PropIndices::FACING_WEST) {
        return {-1, 0, 0};
    }
    failHopperSystem("Hopper state contains an unsupported facing value");
}

const ContainerSlotRuleDef* findSlotRule(const ContainerBehaviorDef& behavior, const int slot) {
    for (const ContainerSlotRuleDef& rule : behavior.slotRules) {
        if (rule.slot == slot) {
            return &rule;
        }
    }
    return nullptr;
}

const ContainerSlotRuleDef* slotRuleForConfiguredSlot(const ContainerBehaviorDef& behavior, const int slot) {
    const ContainerSlotRuleDef* rule = findSlotRule(behavior, slot);
    if (rule != nullptr || behavior.slotRules.empty()) {
        return rule;
    }
    failHopperSystem(behavior.id + " is missing a slot rule for slot " + std::to_string(slot));
}

const SmeltingSystem& requireSmeltingSystem(GameplayRegistry& registry, const std::string& behaviorId) {
    if (!registry.ctxHas<SmeltingSystem>()) {
        failHopperSystem(behaviorId + " hopper transfer requires loaded smelting recipes");
    }
    return registry.ctxGet<SmeltingSystem>();
}

bool slotAcceptsItem(GameplayRegistry& registry,
                     const ContainerBehaviorDef& behavior,
                     const int slot,
                     const ItemID itemId) {
    if (itemId == RUNTIME_ID_NULL) {
        return false;
    }

    const ContainerSlotRuleDef* rule = slotRuleForConfiguredSlot(behavior, slot);
    if (rule == nullptr) {
        return true;
    }
    if (rule->outputOnly) {
        return false;
    }
    if (rule->accepts == "any") {
        return true;
    }

    const SmeltingSystem& smelting = requireSmeltingSystem(registry, behavior.id);
    if (rule->accepts == "fuel") {
        return smelting.isFuel(itemId);
    }
    if (rule->accepts == "smelting_input") {
        return smelting.findRecipe(itemId) != nullptr;
    }

    failHopperSystem(behavior.id + " has an unsupported slot accepts rule: " + rule->accepts);
}

bool hasOutputOnlySlot(const ContainerBehaviorDef& behavior) {
    for (const ContainerSlotRuleDef& rule : behavior.slotRules) {
        if (rule.outputOnly) {
            return true;
        }
    }
    return false;
}

bool slotCanExtract(const ContainerBehaviorDef& behavior, const int slot) {
    if (!hasOutputOnlySlot(behavior)) {
        return true;
    }

    const ContainerSlotRuleDef* rule = slotRuleForConfiguredSlot(behavior, slot);
    return rule != nullptr && rule->outputOnly;
}

BlockEntityInventoryStore& ensureStorageStore(GameplayRegistry& registry) {
    return registry.ctxHas<BlockEntityInventoryStore>()
        ? registry.ctxGet<BlockEntityInventoryStore>()
        : registry.ctxSet<BlockEntityInventoryStore>();
}

MachineInventoryStore& ensureMachineStore(GameplayRegistry& registry) {
    return registry.ctxHas<MachineInventoryStore>()
        ? registry.ctxGet<MachineInventoryStore>()
        : registry.ctxSet<MachineInventoryStore>();
}

std::optional<InventoryAccess> containerAt(World& world,
                                           GameplayRegistry& registry,
                                           const glm::ivec3& position) {
    if (!world.isChunkLoadedForBlock(position.x, position.y, position.z)) {
        return std::nullopt;
    }

    const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
    if (stateId == NULL_BLOCK_STATE) {
        return std::nullopt;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return std::nullopt;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        return std::nullopt;
    }

    if (behavior.handler == "storage") {
        BlockEntityInventory& inventory =
            ensureStorageStore(registry).getOrCreate(position, behavior.id, behavior.storage.slots);
        return InventoryAccess{InventoryKind::Storage, &inventory, nullptr, &behavior, behavior.storage.slots};
    }
    if (behavior.handler == "smelting") {
        MachineInventory& inventory =
            ensureMachineStore(registry).getOrCreate(position, behavior.id, behavior.storage.slots);
        return InventoryAccess{InventoryKind::Machine, nullptr, &inventory, &behavior, behavior.storage.slots};
    }
    if (behavior.handler == "crafting") {
        return std::nullopt;
    }

    failHopperSystem("Unsupported hopper container handler: " + behavior.handler);
}

ItemStack getSlotStack(const InventoryAccess& inventory, const int slot) {
    if (slot < 0 || slot >= inventory.slotCount) {
        failHopperSystem("Hopper transfer slot index is outside inventory bounds");
    }
    if (inventory.kind == InventoryKind::Storage) {
        return inventory.storage->getSlotStack(slot);
    }
    return inventory.machine->getSlotStack(slot);
}

void setSlotStack(const InventoryAccess& inventory, const int slot, const ItemStack& stack) {
    if (slot < 0 || slot >= inventory.slotCount) {
        failHopperSystem("Hopper transfer slot index is outside inventory bounds");
    }
    if (inventory.kind == InventoryKind::Storage) {
        inventory.storage->setSlotStack(slot, stack);
        return;
    }
    inventory.machine->setSlotStack(slot, stack);
}

bool insertOneItem(GameplayRegistry& registry,
                   const InventoryAccess& target,
                   const ItemID itemId,
                   const uint16_t durability) {
    if (target.behavior == nullptr) {
        failHopperSystem("Hopper transfer target has no container behavior");
    }
    if (itemId == RUNTIME_ID_NULL) {
        return false;
    }

    const ItemDef& itemDef = ItemRegistry::get(itemId);
    if (itemDef.maxStack == 0) {
        return false;
    }

    for (int slot = 0; slot < target.slotCount; ++slot) {
        if (!slotAcceptsItem(registry, *target.behavior, slot, itemId)) {
            continue;
        }

        ItemStack stack = getSlotStack(target, slot);
        if (stack.isEmpty() ||
            stack.itemId != itemId ||
            stack.durability != durability ||
            stack.count >= itemDef.maxStack) {
            continue;
        }

        ++stack.count;
        setSlotStack(target, slot, stack);
        return true;
    }

    for (int slot = 0; slot < target.slotCount; ++slot) {
        if (!slotAcceptsItem(registry, *target.behavior, slot, itemId)) {
            continue;
        }

        if (!getSlotStack(target, slot).isEmpty()) {
            continue;
        }

        ItemStack stack;
        stack.itemId = itemId;
        stack.count = 1;
        stack.durability = durability;
        setSlotStack(target, slot, stack);
        return true;
    }

    return false;
}

bool moveOneItem(GameplayRegistry& registry,
                 const InventoryAccess& source,
                 const int sourceSlot,
                 const InventoryAccess& target) {
    if (source.behavior == nullptr) {
        failHopperSystem("Hopper transfer source has no container behavior");
    }
    if (!slotCanExtract(*source.behavior, sourceSlot)) {
        return false;
    }

    ItemStack stack = getSlotStack(source, sourceSlot);
    if (stack.isEmpty()) {
        return false;
    }
    if (!insertOneItem(registry, target, stack.itemId, stack.durability)) {
        return false;
    }

    --stack.count;
    setSlotStack(source, sourceSlot, stack);
    return true;
}

bool moveFirstAvailableItem(GameplayRegistry& registry,
                            const InventoryAccess& source,
                            const InventoryAccess& target) {
    for (int slot = 0; slot < source.slotCount; ++slot) {
        if (moveOneItem(registry, source, slot, target)) {
            return true;
        }
    }
    return false;
}

bool aabbIntersects(const glm::vec3& minA,
                    const glm::vec3& maxA,
                    const glm::vec3& minB,
                    const glm::vec3& maxB) {
    return minA.x < maxB.x && maxA.x > minB.x &&
           minA.y < maxB.y && maxA.y > minB.y &&
           minA.z < maxB.z && maxA.z > minB.z;
}

bool dropIntersectsHopperInput(const glm::ivec3& hopperPosition,
                               const TransformComponent& transform,
                               const BoundsComponent& bounds) {
    const glm::vec3 hopperMin = glm::vec3(hopperPosition);
    const glm::vec3 hopperMax =
        glm::vec3(hopperPosition) + glm::vec3(1.0f, 1.5f, 1.0f);
    const glm::vec3 dropMin = transform.position - bounds.halfExtents;
    const glm::vec3 dropMax = transform.position + bounds.halfExtents;
    return aabbIntersects(hopperMin, hopperMax, dropMin, dropMax);
}

bool tryPickupDropEntity(World& world,
                         GameplayRegistry& registry,
                         const glm::ivec3& hopperPosition,
                         const InventoryAccess& hopper) {
    auto view = registry.view<DropItemTag, TransformComponent, BoundsComponent, ItemComponent>();
    std::vector<entt::entity> candidates;

    for (const entt::entity entity : view) {
        const TransformComponent& transform = view.get<TransformComponent>(entity);
        const BoundsComponent& bounds = view.get<BoundsComponent>(entity);
        const ItemComponent& item = view.get<ItemComponent>(entity);
        if (item.itemId == RUNTIME_ID_NULL || item.stackCount == 0) {
            continue;
        }
        if (!positionTicks(world, transform.position)) {
            continue;
        }
        if (!dropIntersectsHopperInput(hopperPosition, transform, bounds)) {
            continue;
        }
        candidates.push_back(entity);
    }

    std::sort(candidates.begin(), candidates.end(), [&](const entt::entity lhs, const entt::entity rhs) {
        const glm::vec3& left = view.get<TransformComponent>(lhs).position;
        const glm::vec3& right = view.get<TransformComponent>(rhs).position;
        if (left.y != right.y) {
            return left.y < right.y;
        }
        if (left.z != right.z) {
            return left.z < right.z;
        }
        if (left.x != right.x) {
            return left.x < right.x;
        }
        return static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs);
    });

    for (const entt::entity entity : candidates) {
        if (!registry.registry().valid(entity)) {
            continue;
        }
        ItemComponent& item = view.get<ItemComponent>(entity);
        if (item.itemId == RUNTIME_ID_NULL || item.stackCount == 0) {
            continue;
        }
        if (!insertOneItem(registry, hopper, item.itemId, 0)) {
            continue;
        }

        --item.stackCount;
        if (item.stackCount == 0) {
            registry.destroy(entity);
        }
        return true;
    }

    return false;
}

bool tryPushFromHopper(World& world,
                       GameplayRegistry& registry,
                       const glm::ivec3& hopperPosition,
                       const BlockStateId hopperState,
                       const InventoryAccess& hopper) {
    const glm::ivec3 targetPosition = hopperPosition + hopperFacingDirection(hopperState);
    std::optional<InventoryAccess> target = containerAt(world, registry, targetPosition);
    if (!target.has_value()) {
        return false;
    }

    return moveFirstAvailableItem(registry, hopper, *target);
}

bool tryPullIntoHopper(World& world,
                       GameplayRegistry& registry,
                       const glm::ivec3& hopperPosition,
                       const InventoryAccess& hopper) {
    const glm::ivec3 sourcePosition = hopperPosition + glm::ivec3(0, 1, 0);
    std::optional<InventoryAccess> source = containerAt(world, registry, sourcePosition);
    if (source.has_value() && moveFirstAvailableItem(registry, *source, hopper)) {
        return true;
    }

    return tryPickupDropEntity(world, registry, hopperPosition, hopper);
}

std::vector<glm::ivec3> collectHopperPositions(BlockEntityInventoryStore& store) {
    std::vector<glm::ivec3> positions;
    store.forEach([&positions](const glm::ivec3& position,
                               const std::string& typeId,
                               const int slotCount,
                               const BlockEntityInventory&) {
        if (typeId != kHopperBehaviorId) {
            return;
        }
        if (slotCount != kHopperSlotCount) {
            failHopperSystem("Hopper inventory has an invalid slot count");
        }
        positions.push_back(position);
    });
    std::sort(positions.begin(), positions.end(), positionLess);
    return positions;
}

} // namespace

void HopperSystem::update(SystemContext& ctx) {
    if (!ctx.services.world) {
        return;
    }
    if (ctx.services.gameClient) {
        return;
    }

    processWorld(*ctx.services.world, ctx.registry, ctx.tickIndex);
}

std::size_t HopperSystem::processWorld(World& world,
                                       GameplayRegistry& registry,
                                       const std::uint64_t gameTick) {
    if ((gameTick % kHopperTransferIntervalTicks) != 0u) {
        return 0;
    }
    if (!registry.ctxHas<BlockEntityInventoryStore>()) {
        return 0;
    }

    BlockEntityInventoryStore& store = registry.ctxGet<BlockEntityInventoryStore>();
    const std::vector<glm::ivec3> hopperPositions = collectHopperPositions(store);

    std::size_t transfers = 0;
    for (const glm::ivec3& position : hopperPositions) {
        if (!blockPositionTicks(world, position)) {
            continue;
        }

        const BlockStateId stateId = world.getBlockState(position.x, position.y, position.z);
        if (!isHopperState(stateId)) {
            failHopperSystem("Hopper inventory is not backed by a hopper block");
        }
        if (!hopperEnabled(stateId)) {
            continue;
        }

        BlockEntityInventory& hopperInventory =
            store.getOrCreate(position, kHopperBehaviorId, kHopperSlotCount);
        const ContainerBehaviorDef& hopperBehavior = ContainerBehaviorRegistry::require(kHopperBehaviorId);
        InventoryAccess hopper{
            InventoryKind::Storage,
            &hopperInventory,
            nullptr,
            &hopperBehavior,
            kHopperSlotCount
        };

        if (tryPushFromHopper(world, registry, position, stateId, hopper) ||
            tryPullIntoHopper(world, registry, position, hopper)) {
            ++transfers;
        }
    }

    return transfers;
}

} // namespace ecs
