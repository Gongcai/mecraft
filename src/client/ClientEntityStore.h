#ifndef MECRAFT_CLIENT_ENTITY_STORE_H
#define MECRAFT_CLIENT_ENTITY_STORE_H

#include "../net/Protocol.h"
#include "../ecs/components/NetworkComponents.h"
#include <unordered_map>
#include <entt/entt.hpp>

class ResourceMgr;

namespace client {

/// Manages remote entities on the client side.
/// Maps EntityNetId to local entt::entity handles.
/// Creates visual-only entities from server spawn messages,
/// updates their positions from snapshots, and destroys them on despawn.
class ClientEntityStore {
public:
    ClientEntityStore();
    ~ClientEntityStore();

    /// Initialize with the ECS registry and resource manager.
    void init(entt::registry& registry, ResourceMgr* resourceMgr);

    /// Handle an EntitySpawn message from the server.
    void handleSpawn(const net::EntitySpawnMessage& msg);

    /// Handle an EntityDespawn message from the server.
    void handleDespawn(const net::EntityDespawnMessage& msg);

    /// Handle an EntitySnapshot message from the server (batch position update).
    void handleSnapshot(const net::EntitySnapshotMessage& msg);

    /// Get the number of tracked remote entities.
    [[nodiscard]] size_t remoteEntityCount() const { return m_netIdToEntity.size(); }

    /// Check if a netId is already tracked.
    [[nodiscard]] bool hasEntity(net::EntityNetId netId) const;

private:
    void createDropEntity(const net::EntitySpawnMessage& msg);
    void createPlayerEntity(const net::EntitySpawnMessage& msg);
    void createMobEntity(const net::EntitySpawnMessage& msg);

    entt::registry* m_registry = nullptr;
    ResourceMgr* m_resourceMgr = nullptr;

    /// Mapping from network ID to local entity handle.
    std::unordered_map<net::EntityNetId, entt::entity> m_netIdToEntity;
};

} // namespace client

#endif // MECRAFT_CLIENT_ENTITY_STORE_H
