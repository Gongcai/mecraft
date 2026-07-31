#include "SoilTillingSystem.h"

#include "../../components/Components.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/ToolDurability.h"
#include "../../../client/GameClient.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../item/Item.h"
#include "../../../item/ItemUseDispatcher.h"
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

    auto view = registry.view<LocalPlayerTag, BlockActionIntentComponent, TransformComponent, InventoryComponent,
                              InventoryDataComponent, BlockTargetComponent, BlockInteractionRuntimeComponent>();
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
        if (!ItemUseDispatcher::isWithinReach(transform.position, tillPos)) {
            continue;
        }

        if (!ItemUseDispatcher::canApplyBlockRule(worldView, tillPos, *tillRule)) {
            continue;
        }

        const BlockStateId resultState = BlockStateRegistry::getDefaultState(tillRule->resultBlock);
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                net::ClientBlockAction action;
                action.sequence = ++runtime.heldItemSwingSequence;
                action.action = net::ClientBlockActionType::Till;
                action.targetBlock = tillPos;
                action.placeBlock = tillPos;
                action.hitNormal = target.hitNormal;
                action.playerPosition = transform.position;
                action.blockState = resultState;
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
