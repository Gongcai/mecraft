#ifndef MECRAFT_NETWORK_COMPONENTS_H
#define MECRAFT_NETWORK_COMPONENTS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

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

/// Stable gameplay definition id for synced/persistent entities.
struct EntityTypeComponent {
    std::string entityId;
};

/// Tag for synced entities that should despawn after the server has sent final events.
struct PendingNetworkDespawnTag {};

struct NetworkSnapshotSample {
    uint32_t serverTick = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// Client-side smoothing target for server-authoritative entity snapshots.
struct NetworkInterpolationComponent {
    static constexpr std::size_t SnapshotBufferSize = 8;

    glm::vec3 targetPosition{0.0f};
    glm::vec3 targetVelocity{0.0f};
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    float positionLerpSpeed = 12.0f;
    float rotationLerpSpeed = 16.0f;
    float snapDistance = 8.0f;
    float serverTickRate = 20.0f;
    float interpolationDelayTicks = 2.0f;
    float renderServerTick = 0.0f;
    uint32_t latestServerTick = 0;
    std::array<NetworkSnapshotSample, SnapshotBufferSize> snapshots{};
    std::size_t snapshotCount = 0;
    bool hasRenderServerTick = false;
    bool hasTarget = false;
};

} // namespace ecs

#endif // MECRAFT_NETWORK_COMPONENTS_H
