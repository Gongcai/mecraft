#include "PlayerQuery.h"

#include "../GameplayRegistry.h"
#include "../components/TagComponents.h"
#include "../components/InputComponents.h"
#include "../components/TransformComponents.h"
#include "../components/PhysicsComponents.h"
#include "../components/CameraComponents.h"
#include "../components/InteractionComponents.h"
#include "../components/PlayerStateComponents.h"
#include "../../player/Inventory.h"

namespace ecs {

PlayerQuery::PlayerQuery(const GameplayRegistry& registry) : m_registry(registry) {}

entt::entity PlayerQuery::findPlayer() const {
    auto view = m_registry.view<LocalPlayerTag>();
    auto it = view.begin();
    return (it != view.end()) ? *it : entt::null;
}

bool PlayerQuery::isValid() const {
    return findPlayer() != entt::null;
}

// ── Position & Transform ──

glm::vec3 PlayerQuery::getPosition() const {
    auto e = findPlayer();
    if (auto* t = m_registry.try_get<TransformComponent>(e))
        return t->position;
    return {};
}

glm::vec3 PlayerQuery::getEyePosition() const {
    auto e = findPlayer();
    if (auto* t = m_registry.try_get<TransformComponent>(e))
        return t->position + glm::vec3(0.0f, t->eyeHeight, 0.0f);
    return {};
}

float PlayerQuery::getEyeHeight() const {
    auto e = findPlayer();
    if (auto* t = m_registry.try_get<TransformComponent>(e))
        return t->eyeHeight;
    return 1.62f;
}

// ── Physics ──

bool PlayerQuery::isOnGround() const {
    auto e = findPlayer();
    if (auto* p = m_registry.try_get<PhysicsBodyComponent>(e))
        return p->body.isGrounded;
    return false;
}

bool PlayerQuery::isFullySubmerged() const {
    auto e = findPlayer();
    if (auto* p = m_registry.try_get<PhysicsBodyComponent>(e))
        return p->body.isFullySubmerged;
    return false;
}

bool PlayerQuery::isEyesInWater() const {
    auto e = findPlayer();
    if (auto* p = m_registry.try_get<PhysicsBodyComponent>(e))
        return p->body.isEyesInWater;
    return false;
}

glm::vec3 PlayerQuery::getVelocity() const {
    auto e = findPlayer();
    if (auto* p = m_registry.try_get<PhysicsBodyComponent>(e))
        return p->body.velocity;
    return {};
}

// ── Camera ──

glm::vec3 PlayerQuery::getCameraFront() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->front;
    return {0.0f, 0.0f, -1.0f};
}

glm::vec3 PlayerQuery::getCameraRight() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->right;
    return {1.0f, 0.0f, 0.0f};
}

glm::vec3 PlayerQuery::getCameraUp() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->up;
    return {0.0f, 1.0f, 0.0f};
}

float PlayerQuery::getCameraYaw() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->yaw;
    return -90.0f;
}

float PlayerQuery::getCameraPitch() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->pitch;
    return 0.0f;
}

float PlayerQuery::getCameraFOV() const {
    auto e = findPlayer();
    if (auto* c = m_registry.try_get<CameraStateComponent>(e))
        return c->fov;
    return 75.0f;
}

// ── Movement State ──

bool PlayerQuery::isMoving() const {
    auto e = findPlayer();
    auto* move = m_registry.try_get<MoveIntentComponent>(e);
    auto* physics = m_registry.try_get<PhysicsBodyComponent>(e);
    if (move && physics) {
        const bool hasMoveInput = (move->move.x != 0.0f || move->move.y != 0.0f);
        return hasMoveInput && physics->body.isGrounded;
    }
    return false;
}

bool PlayerQuery::isSprinting() const {
    auto e = findPlayer();
    if (auto* m = m_registry.try_get<MoveIntentComponent>(e))
        return m->wantsSprint;
    return false;
}

bool PlayerQuery::isCrouching() const {
    auto e = findPlayer();
    if (auto* m = m_registry.try_get<MoveIntentComponent>(e))
        return m->wantsCrouch;
    return false;
}

bool PlayerQuery::isJustLanded() const {
    auto e = findPlayer();
    if (auto* l = m_registry.try_get<LandingStateComponent>(e))
        return l->justLanded;
    return false;
}

float PlayerQuery::getLandingImpactSpeed() const {
    auto e = findPlayer();
    if (auto* l = m_registry.try_get<LandingStateComponent>(e))
        return l->impactSpeed;
    return 0.0f;
}

// ── Block Interaction ──

bool PlayerQuery::hasTargetBlock() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockTargetComponent>(e))
        return b->hasTarget;
    return false;
}

glm::ivec3 PlayerQuery::getTargetBlock() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockTargetComponent>(e))
        return b->targetBlock;
    return {};
}

glm::ivec3 PlayerQuery::getTargetHitNormal() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockTargetComponent>(e))
        return b->hitNormal;
    return {};
}

bool PlayerQuery::hasBlockBreakProgress() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockBreakComponent>(e))
        return b->active;
    return false;
}

float PlayerQuery::getBlockBreakProgress() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockBreakComponent>(e))
        return b->progress01;
    return 0.0f;
}

glm::ivec3 PlayerQuery::getBreakTargetBlock() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockBreakComponent>(e))
        return b->blockPos;
    return {};
}

glm::ivec3 PlayerQuery::getBreakTargetHitNormal() const {
    auto e = findPlayer();
    if (auto* b = m_registry.try_get<BlockBreakComponent>(e))
        return b->hitNormal;
    return {};
}

// ── Stats ──

int PlayerQuery::getHealth() const {
    auto e = findPlayer();
    if (auto* h = m_registry.try_get<HealthComponent>(e))
        return h->current;
    return 20;
}

int PlayerQuery::getMaxHealth() const {
    auto e = findPlayer();
    if (auto* h = m_registry.try_get<HealthComponent>(e))
        return h->max;
    return 20;
}

int PlayerQuery::getArmor() const {
    auto e = findPlayer();
    if (auto* a = m_registry.try_get<ArmorComponent>(e))
        return a->current;
    return 0;
}

int PlayerQuery::getMaxArmor() const {
    auto e = findPlayer();
    if (auto* a = m_registry.try_get<ArmorComponent>(e))
        return a->max;
    return 20;
}

int PlayerQuery::getFood() const {
    auto e = findPlayer();
    if (auto* f = m_registry.try_get<FoodComponent>(e))
        return f->current;
    return 20;
}

int PlayerQuery::getMaxFood() const {
    auto e = findPlayer();
    if (auto* f = m_registry.try_get<FoodComponent>(e))
        return f->max;
    return 20;
}

// ── View Bob ──

float PlayerQuery::getEyeBobAmplitude() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->amplitude;
    return 0.25f;
}

float PlayerQuery::getEyeBobHorizontalAmplitude() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->horizontalAmplitude;
    return 0.02f;
}

float PlayerQuery::getEyeBobFrequency() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->frequency;
    return 6.0f;
}

float PlayerQuery::getEyeBobPhaseOffset() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->phaseOffset;
    return 0.0f;
}

float PlayerQuery::getEyeBobBlend() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->blend;
    return 0.0f;
}

float PlayerQuery::getEyeBobVerticalOffset() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->verticalOffset;
    return 0.0f;
}

float PlayerQuery::getEyeBobHorizontalOffset() const {
    auto e = findPlayer();
    if (auto* v = m_registry.try_get<ViewBobComponent>(e))
        return v->horizontalOffset;
    return 0.0f;
}

// ── Hurt Effect ──

bool PlayerQuery::hasClassicHurtEffectPending() const {
    auto e = findPlayer();
    if (auto* h = m_registry.try_get<HurtEffectComponent>(e))
        return h->classicHurtEffectPending;
    return false;
}

// ── Inventory ──

const Inventory& PlayerQuery::getInventory() const {
    auto e = findPlayer();
    if (auto* inv = m_registry.try_get<InventoryDataComponent>(e))
        return inv->inventory;
    static Inventory s_empty;
    return s_empty;
}

} // namespace ecs
