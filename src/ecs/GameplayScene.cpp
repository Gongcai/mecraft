#include "GameplayScene.h"

#include "util/InputFrameState.h"
#include "components/Components.h"
#include "../physics/PhysicsSystem.h"

namespace ecs {

GameplayScene::GameplayScene()
    : m_pipeline(GameplayPipelineProfile::Client) {}

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
    m_registry.emplace<MeleeAttackComponent>(m_localPlayer);
    m_registry.emplace<ProjectileThrowerComponent>(m_localPlayer);
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
    m_pipeline.runFixedUpdate(m_registry, m_services, dt, m_tickClock.tickIndex());
}

#ifdef MECRAFT_DEBUG
GameplayScene::FixedUpdateProfile GameplayScene::runFixedUpdateProfiled(float dt) {
    return m_pipeline.runFixedUpdateProfiled(m_registry, m_services, dt, m_tickClock.tickIndex());
}
#endif

void GameplayScene::runOneTick() {
    m_pipeline.runOneTick(m_registry, m_services, 0.0f, m_tickClock.tickIndex());
}

} // namespace ecs
