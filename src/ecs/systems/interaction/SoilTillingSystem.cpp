#include "SoilTillingSystem.h"

#include "../../components/Components.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/ToolDurability.h"
#include "../../../client/GameClient.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../item/Item.h"
#include "../../../world/IWorldView.h"
#include "../../../world/World.h"
#include "../../../world/block/BlockStateRegistry.h"

namespace ecs {

namespace {

const IGameplayModeRules& resolveModeRules(const GameplayRegistry& registry) {
    if (registry.ctxHas<GameplayRuntimeContext>()) {
        const auto& runtime = registry.ctxGet<GameplayRuntimeContext>();
        if (runtime.modeRules != nullptr) {
            return *runtime.modeRules;
        }
    }
    return SurvivalModeRules::instance();
}

bool hasEmptySpaceAbove(const IWorldView& worldView, const glm::ivec3& pos) {
    const glm::ivec3 above = pos + glm::ivec3(0, 1, 0);
    return worldView.getBlockState(above.x, above.y, above.z) == RUNTIME_ID_NULL &&
           worldView.getFluidState(above.x, above.y, above.z) == RUNTIME_ID_NULL;
}

bool isWithinInteractionReach(const glm::vec3& playerPos, const glm::ivec3& blockPos) {
    constexpr float kMaxTillDistance = 6.5f;
    const glm::vec3 blockCenter = glm::vec3(blockPos) + glm::vec3(0.5f);
    const glm::vec3 diff = playerPos - blockCenter;
    const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    return distSq <= kMaxTillDistance * kMaxTillDistance;
}

} // namespace

void SoilTillingSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) {
        return;
    }

    const auto& worldView = *ctx.services.worldView;
    World* mutableWorld = ctx.services.world.get();
    auto& registry = ctx.registry;
    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioBus = ensureAudioEventBus(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              TransformComponent,
                              InventoryComponent,
                              InventoryDataComponent,
                              BlockTargetComponent,
                              BlockInteractionRuntimeComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);
        const auto& inventoryState = view.get<InventoryComponent>(e);
        auto& inventoryData = view.get<InventoryDataComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);

        inventoryData.inventory.setSelectedSlot(inventoryState.selectedHotbarSlot);

        if (!intent.wantsPlace || intent.wantsBreak || !target.hasTarget) {
            continue;
        }
        if (runtime.placeCooldownRemaining > 0.0f) {
            continue;
        }

        Inventory& inventory = inventoryData.inventory;
        const ItemStack selectedStack = inventory.getSelectedStack();
        if (selectedStack.isEmpty()) {
            continue;
        }
        const ItemDef& selectedItemDef = ItemRegistry::get(selectedStack.itemId);
        const ItemUseRule* tillRule = ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::TillSoil);
        if (tillRule == nullptr) {
            continue;
        }

        const glm::ivec3 tillPos = target.targetBlock;
        if (!isWithinInteractionReach(transform.position, tillPos)) {
            continue;
        }

        const StateID targetState = worldView.getBlockState(tillPos.x, tillPos.y, tillPos.z);
        const BlockID targetBlock = BlockStateRegistry::getBlockId(targetState);
        if (!ItemUseRules::matchesBlock(*tillRule, targetBlock) || !hasEmptySpaceAbove(worldView, tillPos)) {
            continue;
        }

        const StateID resultState = BlockStateRegistry::getDefaultState(tillRule->resultBlock);
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                net::ClientBlockAction action;
                action.sequence = ++runtime.heldItemSwingSequence;
                action.action = net::ClientBlockActionType::Till;
                action.targetBlock = tillPos;
                action.placeBlock = tillPos;
                action.hitNormal = target.hitNormal;
                action.playerPosition = transform.position;
                action.blockState = static_cast<uint16_t>(resultState);
                ctx.services.gameClient->sendBlockAction(action);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            continue;
        }

        mutableWorld->setBlockState(tillPos.x, tillPos.y, tillPos.z, resultState);
        if (modeRules.shouldReportBreakProgress()) {
            for (uint16_t i = 0; i < tillRule->consumeDurability; ++i) {
                applySelectedToolDurabilityWear(inventory);
            }
        }
        runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
        audioBus.push({"block.generic.place", glm::vec3(tillPos), true, 1.0f});
        ++runtime.heldItemSwingSequence;
    }
}

} // namespace ecs
