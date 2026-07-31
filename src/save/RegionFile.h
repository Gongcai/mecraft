#ifndef MECRAFT_REGION_FILE_H
#define MECRAFT_REGION_FILE_H

// RegionFile: manages a 32x32 grid of chunks in a single .mcrg file.
//
// File layout:
//   RegionHeader (fixed, 12 bytes)
//   ChunkIndex[1024] (fixed, 1024 * 16 = 16384 bytes)
//   Chunk data (variable size, appended)
//
// RegionHeader:
//   uint32_t magic (MCRG_MAGIC = 0x4D435247 = 'MCRG')
//   uint16_t version (1)
//   uint16_t flags (reserved)
//   int32_t regionX
//   int32_t regionZ
//
// ChunkIndex entry (16 bytes each):
//   uint32_t offset (byte offset from file start, 0 = empty)
//   uint32_t size (byte count of chunk data, 0 = empty)
//   uint32_t crc32
//   uint32_t timestamp (unix epoch seconds)

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>
#include <array>

#include "ChunkSerializer.h"

class Chunk;

namespace save {

constexpr uint32_t MCRG_MAGIC = 0x4D435247u; // 'MCRG'
constexpr uint16_t MCRG_VERSION = 1;
constexpr int REGION_SIZE = 32; // 32x32 chunks per region
constexpr int CHUNKS_PER_REGION = REGION_SIZE * REGION_SIZE; // 1024

#pragma pack(push, 1)
struct RegionHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    int32_t regionX;
    int32_t regionZ;
};

struct ChunkIndexEntry {
    uint32_t offset = 0; // 0 means chunk not stored
    uint32_t size = 0;
    uint32_t crc32 = 0;
    uint32_t timestamp = 0;
};
#pragma pack(pop)

static_assert(sizeof(RegionHeader) == 16, "RegionHeader must be 16 bytes");
static_assert(sizeof(ChunkIndexEntry) == 16, "ChunkIndexEntry must be 16 bytes");

class RegionFile {
public:
    // Convert chunk coords to region coords
    [[nodiscard]] static int toRegionCoord(int chunkCoord);
    [[nodiscard]] static int toLocalCoord(int chunkCoord);

    // Region file path: <chunksDir>/r.<rx>.<rz>.mcrg
    [[nodiscard]] static std::filesystem::path regionPath(const std::filesystem::path& chunksDir, int rx, int rz);

    // Open or create a region file. Returns nullptr on error.
    [[nodiscard]] static std::unique_ptr<RegionFile> open(const std::filesystem::path& path, int rx, int rz);

    // Read a chunk from the region. Returns nullptr if not stored.
    [[nodiscard]] std::shared_ptr<Chunk> readChunk(int cx, int cz);
    [[nodiscard]] ChunkLoadData readChunkWithData(int cx, int cz);

    // Write a chunk to the region. Appends to end of file.
    bool writeChunk(int cx, int cz, const Chunk& chunk);

    // Write raw chunk data to the region. Appends to end of file.
    bool writeChunkRaw(int cx, int cz, const std::vector<uint8_t>& data);

    // Check if a chunk is stored in this region.
    [[nodiscard]] bool hasChunk(int cx, int cz) const;

    // Get the region coordinates.
    [[nodiscard]] int regionX() const { return m_header.regionX; }
    [[nodiscard]] int regionZ() const { return m_header.regionZ; }

private:
    RegionFile() = default;

    // Read the chunk data at the given index entry. The caller must hold m_ioMutex.
    [[nodiscard]] std::vector<uint8_t> readChunkData(int localX, int localZ) const;

    // Write chunk data and update index
    bool writeChunkData(int localX, int localZ, const std::vector<uint8_t>& data);

    std::filesystem::path m_path;
    RegionHeader m_header{};
    std::array<ChunkIndexEntry, CHUNKS_PER_REGION> m_index{};
    mutable std::mutex m_ioMutex;
    bool m_dirty = false;
};

} // namespace save

#endif // MECRAFT_REGION_FILE_H
