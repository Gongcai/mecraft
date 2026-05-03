#include "GameplayScene.h"
#include "util/InputFrameState.h"
#include "components/Components.h"
#include "systems/player/CharacterPhysicsSystem.h"
#include "systems/player/InputSamplingSystem.h"
#include "systems/player/PlayerIntentBuildSystem.h"
#include "systems/player/PlayerRuntimeUpdateSystem.h"
#include "systems/player/ViewBobSystem.h"
#include "systems/audio/AudioSyncSystem.h"
#include "systems/interaction/BlockTargetSystem.h"
#include "systems/interaction/BlockBreakSystem.h"
#include "systems/interaction/BlockPlaceSystem.h"
#include "systems/item/ItemSpawnSystem.h"
#include "systems/item/ItemPhysicsSystem.h"
#include "systems/item/ItemMergeSystem.h"
#include "systems/item/ItemPickupSystem.h"
#include "systems/item/ItemLifetimeSystem.h"
#include "systems/particle/ParticleCleanupSystem.h"
#include "systems/particle/ParticleSimulationSystem.h"
#include "systems/particle/ParticleSpawnSystem.h"
#include "systems/player/FallRollEffectSystem.h"
#include "systems/audio/PlayerFootstepAudioSystem.h"
#include "systems/world/FluidTickSystem.h"
#include "systems/steve/SteveAnimationSystem.h"
#include "systems/steve/SteveSyncSystem.h"
#include "systems/steve/TransformHierarchySystem.h"
#include "systems/mob/MobAISystem.h"
#include "systems/mob/MobAnimationSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../world/World.h"

namespace ecs {

GameplayScene::GameplayScene() {
    // ── Fixed-update pipeline — execution order matches declaration order ──

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        if (ctx.services.inputContextManager) {
            InputSamplingSystem::update(ctx.registry, *ctx.services.inputContextManager);
        }
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        PlayerIntentBuildSystem::update(ctx.registry);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        MobAISystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        if (ctx.services.physicsSystem) {
            CharacterPhysicsSystem::update(ctx.registry, *ctx.services.physicsSystem, ctx.dt);
        }
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        PlayerRuntimeUpdateSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ViewBobSystem::update(ctx.registry, ctx.dt);
    }));

    // ── Block interaction pipeline (was BlockInteractionBridgeSystem) ──
    m_fixedUpdateSystems.push_back(std::make_unique<BlockTargetSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<BlockBreakSystem>());
    m_fixedUpdateSystems.push_back(std::make_unique<BlockPlaceSystem>());

    // ── Item pipeline (was DropCollectionBridgeSystem) ──
    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ItemSpawnSystem::update(ctx.registry);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        if (ctx.services.world) {
            ItemPhysicsSystem::update(ctx.registry, *ctx.services.world, ctx.dt);
        }
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ItemMergeSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        constexpr float kDropCollectRadius = 1.35f;
        auto playerView = ctx.registry.view<LocalPlayerTag, TransformComponent, InventoryDataComponent>();
        for (auto e : playerView) {
            const auto& transform = playerView.get<TransformComponent>(e);
            auto& inventoryData = playerView.get<InventoryDataComponent>(e);
            static_cast<void>(ItemPickupSystem::update(ctx.registry,
                                                       transform.position,
                                                       kDropCollectRadius,
                                                       inventoryData.inventory));
            break; // Only first local player
        }
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ItemLifetimeSystem::update(ctx.registry, ctx.dt);
    }));

    // ── Particle pipeline ──
    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ParticleSpawnSystem::update(ctx.registry);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ParticleSimulationSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        ParticleCleanupSystem::update(ctx.registry);
    }));

    // ── Audio pipeline ──
    m_fixedUpdateSystems.push_back(std::make_unique<PlayerFootstepAudioSystem>());

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        FallRollEffectSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        if (ctx.services.audioEngine) {
            AudioSyncSystem::update(ctx.registry, *ctx.services.audioEngine);
        }
    }));

    // ── Steve sync, animation, and transform hierarchy ──
    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        if (ctx.services.cameraController) {
            SteveSyncSystem::update(ctx.registry, *ctx.services.cameraController);
        }
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        SteveAnimationSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        MobAnimationSystem::update(ctx.registry, ctx.dt);
    }));

    m_fixedUpdateSystems.push_back(makeLegacySystem([](SystemContext& ctx) {
        TransformHierarchySystem::update(ctx.registry);
    }));
}

void GameplayScene::initLocalPlayer(const glm::vec3& spawnPos) {
    m_localPlayer = m_registry.create();
    m_registry.emplace<LocalPlayerTag>(m_localPlayer);
    m_registry.emplace<MoveIntentComponent>(m_localPlayer);
    m_registry.emplace<LookIntentComponent>(m_localPlayer);
    m_registry.emplace<HotbarIntentComponent>(m_localPlayer);
    m_registry.emplace<BlockActionIntentComponent>(m_localPlayer);

    auto& transform = m_registry.emplace<TransformComponent>(m_localPlayer);
    transform.position = spawnPos;
    transform.eyeHeight = 1.62f;

    auto& physicsBody = m_registry.emplace<PhysicsBodyComponent>(m_localPlayer);
    physicsBody.body.position = spawnPos;
    physicsBody.body.velocity = glm::vec3(0.0f);
    physicsBody.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
    physicsBody.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
    physicsBody.body.eyeOffsetY = 1.62f;

    auto& controller = m_registry.emplace<CharacterControllerComponent>(m_localPlayer);
    if (m_services.physicsSystem) {
        controller.tuning = m_services.physicsSystem->tuning;
    }
    controller.standEyeHeight = 1.62f;

    auto& camera = m_registry.emplace<CameraStateComponent>(m_localPlayer);
    camera.yaw = -90.0f;
    camera.pitch = 0.0f;
    camera.fov = 75.0f;
    camera.sensitivity = 0.1f;
    camera.front = glm::vec3(0.0f, 0.0f, -1.0f);
    camera.right = glm::vec3(1.0f, 0.0f, 0.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);

    auto& sprintFov = m_registry.emplace<SprintFovComponent>(m_localPlayer);
    sprintFov.walkFov = 75.0f;
    sprintFov.sprintFov = 90.0f;

    auto& inventory = m_registry.emplace<InventoryComponent>(m_localPlayer);
    inventory.selectedHotbarSlot = 0;

    auto& inventoryData = m_registry.emplace<InventoryDataComponent>(m_localPlayer);
    inventoryData.inventory.initializeDefaultLoadout();

    m_registry.emplace<BlockTargetComponent>(m_localPlayer);
    m_registry.emplace<BlockBreakComponent>(m_localPlayer);
    m_registry.emplace<BlockInteractionRuntimeComponent>(m_localPlayer);
    m_registry.emplace<FlightStateComponent>(m_localPlayer);
    m_registry.emplace<FootstepStateComponent>(m_localPlayer);
    m_registry.emplace<LandingStateComponent>(m_localPlayer);
    m_registry.emplace<FallRollComponent>(m_localPlayer);
    m_registry.emplace<HealthComponent>(m_localPlayer);
    m_registry.emplace<ArmorComponent>(m_localPlayer);
    m_registry.emplace<FoodComponent>(m_localPlayer);
    m_registry.emplace<ViewBobComponent>(m_localPlayer);
    m_registry.emplace<HurtEffectComponent>(m_localPlayer);

    if (!m_registry.ctxHas<InputFrameState>()) {
        m_registry.ctxSet<InputFrameState>();
    }
}

void GameplayScene::runFixedUpdate(float dt) {
    SystemContext ctx{m_registry, m_services, dt};
    for (auto& system : m_fixedUpdateSystems) {
        system->update(ctx);
    }
}

void GameplayScene::runOneTick() {
    if (m_services.world) {
        FluidTickSystem::update(*m_services.world, m_tickClock.tickIndex());
    }
}

} // namespace ecs
