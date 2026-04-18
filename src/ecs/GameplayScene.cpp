#include "GameplayScene.h"
#include "util/InputFrameState.h"
#include "components/Components.h"
#include "systems/player/CharacterPhysicsSystem.h"
#include "systems/player/InputSamplingSystem.h"
#include "systems/player/PlayerIntentBuildSystem.h"
#include "systems/player/PlayerRuntimeUpdateSystem.h"
#include "systems/audio/AudioSyncSystem.h"
#include "systems/interaction/BlockInteractionBridgeSystem.h"
#include "systems/item/DropCollectionBridgeSystem.h"
#include "systems/particle/ParticleCleanupSystem.h"
#include "systems/particle/ParticleSimulationSystem.h"
#include "systems/particle/ParticleSpawnSystem.h"
#include "systems/player/PlayerFacadeSyncSystem.h"
#include "systems/audio/PlayerAudioBridgeSystem.h"
#include "../player/Player.h"
#include "../physics/PhysicsSystem.h"

namespace ecs {

void GameplayScene::initLocalPlayer() {
    m_localPlayer = m_registry.create();
    m_registry.emplace<LocalPlayerTag>(m_localPlayer);
    m_registry.emplace<MoveIntentComponent>(m_localPlayer);
    m_registry.emplace<LookIntentComponent>(m_localPlayer);
    m_registry.emplace<HotbarIntentComponent>(m_localPlayer);
    m_registry.emplace<BlockActionIntentComponent>(m_localPlayer);

    auto& transform = m_registry.emplace<TransformComponent>(m_localPlayer);
    auto& physicsBody = m_registry.emplace<PhysicsBodyComponent>(m_localPlayer);
    auto& controller = m_registry.emplace<CharacterControllerComponent>(m_localPlayer);
    auto& camera = m_registry.emplace<CameraStateComponent>(m_localPlayer);
    auto& sprintFov = m_registry.emplace<SprintFovComponent>(m_localPlayer);
    auto& inventory = m_registry.emplace<InventoryComponent>(m_localPlayer);
    m_registry.emplace<BlockTargetComponent>(m_localPlayer);
    m_registry.emplace<BlockBreakComponent>(m_localPlayer);
    m_registry.emplace<BlockInteractionRuntimeComponent>(m_localPlayer);
    m_registry.emplace<FlightStateComponent>(m_localPlayer);
    m_registry.emplace<FootstepStateComponent>(m_localPlayer);
    m_registry.emplace<LandingStateComponent>(m_localPlayer);

    if (m_services.player != nullptr) {
        Player& player = *m_services.player;
        transform.position = player.getPosition();
        transform.eyeHeight = player.getEyeHeight();
        physicsBody.body = player.getPhysicsBody();
        inventory.selectedHotbarSlot = player.getInventory().getSelectedSlot();
        controller.tuning = m_services.physicsSystem ? m_services.physicsSystem->tuning : controller.tuning;
        controller.standEyeHeight = transform.eyeHeight;
        physicsBody.body.eyeOffsetY = transform.eyeHeight;

        Camera& playerCamera = player.getCamera();
        camera.yaw = playerCamera.getYaw();
        camera.pitch = playerCamera.getPitch();
        camera.fov = playerCamera.getFOV();
        camera.sensitivity = playerCamera.getSensitivity();
        camera.front = playerCamera.getFront();
        camera.right = playerCamera.getRight();
        camera.up = playerCamera.getUp();
        sprintFov.walkFov = playerCamera.getFOV();
    }

    if (!m_registry.ctxHas<InputFrameState>()) {
        m_registry.ctxSet<InputFrameState>();
    }
}

void GameplayScene::runFixedUpdate(float dt) {
    if (m_services.inputContextManager) {
        InputSamplingSystem::update(m_registry, *m_services.inputContextManager);
    }

    PlayerIntentBuildSystem::update(m_registry);

    if (m_services.physicsSystem) {
        CharacterPhysicsSystem::update(m_registry, *m_services.physicsSystem, dt);
    }

    PlayerRuntimeUpdateSystem::update(m_registry, dt);

    if (m_services.player && m_services.world && m_services.dropSystem && m_services.uiRenderer) {
        BlockInteractionBridgeSystem::update(m_registry,
                                             *m_services.player,
                                             *m_services.world,
                                             *m_services.dropSystem,
                                             *m_services.uiRenderer,
                                             dt);
    }

    if (m_services.player && m_services.world && m_services.dropSystem) {
        DropCollectionBridgeSystem::update(m_registry,
                                           *m_services.dropSystem,
                                           *m_services.world,
                                           *m_services.player,
                                           dt);
    }

    ParticleSpawnSystem::update(m_registry);
    ParticleSimulationSystem::update(m_registry, dt);
    ParticleCleanupSystem::update(m_registry);

    if (m_services.player) {
        PlayerFacadeSyncSystem::update(m_registry, *m_services.player, dt);
        PlayerAudioBridgeSystem::update(m_registry, *m_services.player, dt);
    }

    if (m_services.audioEngine) {
        AudioSyncSystem::update(m_registry, *m_services.audioEngine);
    }
}

void GameplayScene::runOneTick() {
    // Placeholder for future tick-based systems:
    //   - BlockScheduledUpdateSystem
    //   - WorldRuleTickSystem
    //   - GameplayScheduledEventSystem
}

} // namespace ecs
