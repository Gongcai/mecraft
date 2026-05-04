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

#ifndef NDEBUG
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#endif

namespace ecs {

GameplayScene::GameplayScene() {
    m_fixedUpdateSystems.reserve(32);

    // ── Fixed-update pipeline — execution order matches declaration order ──
    addFixedUpdateSystem<InputSamplingSystem>();
    addFixedUpdateSystem<PlayerIntentBuildSystem>();
    addFixedUpdateSystem<MobAISystem>();
    addFixedUpdateSystem<CharacterPhysicsSystem>();
    addFixedUpdateSystem<PlayerRuntimeUpdateSystem>();
    addFixedUpdateSystem<ViewBobSystem>();

    // ── Block interaction pipeline ──
    addFixedUpdateSystem<BlockTargetSystem>();
    addFixedUpdateSystem<BlockBreakSystem>();
    addFixedUpdateSystem<BlockPlaceSystem>();

    // ── Item pipeline ──
    addFixedUpdateSystem<ItemSpawnSystem>();
    addFixedUpdateSystem<ItemPhysicsSystem>();
    addFixedUpdateSystem<ItemMergeSystem>();
    addFixedUpdateSystem<ItemPickupSystem>();
    addFixedUpdateSystem<ItemLifetimeSystem>();

    // ── Particle pipeline ──
    addFixedUpdateSystem<ParticleSpawnSystem>();
    addFixedUpdateSystem<ParticleSimulationSystem>();
    addFixedUpdateSystem<ParticleCleanupSystem>();

    // ── Audio pipeline ──
    addFixedUpdateSystem<PlayerFootstepAudioSystem>();
    addFixedUpdateSystem<FallRollEffectSystem>();
    addFixedUpdateSystem<AudioSyncSystem>();

    // ── Steve sync, animation, and transform hierarchy ──
    addFixedUpdateSystem<SteveSyncSystem>();
    addFixedUpdateSystem<SteveAnimationSystem>();
    addFixedUpdateSystem<MobAnimationSystem>();
    addFixedUpdateSystem<TransformHierarchySystem>();

    // ── Tick-rate pipeline (20 TPS) ──
    addTickSystem<FluidTickSystem>();

#ifndef NDEBUG
    validateSystemOrder();
#endif
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
    SystemContext ctx{m_registry, m_services, dt, 0};
    for (auto& system : m_fixedUpdateSystems) {
        system->update(ctx);
    }
}

void GameplayScene::runOneTick() {
    SystemContext ctx{m_registry, m_services, 0.0f, m_tickClock.tickIndex()};
    for (auto& system : m_tickSystems) {
        system->update(ctx);
    }
}

#ifndef NDEBUG
void GameplayScene::validateSystemOrder() {
    std::unordered_set<uint32_t> writtenSoFar;
    std::unordered_map<uint32_t, const char*> componentWriters;

    // Pre-pass to find all writers
    for (const auto& info : m_systemDeps) {
        for (uint32_t written : info.written) {
            componentWriters[written] = info.systemName;
        }
    }

    bool hasWarning = false;
    // Validate order
    for (const auto& info : m_systemDeps) {
        for (uint32_t req : info.required) {
            if (componentWriters.count(req) && writtenSoFar.count(req) == 0) {
                std::cerr << "[ECS Order Warning] System " << info.systemName 
                          << " requires a component written by " << componentWriters[req] 
                          << ", but is scheduled BEFORE it.\n";
                hasWarning = true;
            }
        }
        for (uint32_t written : info.written) {
            writtenSoFar.insert(written);
        }
    }

    if (!hasWarning) {
        // std::cout << "[ECS] System execution order validated successfully.\n";
    }
}
#endif

} // namespace ecs
