#include "FurnaceInventoryLifecycle.h"

#include "../../ecs/GameplayRegistry.h"
#include "../../ecs/systems/item/ItemSpawnSystem.h"
#include "../../world/block/BlockStateRegistry.h"
#include "FurnaceInventoryStore.h"

namespace {
BlockID furnaceBlockId() {
    return BlockRegistry::findByName("minecraft:furnace");
}
}

bool handleFurnaceInventoryBreak(ecs::GameplayRegistry& registry,
                                 const BlockID brokenBlock,
                                 const glm::ivec3& blockPos,
                                 const bool dropContents) {
    if (BlockStateRegistry::getBlockId(brokenBlock) != furnaceBlockId()) {
        return false;
    }
    if (!registry.ctxHas<FurnaceInventoryStore>()) {
        return true;
    }

    FurnaceInventoryStore& store = registry.ctxGet<FurnaceInventoryStore>();
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
