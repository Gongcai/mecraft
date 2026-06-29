#include "ChunkSerializer.h"
#include "../Diagnostics.h"
#include "SaveFormat.h"

#include "../world/chunk/Chunk.h"
#include "../world/block/Block.h"
#include "../world/block/BlockStateIdCodec.h"
#include "../world/block/BlockStateRegistry.h"

#include <cstring>
#include <cstdio>
#include <vector>

namespace save {
namespace {

// ---------------------------------------------------------------------------
// Primitive I/O helpers (file-local, used by both serialization and deserialization)
// ---------------------------------------------------------------------------

void writeVaruint(std::vector<uint8_t>& out, uint32_t value) {
    while (value >= 0x80u) {
        out.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

bool readVaruint(const uint8_t*& cursor, const uint8_t* end, uint32_t& outValue) {
    outValue = 0;
    uint32_t shift = 0;
    for (uint32_t byteIndex = 0; byteIndex < 5; ++byteIndex) {
        if (cursor >= end) {
            return false; // truncated
        }
        const uint8_t byte = *cursor++;
        const uint32_t payload = static_cast<uint32_t>(byte & 0x7Fu);
        if (byteIndex == 4 && payload > 0x0Fu) {
            return false; // overflow
        }
        outValue |= payload << shift;
        if ((byte & 0x80u) == 0) return true;
        shift += 7;
    }
    return false; // overflow
}

bool readBlockStateId(const uint8_t*& cursor, const uint8_t* end, BlockStateId& out) {
    if (!block_state_codec::readRegisteredBlockStateId(cursor, end, out)) {
        MECRAFT_LOG_FPRINTF(stderr,
                            "[Save] Invalid palette block state index for registry size %zu\n",
                            BlockStateRegistry::getStateCount());
        return false;
    }
    return true;
}

void writeBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t size) {
    out.insert(out.end(), data, data + size);
}

void writeU8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

bool readU8(const uint8_t*& cursor, const uint8_t* end, uint8_t& out) {
    if (cursor >= end) return false;
    out = *cursor++;
    return true;
}

bool readU16(const uint8_t*& cursor, const uint8_t* end, uint16_t& out) {
    if (cursor + 2 > end) return false;
    out = static_cast<uint16_t>(cursor[0]) | (static_cast<uint16_t>(cursor[1]) << 8);
    cursor += 2;
    return true;
}

// ---------------------------------------------------------------------------
// Layer serialization/deserialization
// ---------------------------------------------------------------------------

// Serialize one layer (block or fluid) of a subchunk.
void serializeLayer(
    std::vector<uint8_t>& out,
    const Palette& palette,
    const BitPackedArray& data)
{
    const size_t paletteSize = palette.size();

    writeVaruint(out, static_cast<uint32_t>(paletteSize));

    if (paletteSize == 0) {
        return;
    }

    // Write palette entries as registry-owned block state indexes.
    for (size_t i = 0; i < paletteSize; ++i) {
        block_state_codec::writeBlockStateId(out, palette.getStateId(static_cast<uint32_t>(i)));
    }

    // Write bits per entry
    const uint8_t bpe = data.bitsPerEntry();
    writeU8(out, bpe);

    // Write packed data
    const size_t dataSize = data.dataByteSize();
    writeVaruint(out, static_cast<uint32_t>(dataSize));
    if (dataSize > 0) {
        writeBytes(out, reinterpret_cast<const uint8_t*>(data.rawData()), dataSize);
    }
}

// Read a packed index from a bit-packed byte buffer.
uint32_t readPackedIndex(const uint8_t* packedData, size_t packedDataSize,
                         size_t entryIndex, uint8_t bitsPerEntry) {
    if (bitsPerEntry == 0) return 0;

    const size_t bitOffset = entryIndex * bitsPerEntry;
    const size_t byteOffset = bitOffset / 8;
    const size_t bitShift = bitOffset % 8;
    const uint32_t mask = (bitsPerEntry >= 32) ? 0xFFFFFFFFu : ((1u << bitsPerEntry) - 1);

    uint64_t value = 0;
    for (size_t i = 0; i < 5 && (byteOffset + i) < packedDataSize; ++i) {
        value |= static_cast<uint64_t>(packedData[byteOffset + i]) << (i * 8);
    }

    return static_cast<uint32_t>((value >> bitShift) & mask);
}

// Deserialize one layer into a subchunk.
bool deserializeLayer(
    const uint8_t*& cursor, const uint8_t* end,
    SubChunk& sub, bool isFluidLayer)
{
    uint32_t paletteCount = 0;
    if (!readVaruint(cursor, end, paletteCount)) return false;

    if (paletteCount == 0) {
        return true;
    }

    // Read palette entries
    std::vector<BlockStateId> palette;
    palette.reserve(paletteCount);
    for (uint32_t i = 0; i < paletteCount; ++i) {
        BlockStateId stateId = NULL_BLOCK_STATE;
        if (!readBlockStateId(cursor, end, stateId)) return false;
        palette.push_back(stateId);
    }

    // Read bits per entry
    uint8_t bpe = 0;
    if (!readU8(cursor, end, bpe)) return false;
    if (bpe > 32) return false;

    // Read packed data size
    uint32_t packedDataSize = 0;
    if (!readVaruint(cursor, end, packedDataSize)) return false;
    if (static_cast<size_t>(end - cursor) < packedDataSize) return false;

    const uint8_t* packedData = cursor;
    cursor += packedDataSize;

    // Apply blocks to subchunk
    // SubChunk::toIndex(x, y, z) = x + z * SIZE + y * SIZE * SIZE
    for (size_t i = 0; i < SubChunk::BLOCK_COUNT; ++i) {
        const size_t lx = i % SubChunk::SIZE;
        const size_t lz = (i / SubChunk::SIZE) % SubChunk::SIZE;
        const size_t ly = i / (SubChunk::SIZE * SubChunk::SIZE);

        uint32_t paletteIndex = 0;
        if (bpe > 0 && packedDataSize > 0) {
            paletteIndex = readPackedIndex(packedData, packedDataSize,
                                           static_cast<size_t>(i), bpe);
        }

        if (paletteIndex >= palette.size()) {
            MECRAFT_LOG_FPRINTF(stderr,
                                "[Save] Packed palette index %u exceeds palette size %zu\n",
                                paletteIndex,
                                palette.size());
            return false;
        }

        const BlockStateId stateId = palette[paletteIndex];

        if (isFluidLayer) {
            sub.setFluidLayer(static_cast<int>(lx), static_cast<int>(ly),
                              static_cast<int>(lz), stateId);
        } else {
            sub.setBlock(static_cast<int>(lx), static_cast<int>(ly),
                         static_cast<int>(lz), stateId);
        }
    }

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ChunkSerializer public methods
// ---------------------------------------------------------------------------

std::vector<uint8_t> ChunkSerializer::serializePayload(const Chunk& chunk) {
    std::vector<uint8_t> payload;

    writeU8(payload, MCHK_ENCODING_PALLETIZED);

    uint16_t subChunkMask = 0;
    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        const SubChunk* sub = chunk.getSubChunk(scy);
        if (sub && sub->getType() != SubChunkType::Air) {
            subChunkMask |= (1u << scy);
        }
    }
    writeU16(payload, subChunkMask);

    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if ((subChunkMask & (1u << scy)) == 0) continue;

        const SubChunk* sub = chunk.getSubChunk(scy);
        if (!sub) continue;

        writeU8(payload, static_cast<uint8_t>(scy));
        serializeLayer(payload, sub->blockPalette(), sub->blockData());
        serializeLayer(payload, sub->fluidPalette(), sub->fluidData());
    }

    return payload;
}

std::shared_ptr<Chunk> ChunkSerializer::deserializePayload(
    int32_t cx, int32_t cz,
    const uint8_t* data, size_t size)
{
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;

    uint8_t encoding = 0;
    if (!readU8(cursor, end, encoding)) return nullptr;
    if (encoding != MCHK_ENCODING_PALLETIZED) return nullptr;

    uint16_t subChunkMask = 0;
    if (!readU16(cursor, end, subChunkMask)) return nullptr;

    auto chunk = std::make_shared<Chunk>(cx, cz);

    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if ((subChunkMask & (1u << scy)) == 0) continue;

        uint8_t storedScy = 0;
        if (!readU8(cursor, end, storedScy)) return nullptr;
        if (storedScy != static_cast<uint8_t>(scy)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Subchunk Y mismatch: expected %d, got %d\n",
                         scy, storedScy);
            return nullptr;
        }

        SubChunk* sub = chunk->getOrCreateSubChunk(scy);
        if (!sub) return nullptr;

        if (!deserializeLayer(cursor, end, *sub, false)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to deserialize block layer for subchunk %d\n", scy);
            return nullptr;
        }
        if (!deserializeLayer(cursor, end, *sub, true)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to deserialize fluid layer for subchunk %d\n", scy);
            return nullptr;
        }

        sub->inferType();
    }

    return chunk;
}

std::vector<uint8_t> ChunkSerializer::serializeFile(const Chunk& chunk) {
    std::vector<uint8_t> payload = serializePayload(chunk);

    MchkHeader header{};
    header.magic = MCHK_MAGIC;
    header.version = MCHK_VERSION;
    header.flags = 0;
    header.chunkX = chunk.m_chunkX;
    header.chunkZ = chunk.m_chunkZ;
    header.payloadSize = static_cast<uint32_t>(payload.size());
    header.payloadCrc32 = detail::crc32(payload.data(), payload.size());

    std::vector<uint8_t> file;
    file.reserve(sizeof(MchkHeader) + payload.size());
    writeBytes(file, reinterpret_cast<const uint8_t*>(&header), sizeof(MchkHeader));
    writeBytes(file, payload.data(), payload.size());

    return file;
}

std::shared_ptr<Chunk> ChunkSerializer::deserializeFile(
    const uint8_t* data, size_t size)
{
    if (size < sizeof(MchkHeader)) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK file too small (%zu bytes)\n", size);
        return nullptr;
    }

    MchkHeader header;
    std::memcpy(&header, data, sizeof(MchkHeader));

    if (header.magic != MCHK_MAGIC) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid MCHK magic: 0x%08X\n", header.magic);
        return nullptr;
    }

    if (header.version != MCHK_VERSION) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported MCHK version: %u\n", header.version);
        return nullptr;
    }

    if (size < sizeof(MchkHeader) + header.payloadSize) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK file truncated: expected %zu, got %zu\n",
                     sizeof(MchkHeader) + header.payloadSize, size);
        return nullptr;
    }

    const uint8_t* payload = data + sizeof(MchkHeader);
    const uint32_t computedCrc = detail::crc32(payload, header.payloadSize);
    if (computedCrc != header.payloadCrc32) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK CRC mismatch: expected 0x%08X, got 0x%08X\n",
                     header.payloadCrc32, computedCrc);
        return nullptr;
    }

    return deserializePayload(header.chunkX, header.chunkZ, payload, header.payloadSize);
}

} // namespace save
