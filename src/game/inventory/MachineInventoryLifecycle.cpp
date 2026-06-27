#include "MachineInventoryLifecycle.h"

#include <stdexcept>

#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/systems/item/ItemSpawnSystem.h"
#include "../../ui/inventory/ContainerUiRegistry.h"
#include "ContainerBehaviorRegistry.h"
#include "MachineInventoryStore.h"

namespace {
const ContainerBehaviorDef* machineBehaviorForBlock(const BlockID blockId) {
    if (blockId == RUNTIME_ID_NULL) {
        return nullptr;
    }

    const BlockDef& blockDef = BlockRegistry::getFast(blockId);
    if (blockDef.containerUi.empty()) {
        return nullptr;
    }

    const ui::ContainerUiDef& uiDef = ui::ContainerUiRegistry::require(blockDef.containerUi);
    const ContainerBehaviorDef& behavior = ContainerBehaviorRegistry::require(uiDef.behavior);
    if (behavior.handler != "smelting") {
        return nullptr;
    }
    if (behavior.storage.kind != ContainerStorageKind::BlockEntity) {
        throw std::runtime_error(behavior.id + " machine handler requires block_entity storage");
    }
    return &behavior;
}
}

bool handleMachineInventoryBreak(ecs::GameplayRegistry& registry,
                                 const BlockID brokenBlock,
                                 const glm::ivec3& blockPos,
                                 const bool dropContents) {
    const ContainerBehaviorDef* behavior = machineBehaviorForBlock(brokenBlock);
    if (behavior == nullptr) {
        return false;
    }
    if (!registry.ctxHas<MachineInventoryStore>()) {
        return true;
    }

    MachineInventoryStore& store = registry.ctxGet<MachineInventoryStore>();
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
