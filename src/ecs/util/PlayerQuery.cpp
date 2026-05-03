#include "PlayerQuery.h"

#include "../GameplayRegistry.h"
#include "../components/Components.h"
#include "../../physics/PhysicsInfo.h"
#include "../../player/Inventory.h"

namespace ecs {

PlayerQuery::PlayerQuery(const GameplayRegistry& registry)
    : m_registry(registry) {}

bool PlayerQuery::isValid() const {
    auto view = m_registry.view<LocalPlayerTag>();
    return view.begin() != view.end();
}

// ── Position & Transform ──

glm::vec3 PlayerQuery::getPosition() const {
    auto view = m_registry.view<LocalPlayerTag, TransformComponent>();
    for (auto e : view) return view.get<TransformComponent>(e).position;
    return {};
}

glm::vec3 PlayerQuery::getEyePosition() const {
    auto view = m_registry.view<LocalPlayerTag, TransformComponent>();
    for (auto e : view) {
        const auto& t = view.get<TransformComponent>(e);
        return t.position + glm::vec3(0.0f, t.eyeHeight, 0.0f);
    }
    return {};
}

float PlayerQuery::getEyeHeight() const {
    auto view = m_registry.view<LocalPlayerTag, TransformComponent>();
    for (auto e : view) return view.get<TransformComponent>(e).eyeHeight;
    return 1.62f;
}

// ── Physics ──

bool PlayerQuery::isOnGround() const {
    auto view = m_registry.view<LocalPlayerTag, PhysicsBodyComponent>();
    for (auto e : view) return view.get<PhysicsBodyComponent>(e).body.isGrounded;
    return false;
}

bool PlayerQuery::isFullySubmerged() const {
    auto view = m_registry.view<LocalPlayerTag, PhysicsBodyComponent>();
    for (auto e : view) return view.get<PhysicsBodyComponent>(e).body.isFullySubmerged;
    return false;
}

bool PlayerQuery::isEyesInWater() const {
    auto view = m_registry.view<LocalPlayerTag, PhysicsBodyComponent>();
    for (auto e : view) return view.get<PhysicsBodyComponent>(e).body.isEyesInWater;
    return false;
}

glm::vec3 PlayerQuery::getVelocity() const {
    auto view = m_registry.view<LocalPlayerTag, PhysicsBodyComponent>();
    for (auto e : view) return view.get<PhysicsBodyComponent>(e).body.velocity;
    return {};
}

// ── Camera ──

glm::vec3 PlayerQuery::getCameraFront() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).front;
    return {0.0f, 0.0f, -1.0f};
}

glm::vec3 PlayerQuery::getCameraRight() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).right;
    return {1.0f, 0.0f, 0.0f};
}

glm::vec3 PlayerQuery::getCameraUp() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).up;
    return {0.0f, 1.0f, 0.0f};
}

float PlayerQuery::getCameraYaw() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).yaw;
    return -90.0f;
}

float PlayerQuery::getCameraPitch() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).pitch;
    return 0.0f;
}

float PlayerQuery::getCameraFOV() const {
    auto view = m_registry.view<LocalPlayerTag, CameraStateComponent>();
    for (auto e : view) return view.get<CameraStateComponent>(e).fov;
    return 75.0f;
}

// ── Movement State ──

bool PlayerQuery::isMoving() const {
    auto view = m_registry.view<LocalPlayerTag, MoveIntentComponent, PhysicsBodyComponent>();
    for (auto e : view) {
        const auto& move = view.get<MoveIntentComponent>(e);
        const auto& physics = view.get<PhysicsBodyComponent>(e);
        const bool hasMoveInput = (move.move.x != 0.0f || move.move.y != 0.0f);
        return hasMoveInput && physics.body.isGrounded;
    }
    return false;
}

bool PlayerQuery::isSprinting() const {
    auto view = m_registry.view<LocalPlayerTag, MoveIntentComponent>();
    for (auto e : view) return view.get<MoveIntentComponent>(e).wantsSprint;
    return false;
}

bool PlayerQuery::isCrouching() const {
    auto view = m_registry.view<LocalPlayerTag, MoveIntentComponent>();
    for (auto e : view) return view.get<MoveIntentComponent>(e).wantsCrouch;
    return false;
}

bool PlayerQuery::isJustLanded() const {
    auto view = m_registry.view<LocalPlayerTag, LandingStateComponent>();
    for (auto e : view) return view.get<LandingStateComponent>(e).justLanded;
    return false;
}

float PlayerQuery::getLandingImpactSpeed() const {
    auto view = m_registry.view<LocalPlayerTag, LandingStateComponent>();
    for (auto e : view) return view.get<LandingStateComponent>(e).impactSpeed;
    return 0.0f;
}

// ── Block Interaction ──

bool PlayerQuery::hasTargetBlock() const {
    auto view = m_registry.view<LocalPlayerTag, BlockTargetComponent>();
    for (auto e : view) return view.get<BlockTargetComponent>(e).hasTarget;
    return false;
}

glm::ivec3 PlayerQuery::getTargetBlock() const {
    auto view = m_registry.view<LocalPlayerTag, BlockTargetComponent>();
    for (auto e : view) return view.get<BlockTargetComponent>(e).targetBlock;
    return {};
}

bool PlayerQuery::hasBlockBreakProgress() const {
    auto view = m_registry.view<LocalPlayerTag, BlockBreakComponent>();
    for (auto e : view) return view.get<BlockBreakComponent>(e).active;
    return false;
}

float PlayerQuery::getBlockBreakProgress() const {
    auto view = m_registry.view<LocalPlayerTag, BlockBreakComponent>();
    for (auto e : view) return view.get<BlockBreakComponent>(e).progress01;
    return 0.0f;
}

glm::ivec3 PlayerQuery::getBreakTargetBlock() const {
    auto view = m_registry.view<LocalPlayerTag, BlockBreakComponent>();
    for (auto e : view) return view.get<BlockBreakComponent>(e).blockPos;
    return {};
}

// ── Stats ──

int PlayerQuery::getHealth() const {
    auto view = m_registry.view<LocalPlayerTag, HealthComponent>();
    for (auto e : view) return view.get<HealthComponent>(e).current;
    return 20;
}

int PlayerQuery::getMaxHealth() const {
    auto view = m_registry.view<LocalPlayerTag, HealthComponent>();
    for (auto e : view) return view.get<HealthComponent>(e).max;
    return 20;
}

int PlayerQuery::getArmor() const {
    auto view = m_registry.view<LocalPlayerTag, ArmorComponent>();
    for (auto e : view) return view.get<ArmorComponent>(e).current;
    return 0;
}

int PlayerQuery::getMaxArmor() const {
    auto view = m_registry.view<LocalPlayerTag, ArmorComponent>();
    for (auto e : view) return view.get<ArmorComponent>(e).max;
    return 20;
}

int PlayerQuery::getFood() const {
    auto view = m_registry.view<LocalPlayerTag, FoodComponent>();
    for (auto e : view) return view.get<FoodComponent>(e).current;
    return 20;
}

int PlayerQuery::getMaxFood() const {
    auto view = m_registry.view<LocalPlayerTag, FoodComponent>();
    for (auto e : view) return view.get<FoodComponent>(e).max;
    return 20;
}

// ── View Bob ──

float PlayerQuery::getEyeBobAmplitude() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).amplitude;
    return 0.25f;
}

float PlayerQuery::getEyeBobHorizontalAmplitude() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).horizontalAmplitude;
    return 0.02f;
}

float PlayerQuery::getEyeBobFrequency() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).frequency;
    return 6.0f;
}

float PlayerQuery::getEyeBobPhaseOffset() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).phaseOffset;
    return 0.0f;
}

float PlayerQuery::getEyeBobBlend() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).blend;
    return 0.0f;
}

float PlayerQuery::getEyeBobVerticalOffset() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).verticalOffset;
    return 0.0f;
}

float PlayerQuery::getEyeBobHorizontalOffset() const {
    auto view = m_registry.view<LocalPlayerTag, ViewBobComponent>();
    for (auto e : view) return view.get<ViewBobComponent>(e).horizontalOffset;
    return 0.0f;
}

// ── Hurt Effect ──

bool PlayerQuery::hasClassicHurtEffectPending() const {
    auto view = m_registry.view<LocalPlayerTag, HurtEffectComponent>();
    for (auto e : view) return view.get<HurtEffectComponent>(e).classicHurtEffectPending;
    return false;
}

// ── Inventory ──

const Inventory& PlayerQuery::getInventory() const {
    auto view = m_registry.view<LocalPlayerTag, InventoryDataComponent>();
    for (auto e : view) return view.get<InventoryDataComponent>(e).inventory;
    static Inventory s_empty;
    return s_empty;
}

} // namespace ecs
