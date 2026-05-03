#include "GameplayScene.h"
#include "util/InputFrameState.h"
#include "components/Components.h"
#include "systems/player/CharacterPhysicsSystem.h"
#include "systems/player/InputSamplingSystem.h"
#include "systems/player/PlayerIntentBuildSystem.h"
#include "systems/player/PlayerRuntimeUpdateSystem.h"
#include "systems/player/ViewBobSystem.h"
#include "systems/audio/AudioSyncSystem.h"
#include "systems/interaction/BlockInteractionBridgeSystem.h"
#include "systems/item/DropCollectionBridgeSystem.h"
#include "systems/particle/ParticleCleanupSystem.h"
#include "systems/particle/ParticleSimulationSystem.h"
#include "systems/particle/ParticleSpawnSystem.h"
#include "systems/player/FallRollEffectSystem.h"
#include "systems/audio/PlayerAudioBridgeSystem.h"
#include "systems/world/FluidTickSystem.h"
#include "systems/steve/SteveAnimationSystem.h"
#include "systems/steve/SteveSyncSystem.h"
#include "systems/steve/TransformHierarchySystem.h"
#include "systems/mob/MobAISystem.h"
#include "systems/mob/MobAnimationSystem.h"
#include "../physics/PhysicsSystem.h"
#include "../world/World.h"

namespace ecs {

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
    if (m_services.inputContextManager) {
        InputSamplingSystem::update(m_registry, *m_services.inputContextManager);
    }

    PlayerIntentBuildSystem::update(m_registry);

    MobAISystem::update(m_registry, dt);

    if (m_services.physicsSystem) {
        CharacterPhysicsSystem::update(m_registry, *m_services.physicsSystem, dt);
    }

    PlayerRuntimeUpdateSystem::update(m_registry, dt);

    ViewBobSystem::update(m_registry, dt);

    if (m_services.world && m_services.dropSystem && m_services.uiRenderer) {
        BlockInteractionBridgeSystem::update(m_registry,
                                             *m_services.world,
                                             *m_services.dropSystem,
                                             *m_services.uiRenderer,
                                             dt);
    }

    if (m_services.world && m_services.dropSystem) {
        DropCollectionBridgeSystem::update(m_registry,
                                           *m_services.dropSystem,
                                           *m_services.world,
                                           dt);
    }

    ParticleSpawnSystem::update(m_registry);
    ParticleSimulationSystem::update(m_registry, dt);
    ParticleCleanupSystem::update(m_registry);

    PlayerAudioBridgeSystem::update(m_registry, dt);
    FallRollEffectSystem::update(m_registry, dt);

    if (m_services.audioEngine) {
        AudioSyncSystem::update(m_registry, *m_services.audioEngine);
    }

    // Steve sync, animation, and transform hierarchy
    if (m_services.cameraController) {
        SteveSyncSystem::update(m_registry, *m_services.cameraController);
    }
    SteveAnimationSystem::update(m_registry, dt);
    MobAnimationSystem::update(m_registry, dt);
    TransformHierarchySystem::update(m_registry);
}

void GameplayScene::runOneTick() {
    if (m_services.world != nullptr) {
        FluidTickSystem::update(*m_services.world, m_tickClock.tickIndex());
    }
}

} // namespace ecs
