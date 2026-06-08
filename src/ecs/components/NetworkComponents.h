#ifndef MECRAFT_NETWORK_COMPONENTS_H
#define MECRAFT_NETWORK_COMPONENTS_H

#include <cstdint>

namespace ecs {

/// Unique network identifier for entities that need synchronization.
/// Assigned by the server; zero means "not yet assigned".
using EntityNetId = uint32_t;

/// Entity kind discriminator for network spawn messages.
enum class EntityKind : uint8_t {
    Drop = 0,
    Player = 1,
    Mob = 2,
};

/// Component storing the network ID of a synced entity.
struct EntityNetIdComponent {
    EntityNetId netId = 0;
};

/// Tag component marking an entity for network synchronization.
/// The server checks for this tag to decide which entities to track and sync.
struct NetworkSyncTag {};

/// Tag for synced entities that should despawn after the server has sent final events.
struct PendingNetworkDespawnTag {};

} // namespace ecs

#endif // MECRAFT_NETWORK_COMPONENTS_H
