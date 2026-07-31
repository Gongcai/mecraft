#include "BlockEntityInventoryLifecycle.h"

#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/systems/item/ItemSpawnSystem.h"
#include "../../item/Item.h"
#include "../../ui/inventory/ContainerUiRegistry.h"
#include "BlockEntityInventoryStore.h"
#include "ContainerBehaviorRegistry.h"

#include <cstdlib>
#include <iostream>

namespace {
const ContainerBehaviorDef* storageBehaviorForBlock(const BlockID blockId) {
    if (blockId == RUNTIME_ID_NULL) {
        return nullptr;
    }

    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return nullptr;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.handler != "storage") {
        return nullptr;
    }
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        std::cerr << behavior.id << " storage handler requires block_entity storage\n";
        std::abort();
    }
    return &behavior;
}
} // namespace

bool handleBlockEntityInventoryBreak(ecs::GameplayRegistry& registry, const BlockID brokenBlock,
                                     const glm::ivec3& blockPos, const bool dropContents) {
    const ContainerBehaviorDef* behavior = storageBehaviorForBlock(brokenBlock);
    if (behavior == nullptr) {
        return false;
    }
    if (!registry.ctxHas<BlockEntityInventoryStore>()) {
        return true;
    }

    BlockEntityInventoryStore& store = registry.ctxGet<BlockEntityInventoryStore>();
    const auto contents = store.extractAndErase(blockPos);
    if (!dropContents) {
        return true;
    }

    for (const ItemStack& stack : contents) {
        if (stack.isEmpty()) {
            continue;
        }
        ecs::ItemSpawnSystem::spawn(registry, stack.itemId, blockPos, stack.count);
    }

    return true;
}

bool ensureBlockEntityInventoryForPlacedBlock(ecs::GameplayRegistry& registry, const BlockID placedBlock,
                                              const glm::ivec3& blockPos) {
    const ContainerBehaviorDef* behavior = storageBehaviorForBlock(placedBlock);
    if (behavior == nullptr) {
        return false;
    }

    BlockEntityInventoryStore& store = registry.ctxHas<BlockEntityInventoryStore>()
                                           ? registry.ctxGet<BlockEntityInventoryStore>()
                                           : registry.ctxSet<BlockEntityInventoryStore>();
    static_cast<void>(store.getOrCreate(blockPos, behavior->id, behavior->storage.slots));
    return true;
}
