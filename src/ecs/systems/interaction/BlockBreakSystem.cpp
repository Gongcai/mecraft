#include "BlockBreakSystem.h"

#include <algorithm>

#include "../../util/AudioEventBuffer.h"
#include "../../util/DropSpawnEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../game/modes/GameplayModeRules.h"
#include "../../../game/inventory/ChestInventoryLifecycle.h"
#include "../../../client/GameClient.h"
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

void resetBreakSession(BlockBreakComponent& blockBreak,
                       BlockInteractionRuntimeComponent& runtime) {
    runtime.breakActive = false;
    runtime.breakElapsedMs = 0.0f;
    runtime.breakRequiredMs = 0.0f;
    runtime.breakBlockPos = glm::ivec3{};
    blockBreak.active = false;
    blockBreak.blockPos = glm::ivec3{};
    blockBreak.progress01 = 0.0f;
}

} // namespace

void BlockBreakSystem::update(SystemContext& ctx) {
    if (!ctx.services.worldView) return;
    const auto& worldView = *ctx.services.worldView;
    World* mutableWorld = ctx.services.world.get();
    auto& registry = ctx.registry;
    const float dt = ctx.dt;

    const IGameplayModeRules& modeRules = resolveModeRules(registry);
    auto& audioBus = ensureAudioEventBus(registry);
    auto& particleBus = ensureParticleEventBus(registry);
    auto& dropBus = ensureDropSpawnEventBus(registry);

    auto view = registry.view<LocalPlayerTag,
                              BlockActionIntentComponent,
                              BlockTargetComponent,
                              BlockBreakComponent,
                              BlockInteractionRuntimeComponent,
                              TransformComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        auto& blockBreak = view.get<BlockBreakComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);

        runtime.creativeBreakCooldownRemaining =
            std::max(0.0f, runtime.creativeBreakCooldownRemaining - dt);

        if (!intent.wantsBreak || !target.hasTarget) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        const glm::ivec3 hitBlock = target.targetBlock;

        // Server-side distance validation: player must be within reach
        constexpr float kMaxBreakDistance = 6.5f;
        const glm::vec3 blockCenter = glm::vec3(hitBlock) + glm::vec3(0.5f);
        const glm::vec3 diff = transform.position - blockCenter;
        const float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        if (distSq > kMaxBreakDistance * kMaxBreakDistance) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        const BlockID targetBlock = worldView.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
        if (targetBlock == 0 || !BlockRegistry::get(targetBlock).isSelectable) {
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        if (!modeRules.shouldReportBreakProgress()) {
            // Creative instant break
            if (runtime.creativeBreakCooldownRemaining > 0.0f) continue;
            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    net::ClientBlockAction action;
                    action.sequence = ++runtime.heldItemSwingSequence;
                    action.action = net::ClientBlockActionType::Break;
                    action.targetBlock = hitBlock;
                    action.placeBlock = target.placeBlock;
                    action.hitNormal = target.hitNormal;
                    action.playerPosition = transform.position;
                    ctx.services.gameClient->sendBlockAction(action);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
                resetBreakSession(blockBreak, runtime);
                continue;
            }

            const BlockID brokenBlock = mutableWorld->getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            mutableWorld->setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            const bool handledChest = handleChestInventoryBreak(registry, brokenBlock, hitBlock, false);
            static_cast<void>(handledChest);
            audioBus.push({"block.generic.break", glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
            ++runtime.heldItemSwingSequence;
            resetBreakSession(blockBreak, runtime);
            continue;
        }

        // Survival break with progress
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

        if (runtime.breakElapsedMs >= runtime.breakRequiredMs) {
            if (mutableWorld == nullptr) {
                if (ctx.services.gameClient) {
                    net::ClientBlockAction action;
                    action.sequence = ++runtime.heldItemSwingSequence;
                    action.action = net::ClientBlockActionType::Break;
                    action.targetBlock = hitBlock;
                    action.placeBlock = target.placeBlock;
                    action.hitNormal = target.hitNormal;
                    action.playerPosition = transform.position;
                    ctx.services.gameClient->sendBlockAction(action);
                } else {
                    ++runtime.heldItemSwingSequence;
                }
                resetBreakSession(blockBreak, runtime);
                continue;
            }
            const BlockID brokenBlock = mutableWorld->getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            mutableWorld->setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            const bool handledChest = handleChestInventoryBreak(registry, brokenBlock, hitBlock, true);
            static_cast<void>(handledChest);
            audioBus.push({"block.generic.break", glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            dropBus.push({brokenBlock, hitBlock});
            ++runtime.heldItemSwingSequence;
            resetBreakSession(blockBreak, runtime);
        }
    }
}

} // namespace ecs
