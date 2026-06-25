#include "ProjectileSystem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "../../entity/EntityFactory.h"
#include "../../components/NetworkComponents.h"
#include "../../util/AudioEventBuffer.h"
#include "../../util/DamageEventBuffer.h"
#include "../../util/GameplayRuntimeContext.h"
#include "../../util/ParticleEventBuffer.h"
#include "../../util/ProjectileDefinitions.h"
#include "../../util/SimulationDistance.h"
#include "../../../item/Item.h"
#include "../../../world/IWorldView.h"
#include "../../../world/World.h"
#include "../../../world/block/Block.h"
#include "../../../world/block/BlockCollision.h"
#include "../../../world/block/BlockStateRegistry.h"
#include "../../../world/block/PropIndices.h"
#include "../../../world/chunk/Chunk.h"
#include "../../../world/redstone/RedstoneUpdateQueue.h"

namespace ecs {
namespace {

constexpr float kProjectileStepLength = 0.25f;
constexpr uint64_t kTargetPulseTicks = 4;

bool isCollisionBlockAt(const IWorldView& worldView,
                        const glm::vec3& position,
                        const glm::vec3& halfExtents,
                        StateID& outState,
                        glm::ivec3& outBlockPosition) {
    outState = BlockIds::AIR;
    const glm::vec3 queryMin = position - halfExtents;
    const glm::vec3 queryMax = position + halfExtents;
    if (queryMin.y < 0.0f || queryMax.y >= static_cast<float>(Chunk::SIZE_Y)) {
        outBlockPosition = {
            static_cast<int>(std::floor(position.x)),
            static_cast<int>(std::floor(position.y)),
            static_cast<int>(std::floor(position.z))
        };
        return true;
    }

    const int minX = static_cast<int>(std::floor(queryMin.x));
    const int minY = static_cast<int>(std::floor(queryMin.y));
    const int minZ = static_cast<int>(std::floor(queryMin.z));
    const int maxX = static_cast<int>(std::floor(queryMax.x));
    const int maxY = static_cast<int>(std::floor(queryMax.y));
    const int maxZ = static_cast<int>(std::floor(queryMax.z));

    bool found = false;
    float bestDistanceSq = 0.0f;
    glm::ivec3 bestPosition{};
    StateID bestState = BlockIds::AIR;
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const glm::ivec3 blockPosition(x, y, z);
                const StateID stateId = worldView.getBlockState(x, y, z);
                if (!BlockCollision::intersects(stateId, blockPosition, queryMin, queryMax)) {
                    continue;
                }

                const glm::vec3 centerDelta = position - (glm::vec3(blockPosition) + glm::vec3(0.5f));
                const float distanceSq = glm::dot(centerDelta, centerDelta);
                if (!found || distanceSq < bestDistanceSq) {
                    found = true;
                    bestDistanceSq = distanceSq;
                    bestPosition = blockPosition;
                    bestState = stateId;
                }
            }
        }
    }

    if (found) {
        outBlockPosition = bestPosition;
        outState = bestState;
    }
    return found;
}

StateID withPowerProperty(const StateID stateId, const uint8_t power) {
    if (power > 15) {
        throw std::runtime_error("Target block power exceeds 15");
    }
    const uint16_t value = BlockStateRegistry::getPropertyValueIndex(PropIndices::POWER, std::to_string(power));
    if (value == BlockStateRegistry::INVALID_INDEX) {
        throw std::runtime_error("Target block requires registered power values 0 through 15");
    }
    const StateID updatedState = BlockStateRegistry::withProperty(stateId, PropIndices::POWER, value);
    if (BlockStateRegistry::getPropertyIndex(updatedState, PropIndices::POWER) != value) {
        throw std::runtime_error("Target block power state transition failed");
    }
    return updatedState;
}

uint8_t targetPowerFromImpact(const glm::ivec3& blockPosition, const glm::vec3& impactPosition) {
    const glm::vec3 local = glm::clamp(impactPosition - glm::vec3(blockPosition), glm::vec3(0.0f), glm::vec3(1.0f));
    const float faceDistances[6] = {
        local.x,
        1.0f - local.x,
        local.y,
        1.0f - local.y,
        local.z,
        1.0f - local.z,
    };
    int nearestFace = 0;
    for (int i = 1; i < 6; ++i) {
        if (faceDistances[i] < faceDistances[nearestFace]) {
            nearestFace = i;
        }
    }

    float u = 0.0f;
    float v = 0.0f;
    switch (nearestFace) {
    case 0:
    case 1:
        u = local.y - 0.5f;
        v = local.z - 0.5f;
        break;
    case 2:
    case 3:
        u = local.x - 0.5f;
        v = local.z - 0.5f;
        break;
    case 4:
    case 5:
        u = local.x - 0.5f;
        v = local.y - 0.5f;
        break;
    default:
        throw std::runtime_error("Target impact selected an invalid face");
    }

    constexpr float kMaxFaceDistance = 0.70710678118f;
    const float distance = std::sqrt(u * u + v * v);
    const float centered = 1.0f - std::min(distance / kMaxFaceDistance, 1.0f);
    return static_cast<uint8_t>(std::max(1, static_cast<int>(std::ceil(centered * 15.0f))));
}

void activateTargetBlock(World& world,
                         const uint64_t tickIndex,
                         const glm::ivec3& blockPosition,
                         const glm::vec3& impactPosition) {
    const StateID currentState = world.getBlockState(blockPosition.x, blockPosition.y, blockPosition.z);
    if (BlockStateRegistry::getBlockId(currentState) != BlockIds::TARGET) {
        return;
    }

    const uint8_t power = targetPowerFromImpact(blockPosition, impactPosition);
    const StateID updatedState = withPowerProperty(currentState, power);
    world.setBlockState(blockPosition.x, blockPosition.y, blockPosition.z, updatedState);
    const uint64_t activationRedstoneTick = std::max(world.lastProcessedRedstoneTick(), tickIndex / 2u);
    world.redstoneScheduledUpdateQueue().reschedule(
        activationRedstoneTick + kTargetPulseTicks,
        blockPosition,
        RedstoneScheduledAction::ReleaseTargetPulse);
}

void emitProjectileImpactParticles(GameplayRegistry& registry,
                                   const glm::vec3& position,
                                   const BlockID blockType,
                                   const int particleCount) {
    if (blockType == 0) {
        return;
    }
    ensureParticleEventBus(registry).push(makeImpactParticleEvent(position, blockType, particleCount));
}

void emitProjectileSound(GameplayRegistry& registry,
                         const std::string& soundId,
                         const glm::vec3& position) {
    if (soundId.empty()) {
        return;
    }
    ensureAudioEventBus(registry).push({soundId, position, true, 1.0f});
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
                                  const int particleCount,
                                  std::vector<entt::entity>& destroyList) {
    if (reg.all_of<EntityNetIdComponent>(projectile)) {
        reg.emplace_or_replace<EntityImpactComponent>(projectile, position, particleBlock, particleCount);
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
                                  const SystemContext& ctx,
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
        if (!simulation::isEntityTicking(ctx, mob)) {
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
        emitProjectileSound(registry, projectileDefinition.throwSoundId, spawnPosition);
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
                                  SpinVisualComponent,
                                  BoundsComponent>();
    for (const entt::entity projectile : projectileView) {
        if (!simulation::isEntityTicking(ctx, projectile)) {
            continue;
        }

        auto& projectileData = projectileView.get<ProjectileComponent>(projectile);
        auto& transform = projectileView.get<TransformComponent>(projectile);
        auto& velocity = projectileView.get<VelocityComponent>(projectile);
        auto& lifetime = projectileView.get<LifetimeComponent>(projectile);
        auto& spin = projectileView.get<SpinVisualComponent>(projectile);
        const auto& bounds = projectileView.get<BoundsComponent>(projectile);

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

            StateID hitState = BlockIds::AIR;
            glm::ivec3 hitBlockPosition{};
            if (isCollisionBlockAt(worldView, transform.position, bounds.halfExtents, hitState, hitBlockPosition)) {
                if (ctx.services.world) {
                    activateTargetBlock(*ctx.services.world, ctx.tickIndex, hitBlockPosition, transform.position);
                }
                emitProjectileImpactParticles(registry,
                                              transform.position,
                                              hitState,
                                              projectileData.entityImpactParticleCount);
                emitProjectileSound(registry, projectileData.impactSoundId, transform.position);
                queueProjectileImpactDespawn(reg,
                                             projectile,
                                             transform.position,
                                             hitState,
                                             projectileData.entityImpactParticleCount,
                                             destroyList);
                destroyed = true;
                break;
            }

            const entt::entity target = findProjectileTarget(reg, ctx, projectile, projectileData, transform.position);
            if (target != entt::null) {
                ensureDamageEventBus(registry).push({target, projectileData.owner, projectileData.damage});
                const BlockID impactBlock = projectileData.entityImpactParticleBlock != 0
                    ? projectileData.entityImpactParticleBlock
                    : defaultProjectileEntityImpactParticleBlock();
                emitProjectileImpactParticles(registry,
                                              transform.position,
                                              impactBlock,
                                              projectileData.entityImpactParticleCount);
                emitProjectileSound(registry, projectileData.impactSoundId, transform.position);
                queueProjectileImpactDespawn(reg,
                                             projectile,
                                             transform.position,
                                             impactBlock,
                                             projectileData.entityImpactParticleCount,
                                             destroyList);
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
