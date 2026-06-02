#include "ClientEntityStore.h"
#include "../ecs/components/Components.h"
#include "../item/Item.h"
#include "../resource/ResourceMgr.h"

namespace client {

ClientEntityStore::ClientEntityStore() = default;
ClientEntityStore::~ClientEntityStore() = default;

void ClientEntityStore::init(entt::registry& registry, ResourceMgr* resourceMgr) {
    m_registry = &registry;
    m_resourceMgr = resourceMgr;
}

void ClientEntityStore::handleSpawn(const net::EntitySpawnMessage& msg) {
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
    }
}

bool ClientEntityStore::hasEntity(net::EntityNetId netId) const {
    return m_netIdToEntity.count(netId) > 0;
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
    auto entity = m_registry->create();

    // Minimal components for a remote player (visual-only, no physics/AI)
    m_registry->emplace<ecs::TransformComponent>(entity, msg.position, 1.62f);
    m_registry->emplace<ecs::VelocityComponent>(entity, msg.velocity);
    m_registry->emplace<ecs::NetworkSyncTag>(entity);
    m_registry->emplace<ecs::EntityNetIdComponent>(entity, msg.netId);

    // TODO: Create Steve model hierarchy when GameplayRegistry is available
    // For now, remote players are represented as position-only entities.

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
