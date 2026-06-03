#ifndef MECRAFT_NET_PROTOCOL_H
#define MECRAFT_NET_PROTOCOL_H

#include <cstdint>
#include <memory>
#include <vector>
#include <any>
#include <glm/glm.hpp>

class Chunk;

namespace net {

/// Entity network ID type (must match ecs::EntityNetId).
using EntityNetId = uint32_t;

/// Entity kind for spawn messages.
enum class EntityKind : uint8_t {
    Drop = 0,
    Player = 1,
    Mob = 2,
};

using ClientId = uint32_t;
using TickId = uint32_t;

/// Network channel types, mapped to ENet channels in Phase 6.
enum class PacketChannel : uint8_t {
    ReliableControl = 0,  // Login, handshake, disconnect, config
    ReliableWorld = 1,    // Chunk data, block updates, inventory
    UnreliableState = 2,  // High-frequency entity snapshots, player input
    ReliableChat = 3,     // Chat, commands, system messages
};

/// Message type discriminator for the protocol.
enum class MessageType : uint8_t {
    // Client -> Server
    ClientHello,
    ClientInput,
    ClientReady,
    ClientViewConfig,
    ClientBlockAction,

    // Server -> Client
    ServerHello,
    ChunkData,
    ChunkUnload,
    BlockUpdateBatch,
    ServerSnapshot,
    EntitySpawn,
    EntityDespawn,
    EntitySnapshot,
    InventorySnapshot,

    // Bidirectional
    KeepAlive,
};

/// A protocol packet with channel, type, and payload.
/// For in-process transport, inProcessPayload carries zero-copy data.
/// For network transport (Phase 6), payload carries binary-encoded data.
struct Packet {
    PacketChannel channel = PacketChannel::ReliableControl;
    MessageType type = MessageType::ClientHello;
    std::vector<uint8_t> payload;
    std::any inProcessPayload;  // Zero-copy for in-process transport
};

// ===========================================================================
// Client -> Server messages
// ===========================================================================

/// Initial handshake from client to server.
struct ClientHello {
    uint32_t protocolVersion = 1;
};

/// Player input sampled at the client's fixed update rate.
struct ClientInput {
    uint32_t sequence = 0;
    float dt = 0.0f;
    glm::vec3 moveInput = glm::vec3(0.0f);
    glm::vec2 lookDelta = glm::vec2(0.0f);
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 playerVelocity = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    uint32_t actions = 0;  // Bitfield for break/place etc.
};

/// Client signals readiness to receive world data.
struct ClientReady {};

/// Client sends its view configuration to the server.
struct ClientViewConfig {
    int renderDistance = 16;
};

enum class ClientBlockActionType : uint8_t {
    Break = 0,
    Place = 1,
};

struct ClientBlockAction {
    uint32_t sequence = 0;
    ClientBlockActionType action = ClientBlockActionType::Break;
    glm::ivec3 targetBlock = glm::ivec3(0);
    glm::ivec3 placeBlock = glm::ivec3(0);
    glm::ivec3 hitNormal = glm::ivec3(0);
    glm::vec3 playerPosition = glm::vec3(0.0f);
    uint16_t blockState = 0;
};

// ===========================================================================
// Server -> Client messages
// ===========================================================================

/// Server response to ClientHello.
struct ServerHello {
    uint32_t protocolVersion = 1;
    ClientId assignedId = 0;
    glm::vec3 spawnPosition = glm::vec3(0.0f);
};

/// Chunk data sent from server to client.
/// For in-process transport, chunk carries a shared_ptr directly.
struct ChunkDataMessage {
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
    uint32_t revision = 0;
    std::shared_ptr<Chunk> chunk;  // In-process zero-copy; null for network path
};

/// Notification that a chunk should be unloaded on the client.
struct ChunkUnloadMessage {
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
};

/// Batch of block updates.
struct BlockUpdateEntry {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    // 0xFFFF means this entry only carries light data and must not edit a block.
    uint16_t blockId = 0;
    // Optional light payload. A SubChunk::BLOCK_COUNT-sized payload is the
    // complete light section containing y. Odd-sized cubic patches are centered
    // on x/y/z and written in dy, dz, dx nested-loop order. A Chunk::BLOCK_COUNT
    // payload is a full packed-light snapshot for backward compatibility.
    std::vector<uint8_t> packedLightPatch;
};

struct BlockUpdateBatchMessage {
    std::vector<BlockUpdateEntry> updates;
};

/// Authoritative world state snapshot from the server.
struct ServerSnapshot {
    TickId serverTick = 0;
    uint32_t ackInputSequence = 0;
    glm::vec3 authoritativePosition = glm::vec3(0.0f);
    glm::vec3 authoritativeVelocity = glm::vec3(0.0f);
};

// ===========================================================================
// Entity synchronization messages
// ===========================================================================

/// Server tells client to create a new entity.
struct EntitySpawnMessage {
    EntityNetId netId = 0;
    EntityKind kind = EntityKind::Drop;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint16_t itemId = 0;      // For drops
    uint16_t stackCount = 0;  // For drops
};

/// Server tells client to destroy an entity.
struct EntityDespawnMessage {
    EntityNetId netId = 0;
};

/// A single entity's snapshot data.
struct EntitySnapshotItem {
    EntityNetId netId = 0;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// Batch of entity snapshots from the server.
struct EntitySnapshotMessage {
    TickId serverTick = 0;
    std::vector<EntitySnapshotItem> entities;
};

/// Inventory slot data for network sync.
struct InventorySlotData {
    uint16_t itemId = 0;
    uint8_t stackCount = 0;
};

/// Server sends authoritative inventory state to client.
struct InventorySnapshotMessage {
    uint8_t selectedHotbarSlot = 0;
    std::vector<InventorySlotData> slots;  // 36 slots (hotbar + main inventory)
};

} // namespace net

#endif // MECRAFT_NET_PROTOCOL_H
