#include "MobAISystem.h"

#include "../../components/Components.h"
#include "../../util/DamageEventBuffer.h"
#include "../../util/SimulationDistance.h"
#include "../../../world/WorldRaycast.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace ecs {
namespace {

constexpr float kDirectionEpsilon = 0.001f;
constexpr float kLineOfSightSlack = 0.15f;
constexpr float kStuckMovementThresholdSq = 0.0004f;

float horizontalDistanceSq(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec2 delta(a.x - b.x, a.z - b.z);
    return glm::dot(delta, delta);
}

float decreaseTimer(const float value, const float dt) {
    return std::max(0.0f, value - dt);
}

float yawFromMove(const glm::vec2& move) {
    if (glm::length(move) <= kDirectionEpsilon) {
        return 0.0f;
    }
    const float angle = std::atan2(move.y, move.x);
    return glm::degrees(angle);
}

bool isLivingPlayerTarget(entt::registry& reg, const entt::entity target) {
    if (target == entt::null || !reg.valid(target) ||
        !reg.all_of<LocalPlayerTag, TransformComponent, HealthComponent>(target)) {
        return false;
    }
    const auto& health = reg.get<HealthComponent>(target);
    return health.current > 0;
}

bool isTargetUsable(entt::registry& reg, const entt::entity target, const glm::vec3& mobPos, const float loseRange) {
    if (!isLivingPlayerTarget(reg, target)) {
        return false;
    }
    const auto& targetTransform = reg.get<TransformComponent>(target);
    return horizontalDistanceSq(mobPos, targetTransform.position) <= loseRange * loseRange;
}

bool hasLineOfSight(const SystemContext& ctx, const TransformComponent& mobTransform,
                    const TransformComponent& targetTransform) {
    const IWorldView* worldView = ctx.services.worldView.get();
    if (worldView == nullptr) {
        return false;
    }

    const glm::vec3 origin = mobTransform.position + glm::vec3(0.0f, mobTransform.eyeHeight, 0.0f);
    const glm::vec3 target = targetTransform.position + glm::vec3(0.0f, targetTransform.eyeHeight, 0.0f);
    const glm::vec3 delta = target - origin;
    const float distance = glm::length(delta);
    if (distance <= kDirectionEpsilon) {
        return true;
    }

    const RayHit hit =
        raycastWorldView(*worldView, PhysicsInfo(origin, delta / distance), distance, RaycastFluidMode::Ignore);
    return !hit.hit || hit.distance >= distance - kLineOfSightSlack;
}

void clearTarget(MobAIComponent& ai) {
    ai.target = entt::null;
    ai.targetMemoryRemaining = 0.0f;
}

void setRememberedTarget(MobAIComponent& ai, const entt::entity target) {
    ai.target = target;
    ai.targetMemoryRemaining = std::max(0.0f, ai.lineOfSightMemorySeconds);
}

bool refreshCurrentTarget(SystemContext& ctx, entt::registry& reg, const TransformComponent& mobTransform,
                          MobAIComponent& ai) {
    if (ai.target == entt::null) {
        return false;
    }
    if (!isTargetUsable(reg, ai.target, mobTransform.position, ai.loseTargetRange)) {
        clearTarget(ai);
        return false;
    }

    const auto& targetTransform = reg.get<TransformComponent>(ai.target);
    if (hasLineOfSight(ctx, mobTransform, targetTransform)) {
        ai.targetMemoryRemaining = std::max(0.0f, ai.lineOfSightMemorySeconds);
        return true;
    }

    if (ai.targetMemoryRemaining <= 0.0f) {
        clearTarget(ai);
        return false;
    }
    return true;
}

bool tryRetaliate(entt::registry& reg, const entt::entity entity, const TransformComponent& mobTransform,
                  MobAIComponent& ai) {
    if (!ai.retaliates) {
        return false;
    }

    const auto* lastDamage = reg.try_get<LastDamageSourceComponent>(entity);
    if (lastDamage == nullptr || lastDamage->tickIndex == ai.lastDamageSourceTickHandled) {
        return false;
    }
    ai.lastDamageSourceTickHandled = lastDamage->tickIndex;

    const entt::entity source = lastDamage->source;
    if (source == entt::null || source == entity || !isLivingPlayerTarget(reg, source)) {
        return false;
    }

    const auto& sourceTransform = reg.get<TransformComponent>(source);
    if (horizontalDistanceSq(mobTransform.position, sourceTransform.position) > ai.hearingRange * ai.hearingRange) {
        return false;
    }

    setRememberedTarget(ai, source);
    return true;
}

entt::entity findNearestTarget(SystemContext& ctx, entt::registry& reg, const TransformComponent& mobTransform,
                               const float acquisitionRange) {
    entt::entity best = entt::null;
    const float maxDistSq = acquisitionRange * acquisitionRange;
    float bestDistSq = maxDistSq;

    auto playerView = reg.view<LocalPlayerTag, TransformComponent, HealthComponent>();
    for (const entt::entity player : playerView) {
        const auto& health = playerView.get<HealthComponent>(player);
        if (health.current <= 0) {
            continue;
        }

        const auto& targetTransform = playerView.get<TransformComponent>(player);
        const float distSq = horizontalDistanceSq(mobTransform.position, targetTransform.position);
        if (distSq <= bestDistSq && hasLineOfSight(ctx, mobTransform, targetTransform)) {
            bestDistSq = distSq;
            best = player;
        }
    }

    return best;
}

void updateTargetSelection(SystemContext& ctx, entt::registry& reg, const entt::entity entity,
                           const TransformComponent& mobTransform, MobAIComponent& ai) {
    if (!ai.targetsPlayers && ai.target != entt::null && ai.targetMemoryRemaining <= 0.0f) {
        clearTarget(ai);
    }

    tryRetaliate(reg, entity, mobTransform, ai);
    refreshCurrentTarget(ctx, reg, mobTransform, ai);

    if (ai.target == entt::null && ai.targetsPlayers) {
        const entt::entity target = findNearestTarget(ctx, reg, mobTransform, ai.acquisitionRange);
        if (target != entt::null) {
            setRememberedTarget(ai, target);
        }
    }
}

glm::vec2 pursuitDirectionWithObstacleResponse(entt::registry& reg, const entt::entity entity,
                                               const glm::vec3& mobPosition, const glm::vec2& targetDir,
                                               MobAIComponent& ai, MoveIntentComponent& moveIntent, const float dt) {
    const auto* physicsBody = reg.try_get<PhysicsBodyComponent>(entity);
    if (physicsBody == nullptr) {
        return targetDir;
    }

    const glm::vec2 currentPosition(mobPosition.x, mobPosition.z);
    const glm::vec2 previousPosition(ai.lastPosition.x, ai.lastPosition.z);
    const glm::vec2 moved = currentPosition - previousPosition;
    const bool barelyMoved = glm::dot(moved, moved) <= kStuckMovementThresholdSq;
    const bool hasMoveTarget = glm::length(targetDir) > kDirectionEpsilon;
    const bool canJump = physicsBody->body.isGrounded || physicsBody->body.isInWater;
    const bool blockedByWall = physicsBody->body.hitWall && canJump;

    if (hasMoveTarget && (physicsBody->body.hitWall || barelyMoved)) {
        ai.stuckTimer += dt;
    } else {
        ai.stuckTimer = 0.0f;
    }

    const bool stalled = ai.stuckTimer >= ai.stuckJumpThresholdSeconds && canJump;
    if ((blockedByWall || stalled) && ai.jumpCooldownRemaining <= 0.0f) {
        moveIntent.wantsJump = true;
        ai.jumpCooldownRemaining = std::max(0.0f, ai.jumpCooldownSeconds);
        ai.stuckTimer = 0.0f;
        ai.avoidanceTimer = std::max(0.0f, ai.avoidanceSeconds);

        const glm::vec2 perpendicular(-targetDir.y, targetDir.x);
        ai.avoidanceDir = glm::length(ai.avoidanceDir) > kDirectionEpsilon ? -ai.avoidanceDir : perpendicular;
    }

    if (ai.avoidanceTimer > 0.0f && glm::length(ai.avoidanceDir) > kDirectionEpsilon) {
        const glm::vec2 adjusted = targetDir + ai.avoidanceDir * ai.avoidanceStrength;
        if (glm::length(adjusted) > kDirectionEpsilon) {
            return glm::normalize(adjusted);
        }
    }

    return targetDir;
}

void chooseWanderDirection(MobAIComponent& ai) {
    ai.wanderTimer = ai.wanderInterval * (0.8f + 0.4f * (std::rand() / static_cast<float>(RAND_MAX)));

    if ((std::rand() % 100) < 30) {
        ai.wanderDir = {0.0f, 0.0f};
        ai.state = MobAIComponent::State::Idle;
        return;
    }

    const float angle = (std::rand() / static_cast<float>(RAND_MAX)) * glm::two_pi<float>();
    ai.wanderDir = {std::cos(angle), std::sin(angle)};
    ai.yaw = glm::degrees(angle);
    ai.state = MobAIComponent::State::Wander;
}

} // namespace

void MobAISystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    float dt = ctx.dt;
    if (dt <= 0.0f)
        return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, TransformComponent, MobAIComponent, MoveIntentComponent>();

    for (auto entity : view) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }

        const auto& transform = view.get<TransformComponent>(entity);
        auto& ai = view.get<MobAIComponent>(entity);
        auto& moveIntent = view.get<MoveIntentComponent>(entity);

        ai.attackCooldownRemaining = decreaseTimer(ai.attackCooldownRemaining, dt);
        ai.targetMemoryRemaining = decreaseTimer(ai.targetMemoryRemaining, dt);
        ai.jumpCooldownRemaining = decreaseTimer(ai.jumpCooldownRemaining, dt);
        ai.avoidanceTimer = decreaseTimer(ai.avoidanceTimer, dt);
        ai.wanderTimer -= dt;

        updateTargetSelection(ctx, reg, entity, transform, ai);

        moveIntent.move = glm::vec2(0.0f);
        moveIntent.wantsJump = false;
        moveIntent.wantsSprint = false;
        moveIntent.wantsCrouch = false;
        moveIntent.toggleFlightMode = false;

        if (ai.target != entt::null && reg.valid(ai.target)) {
            const auto& targetTransform = reg.get<TransformComponent>(ai.target);
            glm::vec2 toTarget(targetTransform.position.x - transform.position.x,
                               targetTransform.position.z - transform.position.z);
            const float distance = glm::length(toTarget);
            if (distance > kDirectionEpsilon) {
                toTarget /= distance;
                ai.yaw = yawFromMove(toTarget);
            }

            if (distance <= ai.attackRange && hasLineOfSight(ctx, transform, targetTransform)) {
                ai.state = MobAIComponent::State::Attack;
                ai.lastPosition = transform.position;
                if (ai.attackDamage > 0 && ai.attackCooldownRemaining <= 0.0f) {
                    ensureDamageEventBus(registry).push({ai.target, entity, ai.attackDamage});
                    ai.attackCooldownRemaining = ai.attackCooldownSeconds;
                }
                continue;
            }

            ai.state = MobAIComponent::State::Pursue;
            const glm::vec2 pursuitDir =
                pursuitDirectionWithObstacleResponse(reg, entity, transform.position, toTarget, ai, moveIntent, dt);
            moveIntent.move = pursuitDir * ai.pursueSpeed;
            ai.lastPosition = transform.position;
            continue;
        }

        if (ai.wanderTimer <= 0.0f) {
            chooseWanderDirection(ai);
        }
        moveIntent.move = ai.wanderDir * ai.wanderSpeed;
        ai.lastPosition = transform.position;
    }
}

} // namespace ecs
