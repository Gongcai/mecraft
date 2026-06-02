#include "BlockPlaceSystem.h"

#include <algorithm>

#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../client/GameClient.h"
#include "../../../world/IWorldView.h"
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

bool wouldOverlapBlock(const PhysicsBody& body, const glm::ivec3& blockPos) {
    const glm::vec3 bodyCenter = body.position + body.colliderOffset;
    const glm::vec3 bodyMin = bodyCenter - body.halfExtents;
    const glm::vec3 bodyMax = bodyCenter + body.halfExtents;

    const glm::vec3 blockMin(static_cast<float>(blockPos.x),
                             static_cast<float>(blockPos.y),
                             static_cast<float>(blockPos.z));
    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f, 1.0f, 1.0f);

    return bodyMin.x < blockMax.x && bodyMax.x > blockMin.x &&
           bodyMin.y < blockMax.y && bodyMax.y > blockMin.y &&
           bodyMin.z < blockMax.z && bodyMax.z > blockMin.z;
}

BlockID resolvePlacementState(const BlockID blockId,
                              const CameraStateComponent& camera,
                              const MoveIntentComponent& moveIntent,
                              const glm::ivec3& hitNormal) {
    const BlockDef& def = BlockRegistry::get(blockId);
    PlacementStrategyFn strategy = PlacementStrategyRegistry::getStrategy(def.placementStrategy);
    if (strategy == nullptr) {
        return BlockStateRegistry::getDefaultState(blockId);
    }

    PlacementContext pctx;
    pctx.blockId = blockId;
    pctx.hitNormal = hitNormal;
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

        // Decide if placement is allowed via mode rules

        GameplayBlockActionRequest request;
        request.hasHit = target.hasTarget;
        request.wantsBreak = intent.wantsBreak;
        request.wantsPlace = intent.wantsPlace;
        request.placeCooldownRemaining = runtime.placeCooldownRemaining;
        if (target.hasTarget) {
            request.targetBlock = worldView.getBlock(placeBlock.x, placeBlock.y, placeBlock.z);
            request.playerWouldOverlapPlaceBlock = wouldOverlapBlock(physicsBody.body, placeBlock);
        }

        const GameplayBlockAction action = modeRules.decideBlockAction(request);
        if (action != GameplayBlockAction::Place) {
            continue;
        }

        Inventory& inventory = inventoryData.inventory;
        const ItemID selectedItem = inventory.getSelectedItem();
        const BlockID blockToPlace = ItemRegistry::toPlaceBlock(selectedItem);
        if (blockToPlace == 0) {
            continue;
        }

        const BlockID placedState = resolvePlacementState(blockToPlace, camera, moveIntent, target.hitNormal);
        if (placedState == BlockIds::AIR) {
            continue;
        }
        if (mutableWorld == nullptr) {
            if (ctx.services.gameClient) {
                net::ClientBlockAction action;
                action.sequence = ++runtime.heldItemSwingSequence;
                action.action = net::ClientBlockActionType::Place;
                action.targetBlock = target.targetBlock;
                action.placeBlock = placeBlock;
                action.hitNormal = target.hitNormal;
                action.playerPosition = playerPos;
                action.blockState = static_cast<uint16_t>(placedState);
                ctx.services.gameClient->sendBlockAction(action);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
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
        audioBus.push(
            {gameplay_state_detail::getRandomName("put", 5), glm::vec3(placeBlock), true, 1.0f});
        ++runtime.heldItemSwingSequence;
    }
}

} // namespace ecs
