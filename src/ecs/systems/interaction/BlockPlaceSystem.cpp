#include "BlockPlaceSystem.h"

#include <algorithm>

#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../client/GameClient.h"
#include "../../../world/IWorldView.h"
#include "../../../world/block/BlockCollision.h"
#include "../../../world/block/Placement.h"
#include "../../../world/World.h"
#include "../../../world/DropSystem.h"
#include "../../../item/Item.h"

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

bool wouldOverlapPlacedState(const PhysicsBody& body, const glm::ivec3& blockPos, const StateID stateId) {
    const glm::vec3 bodyCenter = body.position + body.colliderOffset;
    const glm::vec3 bodyMin = bodyCenter - body.halfExtents;
    const glm::vec3 bodyMax = bodyCenter + body.halfExtents;

    return BlockCollision::intersects(stateId, blockPos, bodyMin, bodyMax);
}

void recordPostPlaceSuppression(BlockInteractionRuntimeComponent& runtime,
                                const glm::ivec3& placedBlock) {
    runtime.recentlyPlacedBlock = placedBlock;
    runtime.postPlaceInteractionSuppressSeconds = 0.25f;
}

BlockID resolvePlacementState(const BlockID blockId,
                              const CameraStateComponent& camera,
                              const MoveIntentComponent& moveIntent,
                              const glm::ivec3& hitNormal,
                              const glm::vec3& hitPosition) {
    const BlockDef& def = BlockRegistry::get(blockId);
    PlacementStrategyFn strategy = PlacementStrategyRegistry::getStrategy(def.placementStrategy);
    if (strategy == nullptr) {
        return BlockStateRegistry::getDefaultState(blockId);
    }

    PlacementContext pctx;
    pctx.blockId = blockId;
    pctx.hitNormal = hitNormal;
    pctx.hitPosition = hitPosition;
    pctx.playerYaw = camera.yaw;
    pctx.isSneaking = moveIntent.wantsCrouch;

    const StateID stateId = strategy(pctx);
    return stateId != 0 ? stateId : BlockIds::AIR;
}

} // namespace

void BlockPlaceSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) return;
    const auto& worldView = *ctx.services.worldView;
    World* mutableWorld = ctx.services.world.get();
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioBus = ensureAudioEventBus(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              MoveIntentComponent,
                              TransformComponent,
                              PhysicsBodyComponent,
                              CameraStateComponent,
                              InventoryComponent,
                              InventoryDataComponent,
                              BlockTargetComponent,
                              BlockInteractionRuntimeComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);
        const auto& camera = view.get<CameraStateComponent>(e);
        const auto& moveIntent = view.get<MoveIntentComponent>(e);
        const auto& physicsBody = view.get<PhysicsBodyComponent>(e);
        auto& inventoryState = view.get<InventoryComponent>(e);
        auto& inventoryData = view.get<InventoryDataComponent>(e);

        runtime.placeCooldownRemaining = std::max(0.0f, runtime.placeCooldownRemaining - dt);
        runtime.postPlaceInteractionSuppressSeconds =
            std::max(0.0f, runtime.postPlaceInteractionSuppressSeconds - dt);

        // Sync selected slot to inventory
        inventoryData.inventory.setSelectedSlot(inventoryState.selectedHotbarSlot);

        if (!intent.wantsPlace || !target.hasTarget) {
            continue;
        }

        // Server-side distance validation: player must be within reach
        const glm::ivec3 placeBlock = target.placeBlock;
        constexpr float kMaxPlaceDistance = 6.5f;
        const glm::vec3 blockCenter = glm::vec3(placeBlock) + glm::vec3(0.5f);
        const glm::vec3 playerPos = view.get<TransformComponent>(e).position;
        const glm::vec3 diff = playerPos - blockCenter;
        const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        if (distSq > kMaxPlaceDistance * kMaxPlaceDistance) {
            continue;
        }

        Inventory& inventory = inventoryData.inventory;
        const ItemID selectedItem = inventory.getSelectedItem();
        const BlockID blockToPlace = ItemRegistry::toPlaceBlock(selectedItem);
        BlockID placedState = BlockIds::AIR;
        if (blockToPlace != 0) {
            placedState = resolvePlacementState(blockToPlace, camera, moveIntent, target.hitNormal, target.hitPosition);
        }

        GameplayBlockActionRequest request;
        request.hasHit = target.hasTarget;
        request.wantsBreak = intent.wantsBreak;
        request.wantsPlace = intent.wantsPlace;
        request.placeCooldownRemaining = runtime.placeCooldownRemaining;
        if (target.hasTarget) {
            request.targetBlock = worldView.getBlock(placeBlock.x, placeBlock.y, placeBlock.z);
            request.playerWouldOverlapPlaceBlock =
                placedState != BlockIds::AIR && wouldOverlapPlacedState(physicsBody.body, placeBlock, placedState);
        }

        const GameplayBlockAction action = modeRules.decideBlockAction(request);
        if (action != GameplayBlockAction::Place) {
            continue;
        }

        if (blockToPlace == 0) {
            continue;
        }

        if (placedState == BlockIds::AIR) {
            continue;
        }
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                net::ClientBlockAction blockAction;
                blockAction.sequence = ++runtime.heldItemSwingSequence;
                blockAction.action = net::ClientBlockActionType::Place;
                blockAction.targetBlock = target.targetBlock;
                blockAction.placeBlock = placeBlock;
                blockAction.hitNormal = target.hitNormal;
                blockAction.playerPosition = playerPos;
                blockAction.blockState = static_cast<uint16_t>(placedState);
                ctx.services.gameClient->sendBlockAction(blockAction);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            recordPostPlaceSuppression(runtime, placeBlock);
            continue;
        }

        mutableWorld->setBlock(placeBlock.x, placeBlock.y, placeBlock.z, placedState);
        // Notify DropSystem of placement (transitional — will be internalized later)
        if (ctx.services.dropSystem) {
            ctx.services.dropSystem->onBlockPlaced(placeBlock, *mutableWorld);
        }
        if (modeRules.shouldReportBreakProgress()) {
            static_cast<void>(inventory.consumeSelectedOne());
        }
        runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
        recordPostPlaceSuppression(runtime, placeBlock);
        audioBus.push({"block.generic.place", glm::vec3(placeBlock), true, 1.0f});
        ++runtime.heldItemSwingSequence;
    }
}

} // namespace ecs
