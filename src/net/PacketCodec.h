#ifndef MECRAFT_NET_PACKET_CODEC_H
#define MECRAFT_NET_PACKET_CODEC_H

#include "Protocol.h"
#include "../world/chunk/Chunk.h"
#include <cstdint>
#include <vector>
#include <cstring>
#include <utility>

namespace net {

/// Binary codec for serializing/deserializing protocol packets.
/// Format: [channel:u8][type:u8][payload_size:u32][payload...]
class PacketCodec {
public:
    /// Encode a packet into a binary buffer for network transmission.
    /// The inProcessPayload field is NOT serialized — only payload bytes.
    static std::vector<uint8_t> encode(const Packet& packet) {
        std::vector<uint8_t> buffer;
        buffer.reserve(2 + 4 + packet.payload.size());

        // Header
        pushU8(buffer, static_cast<uint8_t>(packet.channel));
        pushU8(buffer, static_cast<uint8_t>(packet.type));
        pushU32(buffer, static_cast<uint32_t>(packet.payload.size()));

        // Payload
        buffer.insert(buffer.end(), packet.payload.begin(), packet.payload.end());

        return buffer;
    }

    /// Decode a binary buffer into a Packet.
    /// Returns false if the buffer is too short or malformed.
    static bool decode(const uint8_t* data, size_t size, Packet& out) {
        if (size < kHeaderSize) {
            return false;
        }

        size_t offset = 0;
        out.channel = static_cast<PacketChannel>(readU8(data, offset));
        out.type = static_cast<MessageType>(readU8(data, offset));
        const uint32_t payloadSize = readU32(data, offset);

        if (size < kHeaderSize + payloadSize) {
            return false;
        }

        out.payload.assign(data + offset, data + offset + payloadSize);
        return true;
    }

    // =========================================================================
    // Typed message encoding helpers
    // =========================================================================

    /// Encode a ServerHello message into payload bytes.
    static std::vector<uint8_t> encodeServerHello(const ServerHello& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.protocolVersion);
        pushU32(buf, msg.assignedId);
        pushFloat(buf, msg.spawnPosition.x);
        pushFloat(buf, msg.spawnPosition.y);
        pushFloat(buf, msg.spawnPosition.z);
        return buf;
    }

    /// Encode a ChunkData message into payload bytes.
    /// Note: chunk data is NOT serialized in binary (use inProcessPayload for local).
    /// For network path, chunk data would need full block serialization (Phase 6+).
    static std::vector<uint8_t> encodeChunkData(const ChunkDataMessage& msg) {
        std::vector<uint8_t> buf;
        pushI32(buf, msg.chunkX);
        pushI32(buf, msg.chunkZ);
        pushU32(buf, msg.revision);
        pushU8(buf, msg.chunk ? 1 : 0);
        if (!msg.chunk) {
            return buf;
        }
        buf.reserve(buf.size() + Chunk::BLOCK_COUNT * 3);
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    pushU16(buf, msg.chunk->getBlock(x, y, z));
                }
            }
        }
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    pushU8(buf, msg.chunk->getPackedLight(x, y, z));
                }
            }
        }
        return buf;
    }

    /// Encode a ServerSnapshot message into payload bytes.
    static std::vector<uint8_t> encodeServerSnapshot(const ServerSnapshot& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.serverTick);
        pushU32(buf, msg.ackInputSequence);
        pushFloat(buf, msg.authoritativePosition.x);
        pushFloat(buf, msg.authoritativePosition.y);
        pushFloat(buf, msg.authoritativePosition.z);
        pushFloat(buf, msg.authoritativeVelocity.x);
        pushFloat(buf, msg.authoritativeVelocity.y);
        pushFloat(buf, msg.authoritativeVelocity.z);
        return buf;
    }

    /// Encode a ClientInput message into payload bytes.
    static std::vector<uint8_t> encodeClientInput(const ClientInput& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.sequence);
        pushFloat(buf, msg.dt);
        pushFloat(buf, msg.moveInput.x);
        pushFloat(buf, msg.moveInput.y);
        pushFloat(buf, msg.moveInput.z);
        pushFloat(buf, msg.lookDelta.x);
        pushFloat(buf, msg.lookDelta.y);
        pushU8(buf, msg.jump ? 1 : 0);
        pushU8(buf, msg.sneak ? 1 : 0);
        pushU8(buf, msg.sprint ? 1 : 0);
        pushU32(buf, msg.actions);
        return buf;
    }

    /// Encode a ClientHello message into payload bytes.
    static std::vector<uint8_t> encodeClientHello(const ClientHello& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.protocolVersion);
        return buf;
    }

    /// Encode a ClientViewConfig message into payload bytes.
    static std::vector<uint8_t> encodeClientViewConfig(const ClientViewConfig& msg) {
        std::vector<uint8_t> buf;
        pushI32(buf, msg.renderDistance);
        return buf;
    }

    /// Encode a BlockUpdateBatch message into payload bytes.
    static std::vector<uint8_t> encodeBlockUpdateBatch(const BlockUpdateBatchMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, static_cast<uint32_t>(msg.updates.size()));
        for (const auto& u : msg.updates) {
            pushI32(buf, u.x);
            pushI32(buf, u.y);
            pushI32(buf, u.z);
            pushU16(buf, u.blockId);
        }
        return buf;
    }

    /// Encode a ChunkUnload message into payload bytes.
    static std::vector<uint8_t> encodeChunkUnload(const ChunkUnloadMessage& msg) {
        std::vector<uint8_t> buf;
        pushI32(buf, msg.chunkX);
        pushI32(buf, msg.chunkZ);
        return buf;
    }

    /// Encode an EntitySpawn message into payload bytes.
    static std::vector<uint8_t> encodeEntitySpawn(const EntitySpawnMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.netId);
        pushU8(buf, static_cast<uint8_t>(msg.kind));
        pushFloat(buf, msg.position.x);
        pushFloat(buf, msg.position.y);
        pushFloat(buf, msg.position.z);
        pushFloat(buf, msg.velocity.x);
        pushFloat(buf, msg.velocity.y);
        pushFloat(buf, msg.velocity.z);
        pushU16(buf, msg.itemId);
        pushU16(buf, msg.stackCount);
        return buf;
    }

    /// Encode an EntityDespawn message into payload bytes.
    static std::vector<uint8_t> encodeEntityDespawn(const EntityDespawnMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.netId);
        return buf;
    }

    /// Encode an EntitySnapshot message into payload bytes.
    static std::vector<uint8_t> encodeEntitySnapshot(const EntitySnapshotMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.serverTick);
        pushU32(buf, static_cast<uint32_t>(msg.entities.size()));
        for (const auto& e : msg.entities) {
            pushU32(buf, e.netId);
            pushFloat(buf, e.position.x);
            pushFloat(buf, e.position.y);
            pushFloat(buf, e.position.z);
            pushFloat(buf, e.velocity.x);
            pushFloat(buf, e.velocity.y);
            pushFloat(buf, e.velocity.z);
            pushFloat(buf, e.yaw);
        }
        return buf;
    }

    // =========================================================================
    // Typed message decoding helpers
    // =========================================================================

    static bool decodeServerHello(const uint8_t* data, size_t size, ServerHello& out) {
        if (size < 20) return false;
        size_t offset = 0;
        out.protocolVersion = readU32(data, offset);
        out.assignedId = readU32(data, offset);
        out.spawnPosition.x = readFloat(data, offset);
        out.spawnPosition.y = readFloat(data, offset);
        out.spawnPosition.z = readFloat(data, offset);
        return true;
    }

    static bool decodeChunkData(const uint8_t* data, size_t size, ChunkDataMessage& out) {
        constexpr size_t kHeaderBytes = 13;
        constexpr size_t kBlockBytes = Chunk::BLOCK_COUNT * sizeof(uint16_t);
        constexpr size_t kLightBytes = Chunk::BLOCK_COUNT;
        if (size < kHeaderBytes) return false;

        size_t offset = 0;
        out.chunkX = readI32(data, offset);
        out.chunkZ = readI32(data, offset);
        out.revision = readU32(data, offset);
        const bool hasChunkData = readU8(data, offset) != 0;
        if (!hasChunkData) {
            out.chunk.reset();
            return true;
        }
        if (size < kHeaderBytes + kBlockBytes + kLightBytes) {
            return false;
        }

        auto chunk = std::make_shared<Chunk>(out.chunkX, out.chunkZ);
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                for (int x = 0; x < Chunk::SIZE_X; ++x) {
                    chunk->setBlockFast(x, y, z, readU16(data, offset));
                }
            }
        }
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            if (SubChunk* subChunk = chunk->getSubChunk(scy)) {
                subChunk->inferType();
            }
        }

        std::vector<uint8_t> packedLight(Chunk::BLOCK_COUNT);
        for (uint8_t& light : packedLight) {
            light = readU8(data, offset);
        }
        chunk->replacePackedLight(packedLight.data(), packedLight.size());

        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                chunk->recalcHeightMap(x, z);
            }
        }
        chunk->markExistingSubChunksDirty();
        out.chunk = std::move(chunk);
        return true;
    }

    static bool decodeServerSnapshot(const uint8_t* data, size_t size, ServerSnapshot& out) {
        if (size < 28) return false;
        size_t offset = 0;
        out.serverTick = readU32(data, offset);
        out.ackInputSequence = readU32(data, offset);
        out.authoritativePosition.x = readFloat(data, offset);
        out.authoritativePosition.y = readFloat(data, offset);
        out.authoritativePosition.z = readFloat(data, offset);
        out.authoritativeVelocity.x = readFloat(data, offset);
        out.authoritativeVelocity.y = readFloat(data, offset);
        out.authoritativeVelocity.z = readFloat(data, offset);
        return true;
    }

    static bool decodeClientInput(const uint8_t* data, size_t size, ClientInput& out) {
        if (size < 27) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        out.dt = readFloat(data, offset);
        out.moveInput.x = readFloat(data, offset);
        out.moveInput.y = readFloat(data, offset);
        out.moveInput.z = readFloat(data, offset);
        out.lookDelta.x = readFloat(data, offset);
        out.lookDelta.y = readFloat(data, offset);
        out.jump = readU8(data, offset) != 0;
        out.sneak = readU8(data, offset) != 0;
        out.sprint = readU8(data, offset) != 0;
        out.actions = readU32(data, offset);
        return true;
    }

    static bool decodeClientHello(const uint8_t* data, size_t size, ClientHello& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.protocolVersion = readU32(data, offset);
        return true;
    }

    static bool decodeClientViewConfig(const uint8_t* data, size_t size, ClientViewConfig& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.renderDistance = static_cast<int>(readI32(data, offset));
        return true;
    }

    static bool decodeBlockUpdateBatch(const uint8_t* data, size_t size, BlockUpdateBatchMessage& out) {
        if (size < 4) return false;
        size_t offset = 0;
        const uint32_t count = readU32(data, offset);
        if (size < 4 + count * 14) return false;
        out.updates.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            out.updates[i].x = readI32(data, offset);
            out.updates[i].y = readI32(data, offset);
            out.updates[i].z = readI32(data, offset);
            out.updates[i].blockId = readU16(data, offset);
        }
        return true;
    }

    static bool decodeChunkUnload(const uint8_t* data, size_t size, ChunkUnloadMessage& out) {
        if (size < 8) return false;
        size_t offset = 0;
        out.chunkX = readI32(data, offset);
        out.chunkZ = readI32(data, offset);
        return true;
    }

    static bool decodeEntitySpawn(const uint8_t* data, size_t size, EntitySpawnMessage& out) {
        if (size < 30) return false;
        size_t offset = 0;
        out.netId = readU32(data, offset);
        out.kind = static_cast<EntityKind>(readU8(data, offset));
        out.position.x = readFloat(data, offset);
        out.position.y = readFloat(data, offset);
        out.position.z = readFloat(data, offset);
        out.velocity.x = readFloat(data, offset);
        out.velocity.y = readFloat(data, offset);
        out.velocity.z = readFloat(data, offset);
        out.itemId = readU16(data, offset);
        out.stackCount = readU16(data, offset);
        return true;
    }

    static bool decodeEntityDespawn(const uint8_t* data, size_t size, EntityDespawnMessage& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.netId = readU32(data, offset);
        return true;
    }

    static bool decodeEntitySnapshot(const uint8_t* data, size_t size, EntitySnapshotMessage& out) {
        if (size < 8) return false;
        size_t offset = 0;
        out.serverTick = readU32(data, offset);
        const uint32_t count = readU32(data, offset);
        if (size < 8 + count * 28) return false;
        out.entities.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            out.entities[i].netId = readU32(data, offset);
            out.entities[i].position.x = readFloat(data, offset);
            out.entities[i].position.y = readFloat(data, offset);
            out.entities[i].position.z = readFloat(data, offset);
            out.entities[i].velocity.x = readFloat(data, offset);
            out.entities[i].velocity.y = readFloat(data, offset);
            out.entities[i].velocity.z = readFloat(data, offset);
            out.entities[i].yaw = readFloat(data, offset);
        }
        return true;
    }

private:
    static constexpr size_t kHeaderSize = 6;  // channel(1) + type(1) + payload_size(4)

    static void pushU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
    static void pushU16(std::vector<uint8_t>& buf, uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    static void pushI32(std::vector<uint8_t>& buf, int32_t v) {
        pushU32(buf, static_cast<uint32_t>(v));
    }
    static void pushU32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }
    static void pushFloat(std::vector<uint8_t>& buf, float v) {
        uint32_t raw;
        std::memcpy(&raw, &v, sizeof(float));
        pushU32(buf, raw);
    }

    static uint8_t readU8(const uint8_t* data, size_t& offset) {
        return data[offset++];
    }
    static uint16_t readU16(const uint8_t* data, size_t& offset) {
        uint16_t v = static_cast<uint16_t>(data[offset]) |
                     (static_cast<uint16_t>(data[offset + 1]) << 8);
        offset += 2;
        return v;
    }
    static int32_t readI32(const uint8_t* data, size_t& offset) {
        return static_cast<int32_t>(readU32(data, offset));
    }
    static uint32_t readU32(const uint8_t* data, size_t& offset) {
        uint32_t v = static_cast<uint32_t>(data[offset]) |
                     (static_cast<uint32_t>(data[offset + 1]) << 8) |
                     (static_cast<uint32_t>(data[offset + 2]) << 16) |
                     (static_cast<uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return v;
    }
    static float readFloat(const uint8_t* data, size_t& offset) {
        uint32_t raw = readU32(data, offset);
        float v;
        std::memcpy(&v, &raw, sizeof(float));
        return v;
    }
};

} // namespace net

#endif // MECRAFT_NET_PACKET_CODEC_H
