#ifndef MECRAFT_CHUNK_SERIALIZER_H
#define MECRAFT_CHUNK_SERIALIZER_H

// ChunkSerializer: converts between in-memory Chunk objects and MCHK binary format.
//
// Serialization reads the SubChunk palette + BitPackedArray data directly,
// writing RuntimeId palette entries as VarUInt values for disk storage.
// Deserialization rejects malformed or out-of-range palette entries.
//
// All methods are stateless and thread-safe (read-only access to Chunk data).

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

class Chunk;

namespace save {

class ChunkSerializer {
public:
    // Serialize a chunk to MCHK payload bytes (encoding + subchunk data, no header).
    [[nodiscard]] static std::vector<uint8_t> serializePayload(const Chunk& chunk);

    // Deserialize MCHK payload bytes into a Chunk.
    // Returns nullptr on error (corrupt data, version mismatch).
    [[nodiscard]] static std::shared_ptr<Chunk> deserializePayload(
        int32_t cx, int32_t cz,
        const uint8_t* data, size_t size);

    // Serialize a complete MCHK file (header + payload with CRC).
    [[nodiscard]] static std::vector<uint8_t> serializeFile(const Chunk& chunk);

    // Deserialize a complete MCHK file into a Chunk.
    // Validates magic, version, and CRC.
    [[nodiscard]] static std::shared_ptr<Chunk> deserializeFile(
        const uint8_t* data, size_t size);
};

} // namespace save

#endif // MECRAFT_CHUNK_SERIALIZER_H
