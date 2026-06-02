#include "ClientEntityStore.h"
#include "../ecs/components/Components.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/entity/SteveModelFactory.h"
#include "../item/Item.h"
#include "../resource/ResourceMgr.h"
#include <algorithm>

namespace client {

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

    switch (msg.kind) {
    case net::EntityKind::Drop:
        createDropEntity(msg);
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
        return;
    }

    if (m_registry->valid(it->second)) {
        // Destroy the entity and its children (if any)
        auto* children = m_registry->try_get<ecs::ChildrenComponent>(it->second);
        if (children) {
            for (auto child : children->children) {
                if (m_registry->valid(child)) {
                    m_registry->destroy(child);
                }
            }
        }
        m_registry->destroy(it->second);
    }

    m_netIdToEntity.erase(it);
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
    }
}

bool ClientEntityStore::hasEntity(net::EntityNetId netId) const {
    return m_netIdToEntity.count(netId) > 0;
}

void ClientEntityStore::flushPendingMessages() {
    if (!m_registry) {
        return;
    }

    auto pendingSpawns = std::move(m_pendingSpawns);
    auto pendingSnapshots = std::move(m_pendingSnapshots);
    auto pendingDespawns = std::move(m_pendingDespawns);
    m_pendingSpawns.clear();
    m_pendingSnapshots.clear();
    m_pendingDespawns.clear();

    for (const auto& msg : pendingSpawns) {
        handleSpawn(msg);
    }
    for (const auto& msg : pendingSnapshots) {
        handleSnapshot(msg);
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
    auto entity = m_registry->create();

    // Minimal components for a remote mob (visual-only, no physics/AI)
    m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 1.62f);
    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    // TODO: Create mob model hierarchy when GameplayRegistry is available

    m_netIdToEntity[msg.netId] = entity;
}

} // namespace client
