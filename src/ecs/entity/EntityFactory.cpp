#include "EntityFactory.h"

#include "../../Diagnostics.h"
#include "EntityDefinitionRegistry.h"
#include "EntityModelFactory.h"
#include "../components/Components.h"
#include "../components/NetworkComponents.h"
#include "../util/DropRuntimeState.h"
#include "../util/ProjectileDefinitions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace ecs {
namespace {

[[noreturn]] void failEntityFactory(const std::string& message) {
    std::cerr << message << '\n';
    std::abort();
}

template <typename Component, typename... Args>
void ensureComponent(entt::registry& registry, const entt::entity entity, Args&&... args) {
    if (!registry.all_of<Component>(entity)) {
        registry.emplace<Component>(entity, std::forward<Args>(args)...);
    }
}

template <typename Component, typename... Args>
Component& ensureComponentRef(entt::registry& registry, const entt::entity entity, Args&&... args) {
    ensureComponent<Component>(registry, entity, std::forward<Args>(args)...);
    return registry.get<Component>(entity);
}

void applyMobDefinition(GameplayRegistry& registry, const entt::entity entity, const MobEntityDefinition& definition) {
    entt::registry& reg = registry.registry();
    if (entity == entt::null || !reg.valid(entity)) {
        return;
    }

    if (auto* transform = reg.try_get<TransformComponent>(entity)) {
        transform->eyeHeight = definition.eyeHeight;
    }

    auto* visual = reg.try_get<MobVisualComponent>(entity);
    if (visual == nullptr) {
        visual = &reg.emplace<MobVisualComponent>(entity);
    }
    visual->model = definition.model;
    visual->textureKey = definition.textureKey;
    visual->skinLayout = definition.skinLayout;
    visual->scale = definition.visualScale;

    if (auto* health = reg.try_get<HealthComponent>(entity)) {
        health->current = definition.health;
        health->max = definition.maxHealth;
    }

    if (auto* ai = reg.try_get<MobAIComponent>(entity)) {
        ai->wanderInterval = definition.ai.wanderInterval;
        ai->wanderSpeed = definition.ai.wanderSpeed;
        ai->pursueSpeed = definition.ai.pursueSpeed;
        ai->acquisitionRange = definition.ai.acquisitionRange;
        ai->loseTargetRange = definition.ai.loseTargetRange;
        ai->attackRange = definition.ai.attackRange;
        ai->attackCooldownSeconds = definition.ai.attackCooldownSeconds;
        ai->attackDamage = definition.ai.attackDamage;
        ai->targetsPlayers = definition.ai.targetsPlayers;
        ai->retaliates = definition.ai.retaliates;
        ai->lineOfSightMemorySeconds = definition.ai.lineOfSightMemorySeconds;
        ai->hearingRange = definition.ai.hearingRange;
        ai->stuckJumpThresholdSeconds = definition.ai.stuckJumpThresholdSeconds;
        ai->jumpCooldownSeconds = definition.ai.jumpCooldownSeconds;
        ai->avoidanceSeconds = definition.ai.avoidanceSeconds;
        ai->avoidanceStrength = definition.ai.avoidanceStrength;
    }

    if (auto* physicsBody = reg.try_get<PhysicsBodyComponent>(entity)) {
        physicsBody->body.halfExtents = definition.physics.halfExtents;
        physicsBody->body.colliderOffset = definition.physics.colliderOffset;
        physicsBody->body.eyeOffsetY = definition.physics.eyeOffsetY;
    }

    if (definition.drops.empty()) {
        if (reg.all_of<DropTableComponent>(entity)) {
            reg.remove<DropTableComponent>(entity);
        }
    } else {
        const MobDropDefinition& firstDrop = definition.drops.front();
        auto* dropTable = reg.try_get<DropTableComponent>(entity);
        if (dropTable == nullptr) {
            dropTable =
                &reg.emplace<DropTableComponent>(entity, firstDrop.itemId, firstDrop.minCount, firstDrop.maxCount);
        } else {
            dropTable->itemId = firstDrop.itemId;
            dropTable->minCount = firstDrop.minCount;
            dropTable->maxCount = firstDrop.maxCount;
        }

        dropTable->entries.clear();
        dropTable->entries.reserve(definition.drops.size());
        for (const MobDropDefinition& drop : definition.drops) {
            dropTable->entries.push_back(DropTableEntry{drop.itemId, drop.minCount, drop.maxCount});
        }
    }

    auto* hurt = reg.try_get<HurtEffectComponent>(entity);
    if (hurt == nullptr) {
        hurt = &reg.emplace<HurtEffectComponent>(entity);
    }
    if (definition.hurtEffect.enabled) {
        hurt->soundId = definition.hurtEffect.soundId;
        hurt->soundVolume = definition.hurtEffect.volume;
        hurt->flashDurationSeconds = definition.hurtEffect.flashDurationSeconds;
    } else {
        hurt->soundId.clear();
        hurt->soundVolume = 1.0f;
    }

    if (definition.deathEffect.enabled) {
        reg.emplace_or_replace<DeathEffectComponent>(entity, definition.deathEffect.particleBlock,
                                                     definition.deathEffect.particleCount,
                                                     definition.deathEffect.soundId, definition.deathEffect.volume);
    } else if (reg.all_of<DeathEffectComponent>(entity)) {
        reg.remove<DeathEffectComponent>(entity);
    }
}

} // namespace

entt::entity EntityFactory::createServerPlayerProxy(GameplayRegistry& registry, const glm::vec3& position,
                                                    const glm::vec3& velocity) {
    entt::registry& reg = registry.registry();
    const entt::entity entity = reg.create();
    ensureServerPlayerProxy(registry, entity, position, velocity);
    return entity;
}

void EntityFactory::ensureServerPlayerProxy(GameplayRegistry& registry, const entt::entity entity,
                                            const glm::vec3& position, const glm::vec3& velocity) {
    entt::registry& reg = registry.registry();
    if (entity == entt::null || !reg.valid(entity)) {
        return;
    }

    ensureComponent<LocalPlayerTag>(reg, entity);

    auto& transform = ensureComponentRef<TransformComponent>(reg, entity, position, 1.62f);
    transform.position = position;
    transform.eyeHeight = 1.62f;

    auto& velocityComponent = ensureComponentRef<VelocityComponent>(reg, entity);
    velocityComponent.velocity = velocity;

    auto& physicsBody = ensureComponentRef<PhysicsBodyComponent>(reg, entity);
    physicsBody.body.position = position;
    physicsBody.body.velocity = velocity;
    physicsBody.body.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
    physicsBody.body.colliderOffset = glm::vec3(0.0f, 0.9f, 0.0f);
    physicsBody.body.eyeOffsetY = 1.62f;

    ensureComponent<CameraStateComponent>(reg, entity);
    ensureComponent<BlockActionIntentComponent>(reg, entity);
    ensureComponent<BlockTargetComponent>(reg, entity);
    ensureComponent<BlockInteractionRuntimeComponent>(reg, entity);
    ensureComponent<MeleeAttackComponent>(reg, entity);
    ensureComponent<ProjectileThrowerComponent>(reg, entity);
    ensureComponent<HealthComponent>(reg, entity);
    ensureComponent<PlayerModeComponent>(reg, entity);
    ensureComponent<HurtEffectComponent>(reg, entity);
    ensureComponent<InventoryComponent>(reg, entity);

    if (!reg.all_of<InventoryDataComponent>(entity)) {
        auto& inventoryData = reg.emplace<InventoryDataComponent>(entity);
        inventoryData.inventory.initializeDefaultLoadout();
    }
}

entt::entity EntityFactory::createMob(GameplayRegistry& registry, const std::string_view entityId,
                                      const glm::vec3& position) {
    std::string error;
    EntityDefinitionRegistry& definitions = EntityDefinitionRegistry::instance();
    if (!definitions.ensureLoaded(&error)) {
        MECRAFT_LOG_PRINTF("[EntityFactory] Failed to load entity definitions: %s\n", error.c_str());
        MECRAFT_LOG_FLUSH(stdout);
        return entt::null;
    }

    const MobEntityDefinition* definition = definitions.findMob(entityId);
    if (definition == nullptr) {
        MECRAFT_LOG_PRINTF("[EntityFactory] Unknown mob definition: %.*s\n", static_cast<int>(entityId.size()),
                           entityId.data());
        MECRAFT_LOG_FLUSH(stdout);
        return entt::null;
    }

    entt::entity entity = EntityModelFactory::createMob(registry, *definition, position);
    if (entity == entt::null) {
        return entt::null;
    }

    entt::registry& reg = registry.registry();
    if (auto* type = reg.try_get<EntityTypeComponent>(entity)) {
        type->entityId = definition->id.full();
    } else {
        reg.emplace<EntityTypeComponent>(entity, definition->id.full());
    }
    applyMobDefinition(registry, entity, *definition);
    return entity;
}

entt::entity EntityFactory::createZombie(GameplayRegistry& registry, const glm::vec3& position) {
    return createMob(registry, "minecraft:zombie", position);
}

entt::entity EntityFactory::createZombie(entt::registry& registry, const glm::vec3& position) {
    const entt::entity zombie = registry.create();
    registry.emplace<MobTag>(zombie);
    registry.emplace<MobVisualComponent>(zombie);
    registry.emplace<TransformComponent>(zombie, position, 1.62f);
    registry.emplace<MobAIComponent>(zombie);
    registry.emplace<MoveIntentComponent>(zombie);
    registry.emplace<HealthComponent>(zombie, 20, 20);
    registry.emplace<HurtEffectComponent>(zombie);
    registry.emplace<EntityTypeComponent>(zombie, "minecraft:zombie");
    registry.emplace<NetworkSyncTag>(zombie);
    return zombie;
}

entt::entity EntityFactory::createItemDrop(GameplayRegistry& registry, const ItemDropSpawnParams& params) {
    if (params.itemId == 0 || params.stackCount == 0) {
        return entt::null;
    }

    entt::registry& reg = registry.registry();
    auto& state = ensureDropRuntimeState(registry);

    const std::size_t dropId = params.dropId != 0 ? params.dropId : state.nextId++;
    if (params.dropId != 0 && state.nextId <= params.dropId) {
        state.nextId = params.dropId + 1;
    }

    const entt::entity drop = reg.create();
    reg.emplace<DropItemTag>(drop);
    reg.emplace<DropEntityIdComponent>(drop, dropId);
    reg.emplace<TransformComponent>(drop, params.position, 0.0f);
    reg.emplace<VelocityComponent>(drop, params.velocity);
    reg.emplace<ItemComponent>(drop, params.itemId, params.stackCount);
    reg.emplace<BoundsComponent>(drop, params.halfExtents);
    reg.emplace<LifetimeComponent>(drop, params.ageSeconds, params.lifeTimeSeconds);
    reg.emplace<SpinVisualComponent>(drop, params.yawRadians, params.spinSpeedRadians);
    reg.emplace<GroundedStateComponent>(drop, GroundedStateComponent{params.grounded});
    reg.emplace<NetworkSyncTag>(drop);
    return drop;
}

entt::entity EntityFactory::createFallingBlock(GameplayRegistry& registry, const FallingBlockSpawnParams& params) {
    if (params.blockId == 0) {
        return entt::null;
    }

    entt::registry& reg = registry.registry();
    auto& state = ensureDropRuntimeState(registry);
    const std::size_t dropId = state.nextId++;

    const entt::entity entity = reg.create();
    // Render position starts at the center of the source cell (full-block cube).
    const glm::vec3 renderPos = glm::vec3(params.gridPosition) + glm::vec3(0.5f);

    reg.emplace<FallingBlockTag>(entity);
    reg.emplace<FallingBlockComponent>(entity, params.blockId, params.gridPosition, params.gridPosition, 0.0f);
    reg.emplace<TransformComponent>(entity, renderPos, 0.0f);
    reg.emplace<BoundsComponent>(entity, glm::vec3(0.5f));
    reg.emplace<GroundedStateComponent>(entity, GroundedStateComponent{false});
    reg.emplace<DropEntityIdComponent>(entity, dropId);
    reg.emplace<NetworkSyncTag>(entity);
    return entity;
}

entt::entity EntityFactory::createMovingBlock(GameplayRegistry& registry, const MovingBlockSpawnParams& params) {
    if (params.stateId == NULL_BLOCK_STATE) {
        return entt::null;
    }
    if (params.durationSeconds <= 0.0f) {
        failEntityFactory("Moving block duration must be positive");
    }
    const glm::ivec3 expectedDirection = params.targetPosition - params.sourcePosition;
    if (expectedDirection != params.direction) {
        failEntityFactory("Moving block direction must match source and target positions");
    }
    if (std::abs(params.direction.x) + std::abs(params.direction.y) + std::abs(params.direction.z) != 1) {
        failEntityFactory("Moving block direction must be a single grid step");
    }

    entt::registry& reg = registry.registry();
    auto& state = ensureDropRuntimeState(registry);
    const std::size_t dropId = state.nextId++;

    const entt::entity entity = reg.create();
    const glm::vec3 renderPos = glm::vec3(params.sourcePosition) + glm::vec3(0.5f);

    reg.emplace<MovingBlockTag>(entity);
    reg.emplace<MovingBlockComponent>(entity, params.stateId, params.sourcePosition, params.targetPosition,
                                      params.direction, 0.0f, params.durationSeconds, params.placeAtTarget);
    reg.emplace<TransformComponent>(entity, renderPos, 0.0f);
    reg.emplace<BoundsComponent>(entity, glm::vec3(0.5f));
    reg.emplace<DropEntityIdComponent>(entity, dropId);
    return entity;
}

entt::entity EntityFactory::createAppleProjectile(GameplayRegistry& registry, const entt::entity owner,
                                                  const glm::vec3& position, const glm::vec3& velocity) {
    return createProjectile(registry, owner, position, velocity, makeAppleProjectileDefinition());
}

entt::entity EntityFactory::createProjectile(GameplayRegistry& registry, const entt::entity owner,
                                             const glm::vec3& position, const glm::vec3& velocity,
                                             const ProjectileDefinition& definition) {
    entt::registry& reg = registry.registry();
    const entt::entity projectile = reg.create();

    reg.emplace<ProjectileTag>(projectile);
    reg.emplace<ProjectileComponent>(projectile, owner, definition.damage, definition.hitRadius, definition.gravity,
                                     definition.entityImpactParticleBlock, definition.entityImpactParticleCount,
                                     definition.impactSoundId);
    reg.emplace<TransformComponent>(projectile, position, 0.0f);
    reg.emplace<VelocityComponent>(projectile, velocity);
    reg.emplace<ItemComponent>(projectile, definition.itemId, 1u);
    reg.emplace<BoundsComponent>(projectile, glm::vec3(definition.boundsHalfExtent));
    reg.emplace<LifetimeComponent>(projectile, 0.0f, definition.lifetimeSeconds);
    reg.emplace<SpinVisualComponent>(projectile, 0.0f, definition.spinSpeedRadians);
    reg.emplace<GroundedStateComponent>(projectile);
    reg.emplace<NetworkSyncTag>(projectile);

    auto& state = ensureDropRuntimeState(registry);
    reg.emplace<DropEntityIdComponent>(projectile, state.nextId++);

    return projectile;
}

} // namespace ecs
