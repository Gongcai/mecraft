#include "GameplayPipeline.h"

#include "systems/audio/AudioSyncSystem.h"
#include "systems/audio/PlayerFootstepAudioSystem.h"
#include "systems/combat/DamageSystem.h"
#include "systems/combat/DeathSystem.h"
#include "systems/combat/HurtEffectDecaySystem.h"
#include "systems/combat/PlayerMeleeSystem.h"
#include "systems/combat/ProjectileSystem.h"
#include "systems/interaction/BlockBreakSystem.h"
#include "systems/interaction/BucketUseSystem.h"
#include "systems/interaction/BlockPlaceSystem.h"
#include "systems/interaction/BlockTargetSystem.h"
#include "systems/interaction/SoilTillingSystem.h"
#include "systems/item/ItemLifetimeSystem.h"
#include "systems/item/ItemMergeSystem.h"
#include "systems/item/ItemPhysicsSystem.h"
#include "systems/item/ItemPickupSystem.h"
#include "systems/item/ItemSpawnSystem.h"
#include "systems/mob/MobAISystem.h"
#include "systems/mob/MobAnimationSystem.h"
#include "systems/network/NetworkInterpolationSystem.h"
#include "systems/particle/ParticleCleanupSystem.h"
#include "systems/particle/ParticleSimulationSystem.h"
#include "systems/particle/ParticleSpawnSystem.h"
#include "systems/player/CharacterPhysicsSystem.h"
#include "systems/player/FallDamageSystem.h"
#include "systems/player/FallRollEffectSystem.h"
#include "systems/player/HungerDepletionSystem.h"
#include "systems/player/InputSamplingSystem.h"
#include "systems/player/PlayerIntentBuildSystem.h"
#include "systems/player/PlayerRuntimeUpdateSystem.h"
#include "systems/player/ViewBobSystem.h"
#include "systems/steve/SteveAnimationSystem.h"
#include "systems/steve/SteveSyncSystem.h"
#include "systems/steve/TransformHierarchySystem.h"
#include "systems/world/BlockSupportSystem.h"
#include "systems/world/FallingBlockSpawnSystem.h"
#include "systems/world/FallingBlockTickSystem.h"
#include "systems/world/FallingBlockInterpolateSystem.h"
#include "systems/world/FarmlandMoistureSystem.h"
#include "systems/world/FluidTickSystem.h"
#include "systems/world/PressurePlateSystem.h"
#include "systems/world/HopperSystem.h"
#include "systems/world/RandomTickSystem.h"
#include "systems/world/RedstoneDeviceActionSystem.h"
#include "systems/world/RedstoneSystem.h"

#ifdef MECRAFT_DEBUG
#include <chrono>
#endif

namespace ecs {

GameplayPipeline::GameplayPipeline(const GameplayPipelineProfile profile) {
    m_fixedUpdateSystems.reserve(40);

    switch (profile) {
    case GameplayPipelineProfile::Client:
        buildClientFixedUpdateSystems();
        buildClientTickSystems();
        break;
    case GameplayPipelineProfile::Server:
        buildServerFixedUpdateSystems();
        break;
    }

#ifdef MECRAFT_DEBUG
    validateSystemOrder();
#endif
}

void GameplayPipeline::buildClientFixedUpdateSystems() {
    // Input and player state.
    addFixedUpdateSystem<InputSamplingSystem>();
    addFixedUpdateSystem<PlayerIntentBuildSystem>();
    addFixedUpdateSystem<MobAISystem>();
    addFixedUpdateSystem<CharacterPhysicsSystem>();
    addFixedUpdateSystem<PlayerRuntimeUpdateSystem>();
    addFixedUpdateSystem<FallDamageSystem>();
    addFixedUpdateSystem<ViewBobSystem>();

    // Block interaction and combat.
    addFixedUpdateSystem<BlockTargetSystem>();
    addFixedUpdateSystem<PlayerMeleeSystem>();
    addFixedUpdateSystem<ProjectileSystem>();
    addFixedUpdateSystem<DamageSystem>(FixedUpdateDebugCategory::State, PostSystemHook::AfterDamageSystem);
    addFixedUpdateSystem<HurtEffectDecaySystem>();
    addFixedUpdateSystem<DeathSystem>();
    addFixedUpdateSystem<BlockBreakSystem>();
    addFixedUpdateSystem<SoilTillingSystem>();
    addFixedUpdateSystem<BucketUseSystem>();
    addFixedUpdateSystem<BlockPlaceSystem>();

    // Item/drop lifecycle.
    addFixedUpdateSystem<ItemSpawnSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemPhysicsSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemMergeSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemPickupSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemLifetimeSystem>(FixedUpdateDebugCategory::Drop);

    // Particles.
    addFixedUpdateSystem<ParticleSpawnSystem>(FixedUpdateDebugCategory::Particle);
    addFixedUpdateSystem<ParticleSimulationSystem>(FixedUpdateDebugCategory::Particle);
    addFixedUpdateSystem<ParticleCleanupSystem>(FixedUpdateDebugCategory::Particle);

    // Survival state.
    addFixedUpdateSystem<HungerDepletionSystem>();

    // Audio and local presentation.
    addFixedUpdateSystem<PlayerFootstepAudioSystem>();
    addFixedUpdateSystem<FallRollEffectSystem>();
    addFixedUpdateSystem<AudioSyncSystem>();
    addFixedUpdateSystem<NetworkInterpolationSystem>();

    // Humanoid visual sync/animation.
    addFixedUpdateSystem<SteveSyncSystem>();
    addFixedUpdateSystem<SteveAnimationSystem>();
    addFixedUpdateSystem<MobAnimationSystem>();
    addFixedUpdateSystem<TransformHierarchySystem>();

    // Falling-block render interpolation (smooths 20 TPS tick motion to 60 Hz).
    addFixedUpdateSystem<FallingBlockInterpolateSystem>();
}

void GameplayPipeline::buildServerFixedUpdateSystems() {
    // Authoritative gameplay systems. Client-only input, block interaction,
    // particles/audio, and visual hierarchy systems intentionally stay out.
    addFixedUpdateSystem<MobAISystem>();
    addFixedUpdateSystem<CharacterPhysicsSystem>();
    addFixedUpdateSystem<PlayerMeleeSystem>();
    addFixedUpdateSystem<ProjectileSystem>();
    addFixedUpdateSystem<DamageSystem>(FixedUpdateDebugCategory::State, PostSystemHook::AfterDamageSystem);
    addFixedUpdateSystem<HurtEffectDecaySystem>();
    addFixedUpdateSystem<DeathSystem>();
    addFixedUpdateSystem<ItemPhysicsSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemMergeSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemPickupSystem>(FixedUpdateDebugCategory::Drop);
    addFixedUpdateSystem<ItemLifetimeSystem>(FixedUpdateDebugCategory::Drop);
}

void GameplayPipeline::buildClientTickSystems() {
    addTickSystem<FluidTickSystem>();
    addTickSystem<FarmlandMoistureSystem>();
    addTickSystem<RandomTickSystem>();
    addTickSystem<BlockSupportSystem>();
    addTickSystem<PressurePlateSystem>();
    addTickSystem<RedstoneSystem>();
    addTickSystem<RedstoneDeviceActionSystem>();
    addTickSystem<HopperSystem>();
    // Spawn falling-block entities from events emitted by BlockSupportSystem,
    // then advance each entity one cell per tick (Minecraft falling semantics).
    addTickSystem<FallingBlockSpawnSystem>();
    addTickSystem<FallingBlockTickSystem>();
}

void GameplayPipeline::runFixedUpdate(GameplayRegistry& registry,
                                      GameplayServices& services,
                                      const float dt,
                                      const uint64_t tickIndex,
                                      const GameplayPipelineHooks* hooks) {
    SystemContext ctx{registry, services, dt, tickIndex};
    for (auto& entry : m_fixedUpdateSystems) {
        entry.system->update(ctx);
        runPostHook(entry.postHook, ctx, hooks);
    }
}

#ifdef MECRAFT_DEBUG
GameplayPipeline::FixedUpdateProfile GameplayPipeline::runFixedUpdateProfiled(GameplayRegistry& registry,
                                                                              GameplayServices& services,
                                                                              const float dt,
                                                                              const uint64_t tickIndex) {
    FixedUpdateProfile profile;
    SystemContext ctx{registry, services, dt, tickIndex};
    for (size_t i = 0; i < m_fixedUpdateSystems.size(); ++i) {
        auto& entry = m_fixedUpdateSystems[i];
        const auto start = std::chrono::steady_clock::now();
        entry.system->update(ctx);
        const auto end = std::chrono::steady_clock::now();

        const auto category = i < m_fixedUpdateDebugCategories.size()
            ? m_fixedUpdateDebugCategories[i]
            : FixedUpdateDebugCategory::State;
        profile.categoryMs[static_cast<size_t>(category)] +=
            std::chrono::duration<double, std::milli>(end - start).count();
    }
    return profile;
}
#endif

void GameplayPipeline::runOneTick(GameplayRegistry& registry,
                                  GameplayServices& services,
                                  const float dt,
                                  const uint64_t tickIndex) {
    SystemContext ctx{registry, services, dt, tickIndex};
    for (auto& system : m_tickSystems) {
        system->update(ctx);
    }
}

void GameplayPipeline::runPostHook(const PostSystemHook hook,
                                   SystemContext& ctx,
                                   const GameplayPipelineHooks* hooks) {
    if (hooks == nullptr) {
        return;
    }

    switch (hook) {
    case PostSystemHook::AfterDamageSystem:
        if (hooks->afterDamageSystem) {
            hooks->afterDamageSystem(ctx);
        }
        break;
    case PostSystemHook::None:
    default:
        break;
    }
}

#ifdef MECRAFT_DEBUG
void GameplayPipeline::validateSystemOrder() {
    // SystemDependency currently documents registry query shape, not strict
    // producer/consumer dataflow. Keep the hook as the future home for explicit
    // transient dependency validation without warning on persistent components.
}
#endif

} // namespace ecs
