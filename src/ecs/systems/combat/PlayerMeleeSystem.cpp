#include "PlayerMeleeSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../../util/DamageEventBuffer.h"
#include "../../util/SimulationDistance.h"

namespace ecs {
namespace {

bool intersectRayAabb(const glm::vec3& origin,
                      const glm::vec3& direction,
                      const glm::vec3& min,
                      const glm::vec3& max,
                      const float maxDistance,
                      float& outDistance) {
    float tMin = 0.0f;
    float tMax = maxDistance;

    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = direction[axis];
        const float aabbMin = min[axis];
        const float aabbMax = max[axis];

        if (std::abs(d) < 0.0001f) {
            if (o < aabbMin || o > aabbMax) {
                return false;
            }
            continue;
        }

        const float invD = 1.0f / d;
        float t1 = (aabbMin - o) * invD;
        float t2 = (aabbMax - o) * invD;
        if (t1 > t2) {
            std::swap(t1, t2);
        }

        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }

    outDistance = tMin >= 0.0f ? tMin : tMax;
    return outDistance >= 0.0f && outDistance <= maxDistance;
}

void mobBounds(entt::registry& registry,
               const entt::entity mob,
               const TransformComponent& transform,
               glm::vec3& outMin,
               glm::vec3& outMax) {
    glm::vec3 halfExtents(0.3f, 0.9f, 0.3f);
    glm::vec3 colliderOffset(0.0f, 0.9f, 0.0f);

    if (const auto* body = registry.try_get<PhysicsBodyComponent>(mob)) {
        halfExtents = body->body.halfExtents;
        colliderOffset = body->body.colliderOffset;
    }

    const glm::vec3 center = transform.position + colliderOffset;
    outMin = center - halfExtents;
    outMax = center + halfExtents;
}

} // namespace

void PlayerMeleeSystem::update(SystemContext& ctx) {
    auto& registry = ctx.registry;
    auto& reg = registry.registry();
    const float dt = ctx.dt;

    auto playerView = reg.view<LocalPlayerTag,
                               TransformComponent,
                               CameraStateComponent,
                               BlockActionIntentComponent,
                               BlockTargetComponent,
                               MeleeAttackComponent>();

    for (const entt::entity player : playerView) {
        const auto& transform = playerView.get<TransformComponent>(player);
        const auto& camera = playerView.get<CameraStateComponent>(player);
        auto& intent = playerView.get<BlockActionIntentComponent>(player);
        auto& blockTarget = playerView.get<BlockTargetComponent>(player);
        auto& melee = playerView.get<MeleeAttackComponent>(player);

        melee.cooldownRemaining = std::max(0.0f, melee.cooldownRemaining - dt);
        if (!intent.wantsBreak || melee.cooldownRemaining > 0.0f) {
            continue;
        }

        glm::vec3 direction = camera.front;
        if (glm::length(direction) < 0.001f) {
            continue;
        }
        direction = glm::normalize(direction);

        const glm::vec3 origin = transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f);
        float reachLimit = melee.reach;
        if (blockTarget.hasTarget) {
            const glm::vec3 blockCenter = glm::vec3(blockTarget.targetBlock) + glm::vec3(0.5f);
            reachLimit = std::min(reachLimit, glm::length(blockCenter - origin));
        }

        entt::entity bestTarget = entt::null;
        float bestDistance = std::numeric_limits<float>::max();
        auto mobView = reg.view<MobTag, TransformComponent, HealthComponent>();
        for (const entt::entity mob : mobView) {
            if (mob == player) {
                continue;
            }
            if (!simulation::isEntityTicking(ctx, mob)) {
                continue;
            }

            const auto& mobHealth = mobView.get<HealthComponent>(mob);
            if (mobHealth.current <= 0) {
                continue;
            }

            glm::vec3 minBounds;
            glm::vec3 maxBounds;
            mobBounds(reg, mob, mobView.get<TransformComponent>(mob), minBounds, maxBounds);

            float hitDistance = 0.0f;
            if (!intersectRayAabb(origin, direction, minBounds, maxBounds, reachLimit, hitDistance)) {
                continue;
            }

            if (hitDistance < bestDistance) {
                bestDistance = hitDistance;
                bestTarget = mob;
            }
        }

        if (bestTarget == entt::null) {
            continue;
        }

        ensureDamageEventBus(registry).push({bestTarget, player, melee.damage});
        melee.cooldownRemaining = melee.cooldownSeconds;
        intent.wantsBreak = false;
        blockTarget.hasTarget = false;

        if (auto* runtime = reg.try_get<BlockInteractionRuntimeComponent>(player)) {
            ++runtime->heldItemSwingSequence;
        }
    }
}

} // namespace ecs
