#include "BucketUseSystem.h"

#include <algorithm>

#include "../../components/Components.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../../client/GameClient.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../item/Item.h"
#include "../../../world/IWorldView.h"
#include "../../../world/World.h"
#include "../../../world/fluid/FluidState.h"

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

bool isWithinInteractionReach(const glm::vec3& playerPos, const glm::ivec3& blockPos) {
    constexpr float kMaxBucketDistance = 6.5f;
    const glm::vec3 blockCenter = glm::vec3(blockPos) + glm::vec3(0.5f);
    const glm::vec3 diff = playerPos - blockCenter;
    const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    return distSq <= kMaxBucketDistance * kMaxBucketDistance;
}

bool isSourceWaterAt(const IWorldView& worldView, const glm::ivec3& pos) {
    const StateID fluidState = worldView.getFluidState(pos.x, pos.y, pos.z);
    return FluidState::isWater(fluidState) && FluidState::isSource(fluidState);
}

bool canPlaceSourceWaterAt(const IWorldView& worldView, const glm::ivec3& pos) {
    if (!worldView.isChunkLoadedForBlock(pos.x, pos.y, pos.z)) {
        return false;
    }

    const StateID blockState = worldView.getBlockState(pos.x, pos.y, pos.z);
    if (FluidState::canWaterReplace(blockState)) {
        return true;
    }

    const BlockID blockId = BlockStateRegistry::getBlockId(blockState);
    return BlockRegistry::getFast(blockId).allowsFluidCoexistence;
}

void replaceSelectedItem(Inventory& inventory, const ItemID itemId) {
    ItemStack replacement;
    replacement.itemId = itemId;
    replacement.count = 1;
    replacement.durability = 0;
    inventory.setSlotStack(inventory.getSelectedSlot(), replacement);
}

void sendBucketAction(client::GameClient& client,
                      BlockInteractionRuntimeComponent& runtime,
                      const net::ClientBlockActionType actionType,
                      const BlockTargetComponent& target,
                      const glm::ivec3& actionBlock,
                      const glm::vec3& playerPosition,
                      const StateID fluidState) {
    net::ClientBlockAction action;
    action.sequence = ++runtime.heldItemSwingSequence;
    action.action = actionType;
    action.targetBlock = target.targetBlock;
    action.placeBlock = actionBlock;
    action.hitNormal = target.hitNormal;
    action.playerPosition = playerPosition;
    action.blockState = static_cast<uint16_t>(fluidState);
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
        const ItemID selectedItem = inventory.getSelectedItem();
        if (selectedItem != ItemIds::BUCKET && selectedItem != ItemIds::WATER_BUCKET) {
            continue;
        }

        if (selectedItem == ItemIds::BUCKET) {
            const glm::ivec3 pickupPos = target.targetBlock;
            if (!isWithinInteractionReach(transform.position, pickupPos) ||
                !isSourceWaterAt(worldView, pickupPos)) {
                continue;
            }

            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    sendBucketAction(*ctx.services.gameClient,
                                     runtime,
                                     net::ClientBlockActionType::BucketPickupWater,
                                     target,
                                     pickupPos,
                                     transform.position,
                                     BlockIds::AIR);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
                continue;
            }

            mutableWorld->setFluidState(pickupPos.x, pickupPos.y, pickupPos.z, BlockIds::AIR);
            if (modeRules.shouldReportBreakProgress()) {
                replaceSelectedItem(inventory, ItemIds::WATER_BUCKET);
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            audioBus.push({"item.bucket.fill", glm::vec3(pickupPos) + glm::vec3(0.5f), true, 1.0f});
            ++runtime.heldItemSwingSequence;
            continue;
        }

        const glm::ivec3 placePos = target.placeBlock;
        if (!isWithinInteractionReach(transform.position, placePos) ||
            !canPlaceSourceWaterAt(worldView, placePos)) {
            continue;
        }

        const StateID sourceWater = FluidState::makeWater(0, false);
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                sendBucketAction(*ctx.services.gameClient,
                                 runtime,
                                 net::ClientBlockActionType::BucketPlaceWater,
                                 target,
                                 placePos,
                                 transform.position,
                                 sourceWater);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            continue;
        }

        mutableWorld->setFluidState(placePos.x, placePos.y, placePos.z, sourceWater);
        if (modeRules.shouldReportBreakProgress()) {
            replaceSelectedItem(inventory, ItemIds::BUCKET);
        }
        runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
        audioBus.push({"item.bucket.empty", glm::vec3(placePos) + glm::vec3(0.5f), true, 1.0f});
        ++runtime.heldItemSwingSequence;
    }
}

} // namespace ecs
