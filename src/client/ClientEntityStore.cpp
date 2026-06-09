#include "ClientEntityStore.h"
#include "../ecs/components/Components.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/entity/SteveModelFactory.h"
#include "../ecs/entity/MobModelFactory.h"
#include "../ecs/entity/EntityDefinitionRegistry.h"
#include "../ecs/util/AudioEventBuffer.h"
#include "../ecs/util/ParticleEventBuffer.h"
#include "../ecs/util/ProjectileDefinitions.h"
#include "../item/Item.h"
#include "../resource/ResourceMgr.h"
#include <algorithm>
#include <string>

namespace client {
namespace {

void collectEntityTree(entt::registry& registry, const entt::entity entity, std::vector<entt::entity>& out) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }
    out.push_back(entity);
    if (const auto* children = registry.try_get<ecs::ChildrenComponent>(entity)) {
        for (const entt::entity child : children->children) {
            collectEntityTree(registry, child, out);
        }
    }
}

void queueAudioEvent(ecs::GameplayRegistry* gameplayRegistry,
                     const std::string& soundId,
                     const glm::vec3& position,
                     const float volume = 1.0f) {
    if (gameplayRegistry == nullptr || soundId.empty()) {
        return;
    }
    ecs::ensureAudioEventBus(*gameplayRegistry).push({soundId, position, true, volume});
}

void applyMobHurtEffect(entt::registry& registry,
                        const entt::entity entity,
                        const ecs::MobEntityDefinition* definition) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }

    auto* hurt = registry.try_get<ecs::HurtEffectComponent>(entity);
    if (hurt == nullptr) {
        hurt = &registry.emplace<ecs::HurtEffectComponent>(entity);
    }
    if (definition != nullptr && definition->hurtEffect.enabled) {
        hurt->soundId = definition->hurtEffect.soundId;
        hurt->soundVolume = definition->hurtEffect.volume;
        hurt->flashDurationSeconds = definition->hurtEffect.flashDurationSeconds;
    } else {
        hurt->soundId.clear();
        hurt->soundVolume = 1.0f;
    }
}

void applyMobDeathEffect(entt::registry& registry,
                         const entt::entity entity,
                         const ecs::MobEntityDefinition* definition) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }

    if (definition != nullptr && definition->deathEffect.enabled) {
        registry.emplace_or_replace<ecs::DeathEffectComponent>(
            entity,
            definition->deathEffect.particleBlock,
            definition->deathEffect.particleCount,
            definition->deathEffect.soundId,
            definition->deathEffect.volume);
    } else if (registry.all_of<ecs::DeathEffectComponent>(entity)) {
        registry.remove<ecs::DeathEffectComponent>(entity);
    }
}

struct ImpactAudio {
    std::string soundId;
    float volume = 1.0f;
};

ImpactAudio impactAudioForEntity(entt::registry& registry, const entt::entity entity) {
    if (entity == entt::null || !registry.valid(entity)) {
        return {};
    }

    if (const auto* projectile = registry.try_get<ecs::ProjectileComponent>(entity)) {
        return {projectile->impactSoundId, 1.0f};
    }

    if (const auto* deathEffect = registry.try_get<ecs::DeathEffectComponent>(entity)) {
        return {deathEffect->soundId, deathEffect->volume};
    }

    return {};
}

void setSyncedHealth(entt::registry& registry,
                     const entt::entity entity,
                     const uint16_t current,
                     const uint16_t max) {
    if (max == 0 || entity == entt::null || !registry.valid(entity)) {
        return;
    }

    if (auto* health = registry.try_get<ecs::HealthComponent>(entity)) {
        health->current = static_cast<int>(current);
        health->max = static_cast<int>(max);
    } else {
        registry.emplace<ecs::HealthComponent>(entity, static_cast<int>(current), static_cast<int>(max));
    }
}

void triggerReplicaHurt(ecs::GameplayRegistry* gameplayRegistry,
                        entt::registry& registry,
                        const entt::entity entity) {
    if (entity == entt::null || !registry.valid(entity)) {
        return;
    }

    auto* hurt = registry.try_get<ecs::HurtEffectComponent>(entity);
    if (hurt == nullptr) {
        hurt = &registry.emplace<ecs::HurtEffectComponent>(entity);
    }
    hurt->triggerClassicHurt();

    if (gameplayRegistry != nullptr && !hurt->soundId.empty()) {
        glm::vec3 position{0.0f};
        if (const auto* transform = registry.try_get<ecs::TransformComponent>(entity)) {
            position = transform->position;
        }
        queueAudioEvent(gameplayRegistry, hurt->soundId, position, hurt->soundVolume);
    }
}

} // namespace

ClientEntityStore::ClientEntityStore() = default;
ClientEntityStore::~ClientEntityStore() = default;

void ClientEntityStore::init(entt::registry& registry, ResourceMgr* resourceMgr) {
    m_registry = &registry;
    m_gameplayRegistry = nullptr;
    m_resourceMgr = resourceMgr;
    flushPendingMessages();
}

void ClientEntityStore::init(ecs::GameplayRegistry& registry, ResourceMgr* resourceMgr) {
    m_registry = &registry.registry();
    m_gameplayRegistry = &registry;
    m_resourceMgr = resourceMgr;
    flushPendingMessages();
}

void ClientEntityStore::handleSpawn(const net::EntitySpawnMessage& msg) {
    if (!m_registry) {
        const auto alreadyPending = std::any_of(m_pendingSpawns.begin(),
                                                m_pendingSpawns.end(),
                                                [&msg](const net::EntitySpawnMessage& pending) {
                                                    return pending.netId == msg.netId;
                                                });
        if (!alreadyPending) {
            m_pendingSpawns.push_back(msg);
        }
        return;
    }

    if (!m_registry || hasEntity(msg.netId)) {
        return;  // Already tracked or not initialized
    }

    if (const entt::entity existing = findExistingEntity(msg.netId); existing != entt::null) {
        m_netIdToEntity[msg.netId] = existing;
        return;
    }

    switch (msg.kind) {
    case net::EntityKind::Drop:
        createDropEntity(msg);
        break;
    case net::EntityKind::Projectile:
        createProjectileEntity(msg);
        break;
    case net::EntityKind::Player:
        createPlayerEntity(msg);
        break;
    case net::EntityKind::Mob:
        createMobEntity(msg);
        break;
    }
}

void ClientEntityStore::handleDespawn(const net::EntityDespawnMessage& msg) {
    if (!m_registry) {
        m_pendingSpawns.erase(std::remove_if(m_pendingSpawns.begin(),
                                             m_pendingSpawns.end(),
                                             [&msg](const net::EntitySpawnMessage& pending) {
                                                 return pending.netId == msg.netId;
                                             }),
                              m_pendingSpawns.end());
        m_pendingDespawns.push_back(msg);
        return;
    }

    auto it = m_netIdToEntity.find(msg.netId);
    if (it == m_netIdToEntity.end()) {
        m_explicitImpactNetIds.erase(msg.netId);
        return;
    }

    if (m_registry->valid(it->second)) {
        const bool explicitImpactReceived = m_explicitImpactNetIds.erase(msg.netId) > 0;
        if (!explicitImpactReceived &&
            m_gameplayRegistry &&
            m_registry->all_of<ecs::ProjectileTag, ecs::TransformComponent>(it->second)) {
            const auto& transform = m_registry->get<ecs::TransformComponent>(it->second);
            BlockID particleBlock = ecs::defaultProjectileEntityImpactParticleBlock();
            int particleCount = 14;
            std::string impactSoundId;
            if (const auto* projectile = m_registry->try_get<ecs::ProjectileComponent>(it->second)) {
                if (projectile->entityImpactParticleBlock != 0) {
                    particleBlock = projectile->entityImpactParticleBlock;
                }
                particleCount = projectile->entityImpactParticleCount;
                impactSoundId = projectile->impactSoundId;
            }
            ecs::ensureParticleEventBus(*m_gameplayRegistry)
                .push(ecs::makeImpactParticleEvent(transform.position, particleBlock, particleCount));
            queueAudioEvent(m_gameplayRegistry, impactSoundId, transform.position);
        }

        std::vector<entt::entity> toDestroy;
        collectEntityTree(*m_registry, it->second, toDestroy);
        for (const entt::entity entity : toDestroy) {
            if (m_registry->valid(entity)) {
                m_registry->destroy(entity);
            }
        }
    }

    m_netIdToEntity.erase(it);
}

void ClientEntityStore::handleImpact(const net::EntityImpactMessage& msg) {
    if (!m_registry) {
        m_pendingImpacts.push_back(msg);
        return;
    }

    m_explicitImpactNetIds.insert(msg.netId);
    if (!m_gameplayRegistry) {
        return;
    }

    if (msg.particleBlockId != 0) {
        ecs::ensureParticleEventBus(*m_gameplayRegistry)
            .push(ecs::makeImpactParticleEvent(msg.position,
                                               static_cast<BlockID>(msg.particleBlockId),
                                               msg.particleCount));
    }

    auto it = m_netIdToEntity.find(msg.netId);
    if (it != m_netIdToEntity.end() && m_registry->valid(it->second)) {
        const ImpactAudio audio = impactAudioForEntity(*m_registry, it->second);
        queueAudioEvent(m_gameplayRegistry, audio.soundId, msg.position, audio.volume);
    }
}

void ClientEntityStore::handleSnapshot(const net::EntitySnapshotMessage& msg) {
    if (!m_registry) {
        m_pendingSnapshots.clear();
        m_pendingSnapshots.push_back(msg);
        return;
    }

    for (const auto& item : msg.entities) {
        auto it = m_netIdToEntity.find(item.netId);
        if (it == m_netIdToEntity.end()) {
            continue;  // Unknown entity, skip
        }

        if (!m_registry->valid(it->second)) {
            m_netIdToEntity.erase(it);
            continue;
        }

        // Update transform
        auto* transform = m_registry->try_get<ecs::TransformComponent>(it->second);
        if (transform) {
            transform->position = item.position;
        }

        // Update velocity if present
        auto* velocity = m_registry->try_get<ecs::VelocityComponent>(it->second);
        if (velocity) {
            velocity->velocity = item.velocity;
        }

        // Update spin yaw for drops
        auto* spin = m_registry->try_get<ecs::SpinVisualComponent>(it->second);
        if (spin) {
            spin->yawRadians = item.yaw;
        }

        auto* camera = m_registry->try_get<ecs::CameraStateComponent>(it->second);
        if (camera) {
            camera->yaw = item.yaw;
            camera->pitch = item.pitch;
        }

        auto* mobAI = m_registry->try_get<ecs::MobAIComponent>(it->second);
        if (mobAI) {
            mobAI->yaw = item.yaw;
        }

        if (item.maxHealth > 0) {
            const auto* previousHealth = m_registry->try_get<ecs::HealthComponent>(it->second);
            const bool healthDropped = previousHealth != nullptr &&
                                       static_cast<int>(item.health) < previousHealth->current;
            setSyncedHealth(*m_registry, it->second, item.health, item.maxHealth);
            if (item.hurt || healthDropped) {
                triggerReplicaHurt(m_gameplayRegistry, *m_registry, it->second);
            }
        } else if (item.hurt) {
            triggerReplicaHurt(m_gameplayRegistry, *m_registry, it->second);
        }
    }
}

bool ClientEntityStore::hasEntity(net::EntityNetId netId) const {
    return m_netIdToEntity.count(netId) > 0;
}

entt::entity ClientEntityStore::findExistingEntity(const net::EntityNetId netId) const {
    if (!m_registry) {
        return entt::null;
    }

    auto view = m_registry->view<ecs::EntityNetIdComponent>();
    for (const entt::entity entity : view) {
        if (view.get<ecs::EntityNetIdComponent>(entity).netId == netId) {
            return entity;
        }
    }
    return entt::null;
}

void ClientEntityStore::flushPendingMessages() {
    if (!m_registry) {
        return;
    }

    auto pendingSpawns = std::move(m_pendingSpawns);
    auto pendingSnapshots = std::move(m_pendingSnapshots);
    auto pendingImpacts = std::move(m_pendingImpacts);
    auto pendingDespawns = std::move(m_pendingDespawns);
    m_pendingSpawns.clear();
    m_pendingSnapshots.clear();
    m_pendingImpacts.clear();
    m_pendingDespawns.clear();

    for (const auto& msg : pendingSpawns) {
        handleSpawn(msg);
    }
    for (const auto& msg : pendingSnapshots) {
        handleSnapshot(msg);
    }
    for (const auto& msg : pendingImpacts) {
        handleImpact(msg);
    }
    for (const auto& msg : pendingDespawns) {
        handleDespawn(msg);
    }
}

void ClientEntityStore::createDropEntity(const net::EntitySpawnMessage& msg) {
    auto entity = m_registry->create();

    // Minimal components for rendering a drop
    m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 0.25f);
    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::ItemComponent>(entity,
        static_cast<ItemID>(msg.itemId), msg.stackCount);
    m_registry->emplace<ecs::SpinVisualComponent>(entity, 0.0f, 3.0f);
    m_registry->emplace<ecs::DropItemTag>(entity);
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    m_netIdToEntity[msg.netId] = entity;
}

void ClientEntityStore::createProjectileEntity(const net::EntitySpawnMessage& msg) {
    auto entity = m_registry->create();
    const uint32_t stackCount = std::max<uint32_t>(1u, msg.stackCount);
    const auto definition = ecs::projectileDefinitionForItemOrDefault(static_cast<ItemID>(msg.itemId));

    m_registry->emplace<ecs::ProjectileTag>(entity);
    m_registry->emplace<ecs::ProjectileComponent>(entity,
                                                  entt::null,
                                                  definition.damage,
                                                  definition.hitRadius,
                                                  definition.gravity,
                                                  definition.entityImpactParticleBlock,
                                                  definition.entityImpactParticleCount,
                                                  definition.impactSoundId);
    m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 0.0f);
    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::ItemComponent>(entity,
        static_cast<ItemID>(msg.itemId), stackCount);
    m_registry->emplace<ecs::SpinVisualComponent>(entity, msg.yaw, definition.spinSpeedRadians);
    m_registry->emplace<ecs::BoundsComponent>(entity, glm::vec3(definition.boundsHalfExtent));
    m_registry->emplace<ecs::LifetimeComponent>(entity, 0.0f, definition.lifetimeSeconds);
    m_registry->emplace<ecs::GroundedStateComponent>(entity);
    m_registry->emplace<ecs::DropEntityIdComponent>(entity, static_cast<std::size_t>(msg.netId));
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    m_netIdToEntity[msg.netId] = entity;
    queueAudioEvent(m_gameplayRegistry, definition.throwSoundId, msg.position);
}

void ClientEntityStore::createPlayerEntity(const net::EntitySpawnMessage& msg) {
    entt::entity entity = entt::null;
    if (m_gameplayRegistry) {
        entity = ecs::SteveModelFactory::createSteve(*m_gameplayRegistry, msg.position);
        m_registry->emplace<ecs::SkinTypeComponent>(entity, ecs::SkinTypeComponent::Type::Player);
        if (auto* camera = m_registry->try_get<ecs::CameraStateComponent>(entity)) {
            camera->yaw = msg.yaw;
            camera->pitch = msg.pitch;
        }
    } else {
        entity = m_registry->create();
        m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 1.62f);
    }

    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    m_netIdToEntity[msg.netId] = entity;
}

void ClientEntityStore::createMobEntity(const net::EntitySpawnMessage& msg) {
    entt::entity entity = entt::null;
    const std::string entityId = msg.entityId.empty() ? "minecraft:zombie" : msg.entityId;
    int initialHealth = 20;
    int initialMaxHealth = 20;
    const ecs::MobEntityDefinition* mobDefinition = nullptr;
    if (m_gameplayRegistry) {
        bool useZombieHumanoid = entityId == "minecraft:zombie";
        std::string error;
        ecs::EntityDefinitionRegistry& definitions = ecs::EntityDefinitionRegistry::instance();
        if (definitions.ensureLoaded(&error)) {
            if (const auto* definition = definitions.findMob(entityId)) {
                mobDefinition = definition;
                initialHealth = definition->health;
                initialMaxHealth = definition->maxHealth;
                useZombieHumanoid = definition->model == "zombie_humanoid";
            }
        }
        if (!useZombieHumanoid) {
            return;
        }
        entity = ecs::MobModelFactory::createZombieReplica(*m_gameplayRegistry, msg.position, msg.yaw);
    } else {
        entity = m_registry->create();
        m_registry->emplace<ecs::MobTag>(entity);
        m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 1.62f);
        m_registry->emplace<ecs::MobAIComponent>(entity);
        m_registry->get<ecs::MobAIComponent>(entity).yaw = msg.yaw;
    }

    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::HealthComponent>(entity, initialHealth, initialMaxHealth);
    m_registry->emplace<ecs::HurtEffectComponent>(entity);
    m_registry->emplace<ecs::EntityTypeComponent>(entity, entityId);
    applyMobHurtEffect(*m_registry, entity, mobDefinition);
    applyMobDeathEffect(*m_registry, entity, mobDefinition);
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    m_netIdToEntity[msg.netId] = entity;
}

} // namespace client
