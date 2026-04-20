#include "BlockInteractionBridgeSystem.h"

#include <algorithm>

#include "../../util/AudioEventBuffer.h"
#include "../../components/Components.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../../core/states/GameplayModeRules.h"
#include "../../../player/Player.h"
#include "../../../world/Placement.h"
#include "../../../world/World.h"
#include "../../../world/DropSystem.h"
#include "../../../ui/UIRenderer.h"
#include "../../../item/Item.h"

namespace ecs {

namespace {
constexpr float kPickDistance = 6.0f;

const IGameplayModeRules& resolveModeRules(const GameplayRegistry& registry) {
    if (registry.ctxHas<GameplayRuntimeContext>()) {
        const auto& runtime = registry.ctxGet<GameplayRuntimeContext>();
        if (runtime.modeRules != nullptr) {
            return *runtime.modeRules;
        }
    }
    return SurvivalModeRules::instance();
}

PhysicsInfo buildPickRay(const TransformComponent& transform, const CameraStateComponent& camera) {
    return {transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f), camera.front};
}

BlockID resolvePlacementState(const BlockID blockId,
                              const CameraStateComponent& camera,
                              const MoveIntentComponent& moveIntent,
                              const RayHit& hit) {
    const BlockDef& def = BlockRegistry::get(blockId);
    PlacementStrategyFn strategy = PlacementStrategyRegistry::getStrategy(def.placementStrategy);
    if (strategy == nullptr) {
        return BlockStateRegistry::getDefaultState(blockId);
    }

    PlacementContext ctx;
    ctx.blockId = blockId;
    ctx.hitNormal = hit.normal;
    ctx.playerYaw = camera.yaw;
    ctx.isSneaking = moveIntent.wantsCrouch;

    const StateID stateId = strategy(ctx);
    return stateId != 0 ? stateId : BlockIds::AIR;
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

void resetBreakSession(BlockBreakComponent& blockBreak,
                       UIRenderer& uiRenderer,
                       BlockInteractionRuntimeComponent& runtime) {
    runtime.breakActive = false;
    runtime.breakElapsedMs = 0.0f;
    runtime.breakRequiredMs = 0.0f;
    runtime.breakBlockPos = glm::ivec3{};
    blockBreak.active = false;
    blockBreak.blockPos = glm::ivec3{};
    blockBreak.progress01 = 0.0f;
    uiRenderer.setHeldItemPreviewActionAnimationActive(false);
}

} // namespace

void BlockInteractionBridgeSystem::update(GameplayRegistry& registry,
                                          Player& player,
                                          World& world,
                                          DropSystem& dropSystem,
                                          UIRenderer& uiRenderer,
                                          const float dt) {
    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioEvents = ensureAudioEventBuffer(registry);
    auto& particleEvents = ensureParticleEventBuffer(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              MoveIntentComponent,
                              TransformComponent,
                              PhysicsBodyComponent,
                              CameraStateComponent,
                              InventoryComponent,
                              BlockTargetComponent,
                              BlockBreakComponent>();
    for (auto e : view) {
        if (!registry.has<BlockInteractionRuntimeComponent>(e)) {
            registry.emplace<BlockInteractionRuntimeComponent>(e);
        }
        auto& runtime = registry.get<BlockInteractionRuntimeComponent>(e);
        auto& moveIntent = view.get<MoveIntentComponent>(e);
        auto& transform = view.get<TransformComponent>(e);
        auto& physicsBody = view.get<PhysicsBodyComponent>(e);
        auto& camera = view.get<CameraStateComponent>(e);
        auto& inventoryState = view.get<InventoryComponent>(e);
        auto& target = view.get<BlockTargetComponent>(e);
        auto& blockBreak = view.get<BlockBreakComponent>(e);

        runtime.placeCooldownRemaining = std::max(0.0f, runtime.placeCooldownRemaining - dt);
        runtime.creativeBreakCooldownRemaining = std::max(0.0f, runtime.creativeBreakCooldownRemaining - dt);

        player.getInventory().setSelectedSlot(inventoryState.selectedHotbarSlot);

        const RayHit hit = world.raycast(buildPickRay(transform, camera), kPickDistance);
        const bool hasHit = hit.hit;
        const glm::ivec3 hitBlock = hit.blockPos;
        const glm::ivec3 placeBlock = hit.blockPos + hit.normal;
        target.hasTarget = hasHit;
        target.targetBlock = hasHit ? hitBlock : glm::ivec3{};
        target.placeBlock = hasHit ? placeBlock : glm::ivec3{};
        target.hitNormal = hasHit ? hit.normal : glm::ivec3{};

        const auto& blockIntent = view.get<BlockActionIntentComponent>(e);
        const bool wantsBreak = blockIntent.wantsBreak;
        const bool wantsPlace = blockIntent.wantsPlace;
        if (!wantsBreak && !wantsPlace) {
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        GameplayBlockActionRequest request;
        request.hasHit = hasHit;
        request.wantsBreak = wantsBreak;
        request.wantsPlace = wantsPlace;
        request.placeCooldownRemaining = runtime.placeCooldownRemaining;
        if (hasHit) {
            request.targetBlock = world.getBlock(placeBlock.x, placeBlock.y, placeBlock.z);
            request.playerWouldOverlapPlaceBlock = wouldOverlapBlock(physicsBody.body, placeBlock);
        }

        const GameplayBlockAction action = modeRules.decideBlockAction(request);
        if (action == GameplayBlockAction::Break) {
            const BlockID targetBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            if (targetBlock == 0 || !BlockRegistry::get(targetBlock).isSelectable) {
                resetBreakSession(blockBreak, uiRenderer, runtime);
                continue;
            }

            uiRenderer.setHeldItemPreviewActionAnimationActive(true);

            if (!modeRules.shouldReportBreakProgress()) {
                if (runtime.creativeBreakCooldownRemaining > 0.0f) {
                    continue;
                }

                const BlockID brokenBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
                world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
                dropSystem.spawnBlockDrop(brokenBlock, hitBlock);
                audioEvents.playSoundEvents.push_back({gameplay_state_detail::getRandomName("put", 5), glm::vec3(hitBlock), true, 1.0f});
                particleEvents.blockBreakEvents.push_back({hitBlock, brokenBlock});
                runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
                resetBreakSession(blockBreak, uiRenderer, runtime);
                continue;
            }

            const float requiredMs = modeRules.breakDurationMs(targetBlock);
            if (!runtime.breakActive || runtime.breakBlockPos != hitBlock) {
                runtime.breakActive = true;
                runtime.breakBlockPos = hitBlock;
                runtime.breakElapsedMs = 0.0f;
                runtime.breakRequiredMs = requiredMs;
            }

            runtime.breakElapsedMs += dt * 1000.0f;
            blockBreak.active = true;
            blockBreak.blockPos = hitBlock;
            blockBreak.progress01 = std::clamp(runtime.breakElapsedMs / runtime.breakRequiredMs, 0.0f, 1.0f);

            if (runtime.breakElapsedMs < runtime.breakRequiredMs) {
                continue;
            }

            const BlockID brokenBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            dropSystem.spawnBlockDrop(brokenBlock, hitBlock);
            audioEvents.playSoundEvents.push_back({gameplay_state_detail::getRandomName("put", 5), glm::vec3(hitBlock), true, 1.0f});
            particleEvents.blockBreakEvents.push_back({hitBlock, brokenBlock});
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        resetBreakSession(blockBreak, uiRenderer, runtime);
        if (action != GameplayBlockAction::Place) {
            continue;
        }

        Inventory& inventory = player.getInventory();
        const ItemID selectedItem = inventory.getSelectedItem();
        const BlockID blockToPlace = ItemRegistry::toPlaceBlock(selectedItem);
        if (blockToPlace == 0) {
            continue;
        }

        const BlockID placedState = resolvePlacementState(blockToPlace, camera, moveIntent, hit);
        if (placedState == BlockIds::AIR) {
            continue;
        }

        world.setBlock(placeBlock.x, placeBlock.y, placeBlock.z, placedState);
        dropSystem.onBlockPlaced(placeBlock, world);
        if (modeRules.shouldReportBreakProgress()) {
            static_cast<void>(inventory.consumeSelectedOne());
        }
        runtime.placeCooldownRemaining = modeRules.placeCooldownSeconds();
        audioEvents.playSoundEvents.push_back({gameplay_state_detail::getRandomName("put", 5), glm::vec3(placeBlock), true, 1.0f});
        uiRenderer.triggerHeldItemPreviewActionAnimation();
    }
}

} // namespace ecs
