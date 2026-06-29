#include "ChunkSerializer.h"
#include "../Diagnostics.h"
#include "SaveFormat.h"

#include "../world/chunk/Chunk.h"
#include "../world/block/Block.h"
#include "../world/block/BlockStateIdCodec.h"
#include "../world/block/BlockStateRegistry.h"

#include <cstring>
#include <cstdio>
#include <stdexcept>
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

bool isWireContainerState(const BlockStateId stateId) {
    if (stateId == NULL_BLOCK_STATE) {
        return false;
    }
    return BlockRegistry::getFast(BlockStateRegistry::getBlockId(stateId)).isWireContainer;
}

int checkedLocalX(const Chunk& chunk, const glm::ivec3& position) {
    const int localX = position.x - chunk.m_chunkX * Chunk::SIZE_X;
    if (localX < 0 || localX >= Chunk::SIZE_X) {
        throw std::runtime_error("Wire container save entry is outside the chunk X range");
    }
    return localX;
}

int checkedLocalY(const glm::ivec3& position) {
    if (position.y < 0 || position.y >= Chunk::SIZE_Y) {
        throw std::runtime_error("Wire container save entry is outside the chunk Y range");
    }
    return position.y;
}

int checkedLocalZ(const Chunk& chunk, const glm::ivec3& position) {
    const int localZ = position.z - chunk.m_chunkZ * Chunk::SIZE_Z;
    if (localZ < 0 || localZ >= Chunk::SIZE_Z) {
        throw std::runtime_error("Wire container save entry is outside the chunk Z range");
    }
    return localZ;
}

bool containsPosition(const std::vector<glm::ivec3>& positions, const glm::ivec3& position) {
    for (const glm::ivec3& existing : positions) {
        if (existing == position) {
            return true;
        }
    }
    return false;
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

void serializeWireContainers(std::vector<uint8_t>& out,
                             const Chunk& chunk,
                             const std::vector<WireContainerSaveEntry>& wireContainers) {
    if (wireContainers.size() > static_cast<size_t>(Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z)) {
        throw std::runtime_error("Wire container save entry count exceeds the chunk block count");
    }

    writeVaruint(out, static_cast<uint32_t>(wireContainers.size()));

    std::vector<glm::ivec3> writtenPositions;
    writtenPositions.reserve(wireContainers.size());

    for (const WireContainerSaveEntry& entry : wireContainers) {
        if (entry.parts.empty()) {
            throw std::runtime_error("Wire container save entry has no parts");
        }
        if (containsPosition(writtenPositions, entry.position)) {
            throw std::runtime_error("Wire container save entries contain a duplicate position");
        }

        const int localX = checkedLocalX(chunk, entry.position);
        const int localY = checkedLocalY(entry.position);
        const int localZ = checkedLocalZ(chunk, entry.position);
        if (!isWireContainerState(chunk.getBlock(localX, localY, localZ))) {
            throw std::runtime_error("Wire container save entry points at a non-container block");
        }

        writeU8(out, static_cast<uint8_t>(localX));
        writeU16(out, static_cast<uint16_t>(localY));
        writeU8(out, static_cast<uint8_t>(localZ));
        writeVaruint(out, static_cast<uint32_t>(entry.parts.size()));
        entry.parts.forEach([&](const WirePart& part) {
            writeU16(out, part.channelId);
            writeU16(out, part.facing);
            writeU8(out, part.power);
            writeU8(out, part.connections);
        });

        writtenPositions.push_back(entry.position);
    }

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                if (!isWireContainerState(chunk.getBlock(x, y, z))) {
                    continue;
                }
                const glm::ivec3 worldPosition(chunk.m_chunkX * Chunk::SIZE_X + x,
                                               y,
                                               chunk.m_chunkZ * Chunk::SIZE_Z + z);
                if (!containsPosition(writtenPositions, worldPosition)) {
                    throw std::runtime_error("Wire container block is missing its saved parts");
                }
            }
        }
    }
}

bool deserializeWireContainers(const uint8_t*& cursor,
                               const uint8_t* end,
                               const Chunk& chunk,
                               std::vector<WireContainerSaveEntry>& out) {
    uint32_t entryCount = 0;
    if (!readVaruint(cursor, end, entryCount)) return false;
    if (entryCount > static_cast<uint32_t>(Chunk::SIZE_X * Chunk::SIZE_Y * Chunk::SIZE_Z)) {
        return false;
    }

    out.clear();
    out.reserve(entryCount);
    std::vector<glm::ivec3> readPositions;
    readPositions.reserve(entryCount);

    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        uint8_t localX = 0;
        uint16_t localY = 0;
        uint8_t localZ = 0;
        if (!readU8(cursor, end, localX) ||
            !readU16(cursor, end, localY) ||
            !readU8(cursor, end, localZ)) {
            return false;
        }
        if (localX >= Chunk::SIZE_X || localY >= Chunk::SIZE_Y || localZ >= Chunk::SIZE_Z) {
            return false;
        }
        if (!isWireContainerState(chunk.getBlock(localX, localY, localZ))) {
            return false;
        }

        const glm::ivec3 worldPosition(chunk.m_chunkX * Chunk::SIZE_X + static_cast<int>(localX),
                                       static_cast<int>(localY),
                                       chunk.m_chunkZ * Chunk::SIZE_Z + static_cast<int>(localZ));
        if (containsPosition(readPositions, worldPosition)) {
            return false;
        }

        uint32_t partCount = 0;
        if (!readVaruint(cursor, end, partCount)) return false;
        if (partCount == 0 || partCount > WireContainerParts::MAX_PARTS) {
            return false;
        }

        WireContainerParts parts;
        for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex) {
            WirePart part;
            if (!readU16(cursor, end, part.channelId) ||
                !readU16(cursor, end, part.facing) ||
                !readU8(cursor, end, part.power) ||
                !readU8(cursor, end, part.connections)) {
                return false;
            }
            try {
                if (!parts.addPart(part)) {
                    return false;
                }
            } catch (const std::exception&) {
                return false;
            }
        }

        WireContainerSaveEntry entry;
        entry.position = worldPosition;
        entry.parts = parts;
        out.push_back(entry);
        readPositions.push_back(worldPosition);
    }

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                if (!isWireContainerState(chunk.getBlock(x, y, z))) {
                    continue;
                }
                const glm::ivec3 worldPosition(chunk.m_chunkX * Chunk::SIZE_X + x,
                                               y,
                                               chunk.m_chunkZ * Chunk::SIZE_Z + z);
                if (!containsPosition(readPositions, worldPosition)) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ChunkSerializer public methods
// ---------------------------------------------------------------------------

std::vector<uint8_t> ChunkSerializer::serializePayload(const Chunk& chunk) {
    static const std::vector<WireContainerSaveEntry> kNoWireContainers;
    return serializePayload(chunk, kNoWireContainers);
}

std::vector<uint8_t> ChunkSerializer::serializePayload(
    const Chunk& chunk,
    const std::vector<WireContainerSaveEntry>& wireContainers) {
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

    serializeWireContainers(payload, chunk, wireContainers);

    return payload;
}

std::shared_ptr<Chunk> ChunkSerializer::deserializePayload(
    int32_t cx, int32_t cz,
    const uint8_t* data, size_t size)
{
    ChunkLoadData loadData = deserializePayloadData(cx, cz, data, size);
    return std::move(loadData.chunk);
}

ChunkLoadData ChunkSerializer::deserializePayloadData(
    int32_t cx, int32_t cz,
    const uint8_t* data, size_t size)
{
    ChunkLoadData loadData;
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;

    uint8_t encoding = 0;
    if (!readU8(cursor, end, encoding)) return loadData;
    if (encoding != MCHK_ENCODING_PALLETIZED) return loadData;

    uint16_t subChunkMask = 0;
    if (!readU16(cursor, end, subChunkMask)) return loadData;

    auto chunk = std::make_shared<Chunk>(cx, cz);

    for (int scy = 0; scy < Chunk::NUM_SUB_CHUNKS; ++scy) {
        if ((subChunkMask & (1u << scy)) == 0) continue;

        uint8_t storedScy = 0;
        if (!readU8(cursor, end, storedScy)) return loadData;
        if (storedScy != static_cast<uint8_t>(scy)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Subchunk Y mismatch: expected %d, got %d\n",
                         scy, storedScy);
            return loadData;
        }

        SubChunk* sub = chunk->getOrCreateSubChunk(scy);
        if (!sub) return loadData;

        if (!deserializeLayer(cursor, end, *sub, false)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to deserialize block layer for subchunk %d\n", scy);
            return loadData;
        }
        if (!deserializeLayer(cursor, end, *sub, true)) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to deserialize fluid layer for subchunk %d\n", scy);
            return loadData;
        }

        sub->inferType();
    }

    if (!deserializeWireContainers(cursor, end, *chunk, loadData.wireContainers)) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to deserialize wire container parts\n");
        loadData.wireContainers.clear();
        return loadData;
    }
    if (cursor != end) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unexpected bytes at end of chunk payload\n");
        loadData.wireContainers.clear();
        return loadData;
    }

    loadData.chunk = std::move(chunk);
    return loadData;
}

std::vector<uint8_t> ChunkSerializer::serializeFile(const Chunk& chunk) {
    static const std::vector<WireContainerSaveEntry> kNoWireContainers;
    return serializeFile(chunk, kNoWireContainers);
}

std::vector<uint8_t> ChunkSerializer::serializeFile(
    const Chunk& chunk,
    const std::vector<WireContainerSaveEntry>& wireContainers) {
    std::vector<uint8_t> payload = serializePayload(chunk, wireContainers);

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
    ChunkLoadData loadData = deserializeFileData(data, size);
    return std::move(loadData.chunk);
}

ChunkLoadData ChunkSerializer::deserializeFileData(
    const uint8_t* data, size_t size)
{
    ChunkLoadData loadData;
    if (size < sizeof(MchkHeader)) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK file too small (%zu bytes)\n", size);
        return loadData;
    }

    MchkHeader header;
    std::memcpy(&header, data, sizeof(MchkHeader));

    if (header.magic != MCHK_MAGIC) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid MCHK magic: 0x%08X\n", header.magic);
        return loadData;
    }

    if (header.version != MCHK_VERSION) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported MCHK version: %u\n", header.version);
        return loadData;
    }

    if (size < sizeof(MchkHeader) + header.payloadSize) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK file truncated: expected %zu, got %zu\n",
                     sizeof(MchkHeader) + header.payloadSize, size);
        return loadData;
    }

    const uint8_t* payload = data + sizeof(MchkHeader);
    const uint32_t computedCrc = detail::crc32(payload, header.payloadSize);
    if (computedCrc != header.payloadCrc32) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] MCHK CRC mismatch: expected 0x%08X, got 0x%08X\n",
                     header.payloadCrc32, computedCrc);
        return loadData;
    }

    return deserializePayloadData(header.chunkX, header.chunkZ, payload, header.payloadSize);
}

} // namespace save
