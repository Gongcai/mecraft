#include "BucketUseSystem.h"

#include "../../components/Components.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../../client/GameClient.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../item/Item.h"
#include "../../../item/ItemUseDispatcher.h"
#include "../../../world/IWorldView.h"
#include "../../../world/World.h"

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

void replaceSelectedItem(Inventory& inventory, const ItemID itemId) {
    ItemStack replacement;
    replacement.itemId = itemId;
    replacement.count = 1;
    replacement.durability = 0;
    inventory.setSlotStack(inventory.getSelectedSlot(), replacement);
}

void sendBucketAction(client::GameClient& client, BlockInteractionRuntimeComponent& runtime,
                      const net::ClientBlockActionType actionType, const glm::ivec3& targetBlock,
                      const glm::ivec3& placeBlock, const glm::ivec3& hitNormal, const glm::vec3& playerPosition,
                      const BlockStateId fluidState) {
    net::ClientBlockAction action;
    action.sequence = ++runtime.heldItemSwingSequence;
    action.action = actionType;
    action.targetBlock = targetBlock;
    action.placeBlock = placeBlock;
    action.hitNormal = hitNormal;
    action.playerPosition = playerPosition;
    action.blockState = fluidState;
    client.sendBlockAction(action);
}

} // namespace

void BucketUseSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) {
        return;
    }

    const IWorldView& worldView = *ctx.services.worldView;
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

        if (!intent.wantsPlace || intent.wantsBreak) {
            continue;
        }
        if (runtime.placeCooldownRemaining > 0.0f) {
            continue;
        }

        Inventory& inventory = inventoryData.inventory;
        const ItemID selectedItem = inventory.getSelectedItem();
        const ItemDef& selectedItemDef = ItemRegistry::get(selectedItem);
        const ItemUseRule* pickupRule = ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::BucketPickupFluid);
        const ItemUseRule* placeRule = ItemUseRules::findRule(selectedItemDef, ItemUseBehavior::BucketPlaceFluid);
        if (pickupRule == nullptr && placeRule == nullptr) {
            continue;
        }

        if (pickupRule != nullptr) {
            if (!target.hasFluidTarget) {
                continue;
            }

            const glm::ivec3 pickupPos = target.fluidTargetBlock;
            if (!ItemUseDispatcher::isWithinReach(transform.position, pickupPos) ||
                !ItemUseDispatcher::canPickupFluid(worldView, pickupPos, *pickupRule)) {
                continue;
            }

            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    sendBucketAction(*ctx.services.gameClient, runtime, net::ClientBlockActionType::BucketPickupWater,
                                     pickupPos, target.fluidPlaceBlock, target.fluidHitNormal, transform.position,
                                     NULL_BLOCK_STATE);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
                continue;
            }

            mutableWorld->setFluidState(pickupPos.x, pickupPos.y, pickupPos.z, NULL_BLOCK_STATE);
            if (modeRules.shouldReportBreakProgress()) {
                replaceSelectedItem(inventory, pickupRule->resultItem);
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            audioBus.push({"item.bucket.fill", glm::vec3(pickupPos) + glm::vec3(0.5f), true, 1.0f});
            ++runtime.heldItemSwingSequence;
            continue;
        }

        if (!target.hasTarget) {
            continue;
        }

        const glm::ivec3 placePos = target.placeBlock;
        if (!ItemUseDispatcher::isWithinReach(transform.position, placePos) ||
            !ItemUseDispatcher::canPlaceFluid(worldView, placePos, *placeRule)) {
            continue;
        }

        const BlockStateId sourceFluid = ItemUseDispatcher::makeSourceFluidState(placeRule->resultBlock);
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                sendBucketAction(*ctx.services.gameClient, runtime, net::ClientBlockActionType::BucketPlaceWater,
                                 target.targetBlock, placePos, target.hitNormal, transform.position, sourceFluid);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            continue;
        }

        mutableWorld->setFluidState(placePos.x, placePos.y, placePos.z, sourceFluid);
        if (modeRules.shouldReportBreakProgress()) {
            replaceSelectedItem(inventory, placeRule->resultItem);
        }
        runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
        audioBus.push({"item.bucket.empty", glm::vec3(placePos) + glm::vec3(0.5f), true, 1.0f});
        ++runtime.heldItemSwingSequence;
    }
}

} // namespace ecs
