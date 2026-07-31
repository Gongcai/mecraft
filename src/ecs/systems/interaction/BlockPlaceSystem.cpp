#include "BlockPlaceSystem.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../../util/AudioEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../game/inventory/BlockEntityInventoryLifecycle.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../client/GameClient.h"
#include "../../../world/IWorldView.h"
#include "../../../world/block/BedBlock.h"
#include "../../../world/block/BlockCollision.h"
#include "../../../world/block/DoorBlock.h"
#include "../../../world/block/Placement.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/redstone/WireContainerPlacement.h"
#include "../../../world/World.h"
#include "../../../world/DropSystem.h"
#include "../../../item/Item.h"

namespace ecs {

namespace {

[[noreturn]] void failBlockPlaceSystem(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

const IGameplayModeRules& resolveModeRules(const GameplayRegistry& registry) {
    if (registry.ctxHas<GameplayRuntimeContext>()) {
        const auto& runtime = registry.ctxGet<GameplayRuntimeContext>();
        if (runtime.modeRules != nullptr) {
            return *runtime.modeRules;
        }
    }
    return SurvivalModeRules::instance();
}

bool wouldOverlapPlacedState(const PhysicsBody& body, const glm::ivec3& blockPos, const BlockStateId stateId) {
    const glm::vec3 bodyCenter = body.position + body.colliderOffset;
    const glm::vec3 bodyMin = bodyCenter - body.halfExtents;
    const glm::vec3 bodyMax = bodyCenter + body.halfExtents;

    return BlockCollision::intersects(stateId, blockPos, bodyMin, bodyMax);
}

void recordPostPlaceSuppression(BlockInteractionRuntimeComponent& runtime, const glm::ivec3& placedBlock) {
    runtime.recentlyPlacedBlock = placedBlock;
    runtime.postPlaceInteractionSuppressSeconds = 0.25f;
}

BlockStateId resolvePlacementState(const BlockID blockId, const CameraStateComponent& camera,
                                   const MoveIntentComponent& moveIntent, const glm::ivec3& hitNormal,
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

    const BlockStateId stateId = strategy(pctx);
    return stateId;
}

struct PlacementResolution {
    glm::ivec3 placeBlock{};
    BlockStateId stateId = NULL_BLOCK_STATE;
    glm::ivec3 secondaryBlock{};
    BlockStateId secondaryStateId = NULL_BLOCK_STATE;
    bool hasSecondaryBlock = false;
    bool replacesExisting = false;
};

bool wouldOverlapPlacement(const PhysicsBody& body, const PlacementResolution& placement) {
    if (placement.stateId != NULL_BLOCK_STATE &&
        wouldOverlapPlacedState(body, placement.placeBlock, placement.stateId)) {
        return true;
    }
    return placement.hasSecondaryBlock && placement.secondaryStateId != NULL_BLOCK_STATE &&
           wouldOverlapPlacedState(body, placement.secondaryBlock, placement.secondaryStateId);
}

bool isPlainWireState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    const BlockID blockId = BlockStateRegistry::getBlockId(stateId);
    return BlockRegistry::getFast(blockId).redstoneBehavior == "wire";
}

BlockStateId withExistingWireFacing(const BlockStateId existingWireState, const BlockStateId incomingWireState) {
    if (!isPlainWireState(existingWireState) || !isPlainWireState(incomingWireState)) {
        return NULL_BLOCK_STATE;
    }

    const uint16_t existingFacing = BlockStateRegistry::getPropertyIndex(existingWireState, PropIndices::FACING);
    if (existingFacing == BlockStateRegistry::INVALID_INDEX) {
        failBlockPlaceSystem("Wire container placement target is missing facing");
    }
    return BlockStateRegistry::withProperty(incomingWireState, PropIndices::FACING, existingFacing);
}

bool canAddWirePartToTarget(const IWorldView& worldView, const glm::ivec3& position, const BlockStateId existingState,
                            const BlockStateId incomingWireState) {
    const World* concreteWorld = worldView.asWorld();
    return concreteWorld != nullptr ? WireContainerPlacement::canApply(*concreteWorld, position, incomingWireState)
                                    : WireContainerPlacement::canApplyToBlockState(existingState, incomingWireState);
}

BlockStateId resolveSameCellWireState(const IWorldView& worldView, const glm::ivec3& position,
                                      const BlockStateId existingState, const BlockStateId hitFaceWireState) {
    if (!WireContainerPlacement::isContainerPlacementTarget(existingState, hitFaceWireState)) {
        return NULL_BLOCK_STATE;
    }

    if (canAddWirePartToTarget(worldView, position, existingState, hitFaceWireState)) {
        return hitFaceWireState;
    }

    const BlockStateId existingFacingWireState = withExistingWireFacing(existingState, hitFaceWireState);
    if (existingFacingWireState != NULL_BLOCK_STATE && existingFacingWireState != hitFaceWireState &&
        canAddWirePartToTarget(worldView, position, existingState, existingFacingWireState)) {
        return existingFacingWireState;
    }

    return NULL_BLOCK_STATE;
}

PlacementResolution resolvePlacementTarget(const IWorldView& worldView, const BlockTargetComponent& target,
                                           const BlockID blockId, const CameraStateComponent& camera,
                                           const MoveIntentComponent& moveIntent) {
    PlacementResolution result;
    result.placeBlock = target.placeBlock;
    result.stateId = resolvePlacementState(blockId, camera, moveIntent, target.hitNormal, target.hitPosition);

    if (BedBlockLogic::isBedBlock(blockId)) {
        const BedBlockLogic::BedPlacement bedPlacement =
            BedBlockLogic::resolvePlacement(worldView, result.placeBlock, result.stateId);
        if (!bedPlacement.valid) {
            result.stateId = NULL_BLOCK_STATE;
            return result;
        }

        result.placeBlock = bedPlacement.footPos;
        result.stateId = bedPlacement.footState;
        result.secondaryBlock = bedPlacement.headPos;
        result.secondaryStateId = bedPlacement.headState;
        result.hasSecondaryBlock = true;
        return result;
    }

    if (DoorBlockLogic::isDoorBlock(blockId)) {
        const DoorBlockLogic::DoorPlacement doorPlacement =
            DoorBlockLogic::resolvePlacement(worldView, result.placeBlock, result.stateId);
        if (!doorPlacement.valid) {
            result.stateId = NULL_BLOCK_STATE;
            return result;
        }

        result.placeBlock = doorPlacement.lowerPos;
        result.stateId = doorPlacement.lowerState;
        result.secondaryBlock = doorPlacement.upperPos;
        result.secondaryStateId = doorPlacement.upperState;
        result.hasSecondaryBlock = true;
        return result;
    }

    const BlockStateId existingTargetState =
        worldView.getBlockState(target.targetBlock.x, target.targetBlock.y, target.targetBlock.z);
    const BlockStateId inCellState =
        resolvePlacementState(blockId, camera, moveIntent, -target.hitNormal, target.hitPosition);

    BlockStateId mergedState = NULL_BLOCK_STATE;
    if (tryMergePlacementStates(existingTargetState, inCellState, mergedState)) {
        result.placeBlock = target.targetBlock;
        result.stateId = mergedState;
        result.replacesExisting = true;
        return result;
    }

    if (WireContainerPlacement::isContainerPlacementTarget(existingTargetState, result.stateId)) {
        const BlockStateId sameCellWireState =
            resolveSameCellWireState(worldView, target.targetBlock, existingTargetState, result.stateId);
        if (sameCellWireState == NULL_BLOCK_STATE) {
            result.stateId = NULL_BLOCK_STATE;
            return result;
        }

        result.placeBlock = target.targetBlock;
        result.stateId = sameCellWireState;
        result.replacesExisting = true;
        return result;
    }

    const BlockStateId existingPlaceState =
        worldView.getBlockState(result.placeBlock.x, result.placeBlock.y, result.placeBlock.z);
    if (WireContainerPlacement::isContainerPlacementTarget(existingPlaceState, result.stateId)) {
        const BlockStateId placeCellWireState =
            resolveSameCellWireState(worldView, result.placeBlock, existingPlaceState, result.stateId);
        if (placeCellWireState == NULL_BLOCK_STATE) {
            result.stateId = NULL_BLOCK_STATE;
            return result;
        }

        result.stateId = placeCellWireState;
        result.replacesExisting = true;
        return result;
    }

    return result;
}

} // namespace

void BlockPlaceSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView)
        return;
    const auto& worldView = *ctx.services.worldView;
    World* mutableWorld = ctx.services.world.get();
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioBus = ensureAudioEventBus(registry);

    auto view = registry.view<LocalPlayerTag, BlockActionIntentComponent, MoveIntentComponent, TransformComponent,
                              PhysicsBodyComponent, CameraStateComponent, InventoryComponent, InventoryDataComponent,
                              BlockTargetComponent, BlockInteractionRuntimeComponent>();
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
        runtime.postPlaceInteractionSuppressSeconds = std::max(0.0f, runtime.postPlaceInteractionSuppressSeconds - dt);

        // Sync selected slot to inventory
        inventoryData.inventory.setSelectedSlot(inventoryState.selectedHotbarSlot);

        if (!intent.wantsPlace || !target.hasTarget) {
            continue;
        }

        // Server-side distance validation: player must be within reach
        Inventory& inventory = inventoryData.inventory;
        const ItemID selectedItem = inventory.getSelectedItem();
        const BlockID blockToPlace = ItemRegistry::toPlaceBlock(selectedItem);
        PlacementResolution placement;
        if (blockToPlace != 0) {
            placement = resolvePlacementTarget(worldView, target, blockToPlace, camera, moveIntent);
        } else {
            placement.placeBlock = target.placeBlock;
        }
        const glm::ivec3 placeBlock = placement.placeBlock;
        constexpr float kMaxPlaceDistance = 6.5f;
        const glm::vec3 blockCenter = glm::vec3(placeBlock) + glm::vec3(0.5f);
        const glm::vec3 playerPos = view.get<TransformComponent>(e).position;
        const glm::vec3 diff = playerPos - blockCenter;
        const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        if (distSq > kMaxPlaceDistance * kMaxPlaceDistance) {
            continue;
        }

        const BlockStateId placedState = placement.stateId;

        GameplayBlockActionRequest request;
        request.hasHit = target.hasTarget;
        request.wantsBreak = intent.wantsBreak;
        request.wantsPlace = intent.wantsPlace;
        request.placeCooldownRemaining = runtime.placeCooldownRemaining;
        if (target.hasTarget) {
            request.targetBlock = worldView.getBlockState(placeBlock.x, placeBlock.y, placeBlock.z);
            request.placementReplacesTarget = placement.replacesExisting;
            request.playerWouldOverlapPlaceBlock = wouldOverlapPlacement(physicsBody.body, placement);
        }

        const GameplayBlockAction action = modeRules.decideBlockAction(request);
        if (action != GameplayBlockAction::Place) {
            continue;
        }

        if (blockToPlace == 0) {
            continue;
        }

        if (placedState == NULL_BLOCK_STATE) {
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
                blockAction.blockState = placedState;
                ctx.services.gameClient->sendBlockAction(blockAction);
            } else {
                ++runtime.heldItemSwingSequence;
            }
            runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
            recordPostPlaceSuppression(runtime, placeBlock);
            continue;
        }

        if (placement.hasSecondaryBlock) {
            if (BedBlockLogic::isBedState(placement.stateId)) {
                BedBlockLogic::BedPlacement bedPlacement;
                bedPlacement.valid = true;
                bedPlacement.footPos = placement.placeBlock;
                bedPlacement.headPos = placement.secondaryBlock;
                bedPlacement.footState = placement.stateId;
                bedPlacement.headState = placement.secondaryStateId;
                BedBlockLogic::placeBed(*mutableWorld, bedPlacement);
            } else if (DoorBlockLogic::isDoorState(placement.stateId)) {
                DoorBlockLogic::DoorPlacement doorPlacement;
                doorPlacement.valid = true;
                doorPlacement.lowerPos = placement.placeBlock;
                doorPlacement.upperPos = placement.secondaryBlock;
                doorPlacement.lowerState = placement.stateId;
                doorPlacement.upperState = placement.secondaryStateId;
                DoorBlockLogic::placeDoor(*mutableWorld, doorPlacement);
            } else {
                failBlockPlaceSystem("Unsupported multi-block placement state");
            }
        } else {
            const WireContainerPlacement::ApplyResult wirePlacementResult =
                WireContainerPlacement::apply(*mutableWorld, placeBlock, placedState);
            if (wirePlacementResult == WireContainerPlacement::ApplyResult::Rejected) {
                continue;
            }
            if (wirePlacementResult == WireContainerPlacement::ApplyResult::NotWirePlacement) {
                mutableWorld->setBlockState(placeBlock.x, placeBlock.y, placeBlock.z, placedState);
            }
        }
        static_cast<void>(ensureBlockEntityInventoryForPlacedBlock(registry, blockToPlace, placeBlock));
        // Notify DropSystem of placement so nearby drops resolve against new collision.
        if (ctx.services.dropSystem) {
            ctx.services.dropSystem->onBlockPlaced(placeBlock, *mutableWorld);
            if (placement.hasSecondaryBlock) {
                ctx.services.dropSystem->onBlockPlaced(placement.secondaryBlock, *mutableWorld);
            }
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
