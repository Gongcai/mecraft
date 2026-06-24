#include "MobAISystem.h"

#include "../../components/Components.h"
#include "../../util/DamageEventBuffer.h"
#include "../../util/SimulationDistance.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace ecs {
namespace {

float horizontalDistanceSq(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec2 delta(a.x - b.x, a.z - b.z);
    return glm::dot(delta, delta);
}

float yawFromMove(const glm::vec2& move) {
    if (glm::length(move) <= 0.001f) {
        return 0.0f;
    }
    const float angle = std::atan2(move.y, move.x);
    return glm::degrees(angle);
}

bool isTargetUsable(entt::registry& reg,
                    const entt::entity target,
                    const glm::vec3& mobPos,
                    const float loseRange) {
    if (target == entt::null || !reg.valid(target) ||
        !reg.all_of<LocalPlayerTag, TransformComponent, HealthComponent>(target)) {
        return false;
    }
    const auto& health = reg.get<HealthComponent>(target);
    if (health.current <= 0) {
        return false;
    }
    const auto& targetTransform = reg.get<TransformComponent>(target);
    return horizontalDistanceSq(mobPos, targetTransform.position) <= loseRange * loseRange;
}

entt::entity findNearestTarget(entt::registry& reg,
                               const glm::vec3& mobPos,
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
        const float distSq = horizontalDistanceSq(mobPos, targetTransform.position);
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            best = player;
        }
    }

    return best;
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
    if (dt <= 0.0f) return;

    auto& reg = registry.registry();
    auto view = reg.view<MobTag, TransformComponent, MobAIComponent, MoveIntentComponent>();

    for (auto entity : view) {
        if (!simulation::isEntityTicking(ctx, entity)) {
            continue;
        }

        const auto& transform = view.get<TransformComponent>(entity);
        auto& ai = view.get<MobAIComponent>(entity);
        auto& moveIntent = view.get<MoveIntentComponent>(entity);

        ai.attackCooldownRemaining = std::max(0.0f, ai.attackCooldownRemaining - dt);
        ai.wanderTimer -= dt;

        if (!ai.targetsPlayers) {
            ai.target = entt::null;
        } else if (!isTargetUsable(reg, ai.target, transform.position, ai.loseTargetRange)) {
            ai.target = findNearestTarget(reg, transform.position, ai.acquisitionRange);
        }

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
            if (distance > 0.001f) {
                toTarget /= distance;
                ai.yaw = yawFromMove(toTarget);
            }

            if (distance <= ai.attackRange) {
                ai.state = MobAIComponent::State::Attack;
                if (ai.attackCooldownRemaining <= 0.0f) {
                    ensureDamageEventBus(registry).push({ai.target, entity, ai.attackDamage});
                    ai.attackCooldownRemaining = ai.attackCooldownSeconds;
                }
                continue;
            }

            ai.state = MobAIComponent::State::Pursue;
            moveIntent.move = toTarget * ai.pursueSpeed;
            continue;
        }

        if (ai.wanderTimer <= 0.0f) {
            chooseWanderDirection(ai);
        }
        moveIntent.move = ai.wanderDir * ai.wanderSpeed;
    }
}

} // namespace ecs
