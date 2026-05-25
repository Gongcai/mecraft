#include "BlockBreakSystem.h"

#include <algorithm>

#include "../../util/AudioEventBuffer.h"
#include "../../util/DropSpawnEventBuffer.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../components/Components.h"
#include "../../../core/states/GameplayModeRules.h"
#include "../../../world/World.h"
#include "../../../ui/core/UIRenderer.h"

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

void BlockBreakSystem::update(SystemContext& ctx) {
    if (!ctx.services.world || !ctx.services.uiRenderer) return;
    auto& world = *ctx.services.world;
    auto& uiRenderer = *ctx.services.uiRenderer;
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
                              BlockInteractionRuntimeComponent>();
    for (auto e : view) {
        auto& runtime = view.get<BlockInteractionRuntimeComponent>(e);
        auto& blockBreak = view.get<BlockBreakComponent>(e);
        const auto& intent = view.get<BlockActionIntentComponent>(e);
        const auto& target = view.get<BlockTargetComponent>(e);

        runtime.creativeBreakCooldownRemaining =
            std::max(0.0f, runtime.creativeBreakCooldownRemaining - dt);

        if (!intent.wantsBreak || !target.hasTarget) {
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        const glm::ivec3 hitBlock = target.targetBlock;
        const BlockID targetBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
        if (targetBlock == 0 || !BlockRegistry::get(targetBlock).isSelectable) {
            resetBreakSession(blockBreak, uiRenderer, runtime);
            continue;
        }

        uiRenderer.setHeldItemPreviewActionAnimationActive(true);

        if (!modeRules.shouldReportBreakProgress()) {
            // Creative instant break
            if (runtime.creativeBreakCooldownRemaining > 0.0f) continue;

            const BlockID brokenBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            audioBus.push(
                {gameplay_state_detail::getRandomName("put", 5), glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            runtime.creativeBreakCooldownRemaining = modeRules.breakDurationMs(targetBlock) / 1000.0f;
            resetBreakSession(blockBreak, uiRenderer, runtime);
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
            const BlockID brokenBlock = world.getBlock(hitBlock.x, hitBlock.y, hitBlock.z);
            world.setBlock(hitBlock.x, hitBlock.y, hitBlock.z, 0);
            audioBus.push(
                {gameplay_state_detail::getRandomName("put", 5), glm::vec3(hitBlock), true, 1.0f});
            particleBus.push({hitBlock, brokenBlock});
            dropBus.push({brokenBlock, hitBlock});
            resetBreakSession(blockBreak, uiRenderer, runtime);
        }
    }
}

} // namespace ecs
