#ifndef MECRAFT_NET_PACKET_CODEC_H
#define MECRAFT_NET_PACKET_CODEC_H

#include "Protocol.h"
#include "../world/chunk/Chunk.h"
#include <cstdint>
#include <vector>
#include <cstring>
#include <utility>
#include <array>

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
        pushU8(buf, kChunkEncodingRleSubChunks);
        for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
            const SubChunk* subChunk = msg.chunk->getSubChunk(scy);
            bool hasExplicitLight = false;
            const int yBase = scy * SubChunk::SIZE;
            for (int ly = 0; ly < SubChunk::SIZE && !hasExplicitLight; ++ly) {
                const int y = yBase + ly;
                for (int z = 0; z < Chunk::SIZE_Z && !hasExplicitLight; ++z) {
                    for (int x = 0; x < Chunk::SIZE_X; ++x) {
                        if (msg.chunk->getPackedLight(x, y, z) != implicitPackedLight(*msg.chunk, x, y, z)) {
                            hasExplicitLight = true;
                            break;
                        }
                    }
                }
            }

            pushU8(buf, (subChunk || hasExplicitLight) ? 1 : 0);
            if (!subChunk && !hasExplicitLight) {
                continue;
            }

            std::array<BlockID, SubChunk::BLOCK_COUNT> blocks{};
            std::array<uint8_t, SubChunk::BLOCK_COUNT> lights{};
            std::size_t index = 0;
            for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
                const int y = yBase + ly;
                for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                    for (int x = 0; x < Chunk::SIZE_X; ++x) {
                        blocks[index] = msg.chunk->getBlock(x, y, z);
                        lights[index] = msg.chunk->getPackedLight(x, y, z);
                        ++index;
                    }
                }
            }
            encodeRleU16(buf, blocks.data(), blocks.size());
            encodeRleU8(buf, lights.data(), lights.size());
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
        pushU16(buf, msg.playerHealth);
        pushU16(buf, msg.playerMaxHealth);
        pushU8(buf, msg.playerHurt ? 1 : 0);
        pushU8(buf, msg.playerRespawned ? 1 : 0);
        pushU8(buf, msg.playerDead ? 1 : 0);
        pushU8(buf, msg.playerPoseCorrected ? 1 : 0);
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
        pushFloat(buf, msg.playerPosition.x);
        pushFloat(buf, msg.playerPosition.y);
        pushFloat(buf, msg.playerPosition.z);
        pushFloat(buf, msg.playerVelocity.x);
        pushFloat(buf, msg.playerVelocity.y);
        pushFloat(buf, msg.playerVelocity.z);
        pushFloat(buf, msg.yaw);
        pushFloat(buf, msg.pitch);
        pushU8(buf, msg.jump ? 1 : 0);
        pushU8(buf, msg.sneak ? 1 : 0);
        pushU8(buf, msg.sprint ? 1 : 0);
        pushU32(buf, msg.actions);
        pushU8(buf, msg.selectedHotbarSlot);
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

    /// Encode a ClientBlockAction message into payload bytes.
    static std::vector<uint8_t> encodeClientBlockAction(const ClientBlockAction& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.sequence);
        pushU8(buf, static_cast<uint8_t>(msg.action));
        pushI32(buf, msg.targetBlock.x);
        pushI32(buf, msg.targetBlock.y);
        pushI32(buf, msg.targetBlock.z);
        pushI32(buf, msg.placeBlock.x);
        pushI32(buf, msg.placeBlock.y);
        pushI32(buf, msg.placeBlock.z);
        pushI32(buf, msg.hitNormal.x);
        pushI32(buf, msg.hitNormal.y);
        pushI32(buf, msg.hitNormal.z);
        pushFloat(buf, msg.playerPosition.x);
        pushFloat(buf, msg.playerPosition.y);
        pushFloat(buf, msg.playerPosition.z);
        pushU16(buf, msg.blockState);
        return buf;
    }

    static std::vector<uint8_t> encodeClientChatMessage(const ClientChatMessage& msg) {
        std::vector<uint8_t> buf;
        pushString(buf, msg.message);
        return buf;
    }

    static std::vector<uint8_t> encodeClientCommandRequest(const ClientCommandRequest& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.sequence);
        pushString(buf, msg.command);
        return buf;
    }

    static std::vector<uint8_t> encodeClientRespawnRequest(const ClientRespawnRequest& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.sequence);
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
            pushU16(buf, u.stateId);
            pushU32(buf, static_cast<uint32_t>(u.packedLightPatch.size()));
            buf.insert(buf.end(), u.packedLightPatch.begin(), u.packedLightPatch.end());
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
        pushFloat(buf, msg.yaw);
        pushFloat(buf, msg.pitch);
        pushU16(buf, msg.itemId);
        pushU16(buf, msg.stackCount);
        pushString(buf, msg.entityId);
        return buf;
    }

    /// Encode an EntityDespawn message into payload bytes.
    static std::vector<uint8_t> encodeEntityDespawn(const EntityDespawnMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.netId);
        return buf;
    }

    /// Encode an EntityImpact message into payload bytes.
    static std::vector<uint8_t> encodeEntityImpact(const EntityImpactMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.netId);
        pushFloat(buf, msg.position.x);
        pushFloat(buf, msg.position.y);
        pushFloat(buf, msg.position.z);
        pushU16(buf, msg.particleBlockId);
        pushU16(buf, msg.particleCount);
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
            pushFloat(buf, e.pitch);
            pushU16(buf, e.health);
            pushU16(buf, e.maxHealth);
            pushU8(buf, e.hurt ? 1 : 0);
        }
        return buf;
    }

    static std::vector<uint8_t> encodeServerChatMessage(const ServerChatMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.senderId);
        pushString(buf, msg.senderName);
        pushString(buf, msg.message);
        return buf;
    }

    static std::vector<uint8_t> encodeServerSystemMessage(const ServerSystemMessage& msg) {
        std::vector<uint8_t> buf;
        pushU8(buf, static_cast<uint8_t>(msg.kind));
        pushString(buf, msg.message);
        return buf;
    }

    static std::vector<uint8_t> encodeCommandResult(const CommandResultMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.sequence);
        pushU8(buf, msg.success ? 1 : 0);
        pushString(buf, msg.message);
        return buf;
    }

    static std::vector<uint8_t> encodeWorldStateSnapshot(const WorldStateSnapshotMessage& msg) {
        std::vector<uint8_t> buf;
        pushFloat(buf, msg.timeOfDay);
        pushU8(buf, static_cast<uint8_t>(msg.weather));
        return buf;
    }

    static std::vector<uint8_t> encodePlayerModeUpdate(const PlayerModeUpdateMessage& msg) {
        std::vector<uint8_t> buf;
        pushU32(buf, msg.clientId);
        pushU8(buf, static_cast<uint8_t>(msg.mode));
        return buf;
    }

    static std::vector<uint8_t> encodeInventorySnapshot(const InventorySnapshotMessage& msg) {
        std::vector<uint8_t> buf;
        pushU8(buf, msg.selectedHotbarSlot);
        pushU32(buf, static_cast<uint32_t>(msg.slots.size()));
        for (const InventorySlotData& slot : msg.slots) {
            pushU16(buf, slot.itemId);
            pushU8(buf, slot.stackCount);
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
        if (offset < size) {
            const uint8_t encoding = readU8(data, offset);
            if (encoding == kChunkEncodingRleSubChunks) {
                auto chunk = std::make_shared<Chunk>(out.chunkX, out.chunkZ);
                std::vector<uint8_t> packedLight(Chunk::BLOCK_COUNT);
                for (int y = 0; y < Chunk::SIZE_Y; ++y) {
                    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                        for (int x = 0; x < Chunk::SIZE_X; ++x) {
                            packedLight[Chunk::toIndex(x, y, z)] = implicitPackedLight(*chunk, x, y, z);
                        }
                    }
                }

                for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                    if (offset >= size) {
                        return false;
                    }
                    const bool hasSubChunk = readU8(data, offset) != 0;
                    if (!hasSubChunk) {
                        continue;
                    }

                    std::array<BlockID, SubChunk::BLOCK_COUNT> blocks{};
                    std::array<uint8_t, SubChunk::BLOCK_COUNT> lights{};
                    if (!decodeRleU16(data, size, offset, blocks.data(), blocks.size()) ||
                        !decodeRleU8(data, size, offset, lights.data(), lights.size())) {
                        return false;
                    }

                    std::size_t index = 0;
                    const int yBase = scy * SubChunk::SIZE;
                    for (int ly = 0; ly < SubChunk::SIZE; ++ly) {
                        const int y = yBase + ly;
                        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                                if (blocks[index] != RUNTIME_ID_NULL) {
                                    chunk->setBlockFast(x, y, z, blocks[index]);
                                }
                                packedLight[Chunk::toIndex(x, y, z)] = lights[index];
                                ++index;
                            }
                        }
                    }
                }

                for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
                    if (SubChunk* subChunk = chunk->getSubChunk(scy)) {
                        subChunk->inferType();
                    }
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
        }
        if (size < kHeaderBytes + kBlockBytes + kLightBytes) {
            return false;
        }

        auto chunk = std::make_shared<Chunk>(out.chunkX, out.chunkZ);
        offset = kHeaderBytes;
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
        if (size < 32) return false;
        size_t offset = 0;
        out.serverTick = readU32(data, offset);
        out.ackInputSequence = readU32(data, offset);
        out.authoritativePosition.x = readFloat(data, offset);
        out.authoritativePosition.y = readFloat(data, offset);
        out.authoritativePosition.z = readFloat(data, offset);
        out.authoritativeVelocity.x = readFloat(data, offset);
        out.authoritativeVelocity.y = readFloat(data, offset);
        out.authoritativeVelocity.z = readFloat(data, offset);
        out.playerHealth = 20;
        out.playerMaxHealth = 20;
        out.playerHurt = false;
        out.playerRespawned = false;
        out.playerDead = false;
        out.playerPoseCorrected = false;
        if (size >= 37) {
            out.playerHealth = readU16(data, offset);
            out.playerMaxHealth = readU16(data, offset);
            out.playerHurt = readU8(data, offset) != 0;
        }
        if (size >= 38) {
            out.playerRespawned = readU8(data, offset) != 0;
        }
        if (size >= 39) {
            out.playerDead = readU8(data, offset) != 0;
        }
        if (size >= 40) {
            out.playerPoseCorrected = readU8(data, offset) != 0;
        }
        return true;
    }

    static bool decodeClientInput(const uint8_t* data, size_t size, ClientInput& out) {
        constexpr size_t kLegacyInputBytes = 35;              // input + actions, before position snapshots
        constexpr size_t kModernInputBytes = 67;              // adds position, velocity, yaw, pitch
        constexpr size_t kModernInputWithSelectedBytes = 68;  // appends selected hotbar slot
        constexpr size_t kInputTailBytes = 7;                 // jump, sneak, sprint, actions
        constexpr size_t kSelectedHotbarSlotOffset = 67;
        if (size < kLegacyInputBytes) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        out.dt = readFloat(data, offset);
        out.moveInput.x = readFloat(data, offset);
        out.moveInput.y = readFloat(data, offset);
        out.moveInput.z = readFloat(data, offset);
        out.lookDelta.x = readFloat(data, offset);
        out.lookDelta.y = readFloat(data, offset);
        if (size >= kModernInputBytes) {
            out.playerPosition.x = readFloat(data, offset);
            out.playerPosition.y = readFloat(data, offset);
            out.playerPosition.z = readFloat(data, offset);
            out.playerVelocity.x = readFloat(data, offset);
            out.playerVelocity.y = readFloat(data, offset);
            out.playerVelocity.z = readFloat(data, offset);
            out.yaw = readFloat(data, offset);
            out.pitch = readFloat(data, offset);
        }
        if (size < offset + kInputTailBytes) return false;
        out.jump = readU8(data, offset) != 0;
        out.sneak = readU8(data, offset) != 0;
        out.sprint = readU8(data, offset) != 0;
        out.actions = readU32(data, offset);
        out.selectedHotbarSlot = 0;
        if (size >= kModernInputWithSelectedBytes) {
            out.selectedHotbarSlot = data[kSelectedHotbarSlotOffset];
        }
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

    static bool decodeClientBlockAction(const uint8_t* data, size_t size, ClientBlockAction& out) {
        if (size < 51) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        out.action = static_cast<ClientBlockActionType>(readU8(data, offset));
        out.targetBlock.x = readI32(data, offset);
        out.targetBlock.y = readI32(data, offset);
        out.targetBlock.z = readI32(data, offset);
        out.placeBlock.x = readI32(data, offset);
        out.placeBlock.y = readI32(data, offset);
        out.placeBlock.z = readI32(data, offset);
        out.hitNormal.x = readI32(data, offset);
        out.hitNormal.y = readI32(data, offset);
        out.hitNormal.z = readI32(data, offset);
        out.playerPosition.x = readFloat(data, offset);
        out.playerPosition.y = readFloat(data, offset);
        out.playerPosition.z = readFloat(data, offset);
        out.blockState = readU16(data, offset);
        return true;
    }

    static bool decodeClientChatMessage(const uint8_t* data, size_t size, ClientChatMessage& out) {
        size_t offset = 0;
        return readString(data, size, offset, out.message);
    }

    static bool decodeClientCommandRequest(const uint8_t* data, size_t size, ClientCommandRequest& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        return readString(data, size, offset, out.command);
    }

    static bool decodeClientRespawnRequest(const uint8_t* data, size_t size, ClientRespawnRequest& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        return true;
    }

    static bool decodeBlockUpdateBatch(const uint8_t* data, size_t size, BlockUpdateBatchMessage& out) {
        if (size < 4) return false;
        size_t offset = 0;
        const uint32_t count = readU32(data, offset);
        const bool hasLightPatches = size > 4 + static_cast<size_t>(count) * 14;
        out.updates.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (offset + 14 > size) return false;
            out.updates[i].x = readI32(data, offset);
            out.updates[i].y = readI32(data, offset);
            out.updates[i].z = readI32(data, offset);
            out.updates[i].stateId = readU16(data, offset);
            if (hasLightPatches) {
                if (offset + 4 > size) return false;
                const uint32_t lightCount = readU32(data, offset);
                if (offset + lightCount > size) return false;
                out.updates[i].packedLightPatch.assign(data + offset, data + offset + lightCount);
                offset += lightCount;
            }
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
        constexpr size_t kOldSpawnBytes = 33;
        constexpr size_t kSpawnBytes = 41;
        if (size < kOldSpawnBytes) return false;
        size_t offset = 0;
        out.netId = readU32(data, offset);
        out.kind = static_cast<EntityKind>(readU8(data, offset));
        out.position.x = readFloat(data, offset);
        out.position.y = readFloat(data, offset);
        out.position.z = readFloat(data, offset);
        out.velocity.x = readFloat(data, offset);
        out.velocity.y = readFloat(data, offset);
        out.velocity.z = readFloat(data, offset);
        if (size >= kSpawnBytes) {
            out.yaw = readFloat(data, offset);
            out.pitch = readFloat(data, offset);
        }
        out.itemId = readU16(data, offset);
        out.stackCount = readU16(data, offset);
        out.entityId.clear();
        if (offset < size && !readString(data, size, offset, out.entityId)) {
            return false;
        }
        return true;
    }

    static bool decodeEntityDespawn(const uint8_t* data, size_t size, EntityDespawnMessage& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.netId = readU32(data, offset);
        return true;
    }

    static bool decodeEntityImpact(const uint8_t* data, size_t size, EntityImpactMessage& out) {
        if (size < 18) return false;
        if (size != 18 && size < 20) return false;
        size_t offset = 0;
        out.netId = readU32(data, offset);
        out.position.x = readFloat(data, offset);
        out.position.y = readFloat(data, offset);
        out.position.z = readFloat(data, offset);
        out.particleBlockId = readU16(data, offset);
        out.particleCount = 14;
        if (size >= offset + 2) {
            out.particleCount = readU16(data, offset);
        }
        return true;
    }

    static bool decodeEntitySnapshot(const uint8_t* data, size_t size, EntitySnapshotMessage& out) {
        if (size < 8) return false;
        size_t offset = 0;
        out.serverTick = readU32(data, offset);
        const uint32_t count = readU32(data, offset);
        const size_t remaining = size - 8;
        constexpr size_t kLegacyNoYawItemBytes = 28;
        constexpr size_t kLegacyNoPitchItemBytes = 32;
        constexpr size_t kLegacyWithPitchItemBytes = 36;
        constexpr size_t kEntityStateItemBytes = 41;
        size_t itemBytes = 0;
        if (count == 0) {
            if (remaining != 0) {
                return false;
            }
        } else if (remaining == count * kEntityStateItemBytes) {
            itemBytes = kEntityStateItemBytes;
        } else if (remaining == count * kLegacyWithPitchItemBytes) {
            itemBytes = kLegacyWithPitchItemBytes;
        } else if (remaining == count * kLegacyNoPitchItemBytes) {
            itemBytes = kLegacyNoPitchItemBytes;
        } else if (remaining == count * kLegacyNoYawItemBytes) {
            itemBytes = kLegacyNoYawItemBytes;
        } else {
            return false;
        }
        out.entities.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            out.entities[i].netId = readU32(data, offset);
            out.entities[i].position.x = readFloat(data, offset);
            out.entities[i].position.y = readFloat(data, offset);
            out.entities[i].position.z = readFloat(data, offset);
            out.entities[i].velocity.x = readFloat(data, offset);
            out.entities[i].velocity.y = readFloat(data, offset);
            out.entities[i].velocity.z = readFloat(data, offset);
            if (itemBytes >= kLegacyNoPitchItemBytes) {
                out.entities[i].yaw = readFloat(data, offset);
            }
            if (itemBytes >= kLegacyWithPitchItemBytes) {
                out.entities[i].pitch = readFloat(data, offset);
            }
            if (itemBytes >= kEntityStateItemBytes) {
                out.entities[i].health = readU16(data, offset);
                out.entities[i].maxHealth = readU16(data, offset);
                out.entities[i].hurt = readU8(data, offset) != 0;
            }
        }
        return true;
    }

    static bool decodeServerChatMessage(const uint8_t* data, size_t size, ServerChatMessage& out) {
        if (size < 4) return false;
        size_t offset = 0;
        out.senderId = readU32(data, offset);
        return readString(data, size, offset, out.senderName) &&
               readString(data, size, offset, out.message);
    }

    static bool decodeServerSystemMessage(const uint8_t* data, size_t size, ServerSystemMessage& out) {
        if (size < 1) return false;
        size_t offset = 0;
        out.kind = static_cast<ChatMessageKind>(readU8(data, offset));
        return readString(data, size, offset, out.message);
    }

    static bool decodeCommandResult(const uint8_t* data, size_t size, CommandResultMessage& out) {
        if (size < 5) return false;
        size_t offset = 0;
        out.sequence = readU32(data, offset);
        out.success = readU8(data, offset) != 0;
        return readString(data, size, offset, out.message);
    }

    static bool decodeWorldStateSnapshot(const uint8_t* data, size_t size, WorldStateSnapshotMessage& out) {
        if (size < 5) return false;
        size_t offset = 0;
        out.timeOfDay = readFloat(data, offset);
        out.weather = static_cast<NetworkWeatherType>(readU8(data, offset));
        return true;
    }

    static bool decodePlayerModeUpdate(const uint8_t* data, size_t size, PlayerModeUpdateMessage& out) {
        if (size < 5) return false;
        size_t offset = 0;
        out.clientId = readU32(data, offset);
        out.mode = static_cast<NetworkGameplayMode>(readU8(data, offset));
        return true;
    }

    static bool decodeInventorySnapshot(const uint8_t* data, size_t size, InventorySnapshotMessage& out) {
        if (size < 5) return false;
        size_t offset = 0;
        out.selectedHotbarSlot = readU8(data, offset);
        const uint32_t count = readU32(data, offset);
        if (offset + static_cast<size_t>(count) * 3 > size) {
            return false;
        }
        out.slots.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            out.slots[i].itemId = readU16(data, offset);
            out.slots[i].stackCount = readU8(data, offset);
        }
        return true;
    }

private:
    static constexpr size_t kHeaderSize = 6;  // channel(1) + type(1) + payload_size(4)
    static constexpr uint8_t kChunkEncodingRleSubChunks = 1;

    static uint8_t implicitPackedLight(const Chunk& chunk, const int x, const int y, const int z) {
        return static_cast<uint8_t>((y >= chunk.getHeightMap(x, z) ? 15 : 0) << 4);
    }

    static void encodeRleU16(std::vector<uint8_t>& buf, const uint16_t* values, const size_t count) {
        size_t i = 0;
        while (i < count) {
            const uint16_t value = values[i];
            uint16_t run = 1;
            while (i + run < count && run < 0xFFFFu && values[i + run] == value) {
                ++run;
            }
            pushU16(buf, run);
            pushU16(buf, value);
            i += run;
        }
        pushU16(buf, 0);
    }

    static void encodeRleU8(std::vector<uint8_t>& buf, const uint8_t* values, const size_t count) {
        size_t i = 0;
        while (i < count) {
            const uint8_t value = values[i];
            uint16_t run = 1;
            while (i + run < count && run < 0xFFFFu && values[i + run] == value) {
                ++run;
            }
            pushU16(buf, run);
            pushU8(buf, value);
            i += run;
        }
        pushU16(buf, 0);
    }

    static bool decodeRleU16(const uint8_t* data, const size_t size, size_t& offset, uint16_t* out, const size_t count) {
        size_t written = 0;
        while (offset + 2 <= size) {
            const uint16_t run = readU16(data, offset);
            if (run == 0) {
                return written == count;
            }
            if (offset + 2 > size || written + run > count) return false;
            const uint16_t value = readU16(data, offset);
            for (uint16_t i = 0; i < run; ++i) {
                out[written++] = value;
            }
        }
        return false;
    }

    static bool decodeRleU8(const uint8_t* data, const size_t size, size_t& offset, uint8_t* out, const size_t count) {
        size_t written = 0;
        while (offset + 2 <= size) {
            const uint16_t run = readU16(data, offset);
            if (run == 0) {
                return written == count;
            }
            if (offset + 1 > size || written + run > count) return false;
            const uint8_t value = readU8(data, offset);
            for (uint16_t i = 0; i < run; ++i) {
                out[written++] = value;
            }
        }
        return false;
    }

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
    static void pushString(std::vector<uint8_t>& buf, const std::string& value) {
        pushU32(buf, static_cast<uint32_t>(value.size()));
        buf.insert(buf.end(), value.begin(), value.end());
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
    static bool readString(const uint8_t* data, size_t size, size_t& offset, std::string& out) {
        if (offset + 4 > size) return false;
        const uint32_t length = readU32(data, offset);
        if (offset + length > size) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), length);
        offset += length;
        return true;
    }
};

} // namespace net

#endif // MECRAFT_NET_PACKET_CODEC_H
