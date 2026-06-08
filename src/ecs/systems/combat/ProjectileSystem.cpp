#include "ProjectileSystem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../entity/EntityFactory.h"
#include "../../components/NetworkComponents.h"
#include "../../util/DamageEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../util/ProjectileDefinitions.h"
#include "../../../item/Item.h"
#include "../../../world/IWorldView.h"
#include "../../../world/block/Block.h"
#include "../../../world/chunk/Chunk.h"

namespace ecs {
namespace {

constexpr float kProjectileStepLength = 0.25f;

bool isSolidBlockAt(const IWorldView& worldView, const glm::vec3& position, BlockID& outBlock) {
    outBlock = 0;
    const int x = static_cast<int>(std::floor(position.x));
    const int y = static_cast<int>(std::floor(position.y));
    const int z = static_cast<int>(std::floor(position.z));
    if (y < 0 || y >= Chunk::SIZE_Y) {
        return true;
    }

    const BlockID block = worldView.getBlock(x, y, z);
    if (block != BlockIds::AIR && BlockRegistry::getFast(block).isSolid) {
        outBlock = block;
        return true;
    }
    return false;
}

void emitProjectileImpactParticles(GameplayRegistry& registry,
                                   const glm::vec3& position,
                                   const BlockID blockType) {
    if (blockType == 0) {
        return;
    }
    ensureParticleEventBus(registry).push(makeImpactParticleEvent(position, blockType));
}

void queueProjectileDespawn(entt::registry& reg,
                            const entt::entity projectile,
                            std::vector<entt::entity>& destroyList) {
    if (reg.all_of<EntityNetIdComponent>(projectile)) {
        reg.emplace_or_replace<PendingNetworkDespawnTag>(projectile);
        return;
    }
    destroyList.push_back(projectile);
}

void queueProjectileImpactDespawn(entt::registry& reg,
                                  const entt::entity projectile,
                                  const glm::vec3& position,
                                  const BlockID particleBlock,
                                  std::vector<entt::entity>& destroyList) {
    if (reg.all_of<EntityNetIdComponent>(projectile)) {
        reg.emplace_or_replace<EntityImpactComponent>(projectile, position, particleBlock);
        reg.emplace_or_replace<PendingNetworkDespawnTag>(projectile);
        return;
    }
    destroyList.push_back(projectile);
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

float distanceSqToAabb(const glm::vec3& point, const glm::vec3& minBounds, const glm::vec3& maxBounds) {
    float distanceSq = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (point[axis] < minBounds[axis]) {
            const float delta = minBounds[axis] - point[axis];
            distanceSq += delta * delta;
        } else if (point[axis] > maxBounds[axis]) {
            const float delta = point[axis] - maxBounds[axis];
            distanceSq += delta * delta;
        }
    }
    return distanceSq;
}

entt::entity findProjectileTarget(entt::registry& reg,
                                  const entt::entity projectile,
                                  const ProjectileComponent& projectileData,
                                  const glm::vec3& position) {
    entt::entity best = entt::null;
    float bestDistSq = projectileData.hitRadius * projectileData.hitRadius;

    auto mobView = reg.view<MobTag, TransformComponent, HealthComponent>();
    for (const entt::entity mob : mobView) {
        if (mob == projectile || mob == projectileData.owner) {
            continue;
        }

        const auto& health = mobView.get<HealthComponent>(mob);
        if (health.current <= 0) {
            continue;
        }

        glm::vec3 minBounds;
        glm::vec3 maxBounds;
        mobBounds(reg, mob, mobView.get<TransformComponent>(mob), minBounds, maxBounds);

        const float distSq = distanceSqToAabb(position, minBounds, maxBounds);
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            best = mob;
        }
    }

    return best;
}

bool isCreativeThrower(GameplayRegistry& registry, const entt::entity player) {
    if (const auto* playerMode = registry.try_get<PlayerModeComponent>(player);
        playerMode != nullptr && playerMode->creative) {
        return true;
    }
    if (!registry.ctxHas<GameplayRuntimeContext>()) {
        return false;
    }
    return registry.ctxGet<GameplayRuntimeContext>().gameplayMode == GameplayMode::Creative;
}

bool tryUseSelectedThrowable(InventoryComponent& inventoryState,
                             InventoryDataComponent& inventoryData,
                             const bool creativeMode,
                             ProjectileDefinition& outDefinition) {
    inventoryData.inventory.setSelectedSlot(inventoryState.selectedHotbarSlot);
    if (!getThrowableProjectileDefinition(inventoryData.inventory.getSelectedItem(), outDefinition)) {
        return false;
    }
    if (creativeMode) {
        return true;
    }
    return inventoryData.inventory.consumeSelectedOne();
}

} // namespace

void ProjectileSystem::update(SystemContext& ctx) {
    if (!ctx.services.world || !ctx.services.worldView || ctx.dt <= 0.0f) {
        return;
    }

    auto& registry = ctx.registry;
    auto& reg = registry.registry();
    const IWorldView& worldView = *ctx.services.worldView;
    const float dt = ctx.dt;

    auto throwerView = reg.view<LocalPlayerTag,
                                TransformComponent,
                                CameraStateComponent,
                                BlockActionIntentComponent,
                                InventoryComponent,
                                InventoryDataComponent,
                                ProjectileThrowerComponent>();
    for (const entt::entity player : throwerView) {
        auto& thrower = throwerView.get<ProjectileThrowerComponent>(player);
        thrower.cooldownRemaining = std::max(0.0f, thrower.cooldownRemaining - dt);

        auto& intent = throwerView.get<BlockActionIntentComponent>(player);
        if (!intent.wantsPlace || thrower.cooldownRemaining > 0.0f) {
            continue;
        }

        auto& inventoryState = throwerView.get<InventoryComponent>(player);
        auto& inventoryData = throwerView.get<InventoryDataComponent>(player);
        ProjectileDefinition projectileDefinition;
        if (!tryUseSelectedThrowable(inventoryState,
                                     inventoryData,
                                     isCreativeThrower(registry, player),
                                     projectileDefinition)) {
            continue;
        }

        const auto& transform = throwerView.get<TransformComponent>(player);
        const auto& camera = throwerView.get<CameraStateComponent>(player);
        glm::vec3 direction = camera.front;
        if (glm::length(direction) < 0.001f) {
            direction = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            direction = glm::normalize(direction);
        }

        const glm::vec3 origin = transform.position + glm::vec3(0.0f, transform.eyeHeight, 0.0f);
        const glm::vec3 spawnPosition = origin + direction * projectileDefinition.spawnForwardOffset;
        const glm::vec3 velocity =
            direction * projectileDefinition.throwSpeed +
            glm::vec3(0.0f, projectileDefinition.upwardBias, 0.0f);

        EntityFactory::createProjectile(registry, player, spawnPosition, velocity, projectileDefinition);
        thrower.cooldownRemaining = thrower.cooldownSeconds;
        intent.wantsPlace = false;
        if (auto* runtime = reg.try_get<BlockInteractionRuntimeComponent>(player)) {
            ++runtime->heldItemSwingSequence;
        }
    }

    std::vector<entt::entity> destroyList;
    auto projectileView = reg.view<ProjectileTag,
                                  ProjectileComponent,
                                  TransformComponent,
                                  VelocityComponent,
                                  LifetimeComponent,
                                  SpinVisualComponent>();
    for (const entt::entity projectile : projectileView) {
        auto& projectileData = projectileView.get<ProjectileComponent>(projectile);
        auto& transform = projectileView.get<TransformComponent>(projectile);
        auto& velocity = projectileView.get<VelocityComponent>(projectile);
        auto& lifetime = projectileView.get<LifetimeComponent>(projectile);
        auto& spin = projectileView.get<SpinVisualComponent>(projectile);

        if (lifetime.ageSeconds <= 0.0f) {
            lifetime.ageSeconds += dt;
            continue;
        }

        lifetime.ageSeconds += dt;
        if (lifetime.ageSeconds >= lifetime.lifeTimeSeconds) {
            queueProjectileDespawn(reg, projectile, destroyList);
            continue;
        }

        velocity.velocity.y -= projectileData.gravity * dt;
        spin.yawRadians = std::fmod(spin.yawRadians + spin.spinSpeedRadians * dt, 6.28318530718f);

        const glm::vec3 delta = velocity.velocity * dt;
        const float distance = glm::length(delta);
        const int steps = std::max(1, static_cast<int>(std::ceil(distance / kProjectileStepLength)));
        const glm::vec3 step = delta / static_cast<float>(steps);

        bool destroyed = false;
        for (int i = 0; i < steps; ++i) {
            transform.position += step;

            BlockID hitBlock = 0;
            if (isSolidBlockAt(worldView, transform.position, hitBlock)) {
                emitProjectileImpactParticles(registry, transform.position, hitBlock);
                queueProjectileImpactDespawn(reg, projectile, transform.position, hitBlock, destroyList);
                destroyed = true;
                break;
            }

            const entt::entity target = findProjectileTarget(reg, projectile, projectileData, transform.position);
            if (target != entt::null) {
                ensureDamageEventBus(registry).push({target, projectileData.owner, projectileData.damage});
                const BlockID impactBlock = projectileData.entityImpactParticleBlock != 0
                    ? projectileData.entityImpactParticleBlock
                    : defaultProjectileEntityImpactParticleBlock();
                emitProjectileImpactParticles(registry, transform.position, impactBlock);
                queueProjectileImpactDespawn(reg, projectile, transform.position, impactBlock, destroyList);
                destroyed = true;
                break;
            }
        }

        if (destroyed) {
            continue;
        }
    }

    for (const entt::entity projectile : destroyList) {
        if (reg.valid(projectile)) {
            registry.destroy(projectile);
        }
    }
}

} // namespace ecs
