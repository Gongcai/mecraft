#include "ChestInventoryLifecycle.h"

#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/systems/item/ItemSpawnSystem.h"
#include "../../item/Item.h"
#include "../../world/block/BlockStateRegistry.h"
#include "ChestInventoryStore.h"

bool handleChestInventoryBreak(ecs::GameplayRegistry& registry,
                               const BlockID brokenBlock,
                               const glm::ivec3& blockPos,
                               const bool dropContents) {
    if (BlockStateRegistry::getBlockId(brokenBlock) != BlockIds::CHEST) {
        return false;
    }
    if (!registry.ctxHas<ChestInventoryStore>()) {
        return true;
    }

    ChestInventoryStore& store = registry.ctxGet<ChestInventoryStore>();
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
