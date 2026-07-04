#include "RegionFile.h"
#include "../Diagnostics.h"
#include "ChunkSerializer.h"
#include "SaveFormat.h"
#include "../world/chunk/Chunk.h"

#include <fstream>
#include <cstring>
#include <cstdio>

namespace save {

int RegionFile::toRegionCoord(int chunkCoord) {
    return chunkCoord >> 5; // floor(chunkCoord / 32)
}

int RegionFile::toLocalCoord(int chunkCoord) {
    return chunkCoord & 31; // chunkCoord % 32, works for negatives
}

std::filesystem::path RegionFile::regionPath(
    const std::filesystem::path& chunksDir, int rx, int rz)
{
    return chunksDir / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mcrg");
}

std::unique_ptr<RegionFile> RegionFile::open(
    const std::filesystem::path& path, int rx, int rz)
{
    auto rf = std::unique_ptr<RegionFile>(new RegionFile());
    rf->m_path = path;

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        // Open existing file
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to open region file %s\n", path.string().c_str());
            return nullptr;
        }

        // Read header
        file.read(reinterpret_cast<char*>(&rf->m_header), sizeof(RegionHeader));
        if (!file) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to read region header\n");
            return nullptr;
        }

        if (rf->m_header.magic != MCRG_MAGIC) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Invalid region magic: 0x%08X\n", rf->m_header.magic);
            return nullptr;
        }

        if (rf->m_header.version != MCRG_VERSION) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Unsupported region version: %u\n", rf->m_header.version);
            return nullptr;
        }

        // Read chunk index
        file.read(reinterpret_cast<char*>(rf->m_index.data()),
                  sizeof(ChunkIndexEntry) * CHUNKS_PER_REGION);
        if (!file) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to read chunk index\n");
            return nullptr;
        }
    } else {
        // Create new file
        rf->m_header.magic = MCRG_MAGIC;
        rf->m_header.version = MCRG_VERSION;
        rf->m_header.flags = 0;
        rf->m_header.regionX = rx;
        rf->m_header.regionZ = rz;
        rf->m_index.fill(ChunkIndexEntry{});
        rf->m_dirty = true;

        // Write initial file
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to create region file %s\n", path.string().c_str());
            return nullptr;
        }

        file.write(reinterpret_cast<const char*>(&rf->m_header), sizeof(RegionHeader));
        file.write(reinterpret_cast<const char*>(rf->m_index.data()),
                   sizeof(ChunkIndexEntry) * CHUNKS_PER_REGION);
        file.flush();
    }

    return rf;
}

std::shared_ptr<Chunk> RegionFile::readChunk(int cx, int cz) {
    ChunkLoadData data = readChunkWithData(cx, cz);
    return std::move(data.chunk);
}

ChunkLoadData RegionFile::readChunkWithData(int cx, int cz) {
    std::lock_guard<std::mutex> lock(m_ioMutex);

    ChunkLoadData loadData;
    const int lx = toLocalCoord(cx);
    const int lz = toLocalCoord(cz);
    const int index = lz * REGION_SIZE + lx;

    const auto& entry = m_index[index];
    if (entry.offset == 0 || entry.size == 0) {
        return loadData;
    }

    std::vector<uint8_t> data = readChunkData(lx, lz);
    if (data.empty()) return loadData;

    // Validate CRC
    const uint32_t computedCrc = detail::crc32(data.data(), data.size());
    if (computedCrc != entry.crc32) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Region chunk CRC mismatch at (%d, %d)\n", cx, cz);
        return loadData;
    }

    return ChunkSerializer::deserializeFileData(data.data(), data.size());
}

bool RegionFile::writeChunk(int cx, int cz, const Chunk& chunk) {
    const int lx = toLocalCoord(cx);
    const int lz = toLocalCoord(cz);

    std::vector<uint8_t> data = ChunkSerializer::serializeFile(chunk);
    if (data.empty()) {
        MECRAFT_LOG_FPRINTF(stderr, "[Save] Failed to serialize region chunk (%d, %d)\n", cx, cz);
        return false;
    }
    return writeChunkData(lx, lz, data);
}

bool RegionFile::writeChunkRaw(int cx, int cz, const std::vector<uint8_t>& data) {
    const int lx = toLocalCoord(cx);
    const int lz = toLocalCoord(cz);
    return writeChunkData(lx, lz, data);
}

bool RegionFile::hasChunk(int cx, int cz) const {
    std::lock_guard<std::mutex> lock(m_ioMutex);

    const int lx = toLocalCoord(cx);
    const int lz = toLocalCoord(cz);
    const int index = lz * REGION_SIZE + lx;
    return m_index[index].offset != 0 && m_index[index].size != 0;
}

std::vector<uint8_t> RegionFile::readChunkData(int localX, int localZ) const {
    const int index = localZ * REGION_SIZE + localX;
    const auto& entry = m_index[index];

    if (entry.offset == 0 || entry.size == 0) {
        return {};
    }

    std::ifstream file(m_path, std::ios::binary);
    if (!file.is_open()) return {};

    file.seekg(entry.offset);
    std::vector<uint8_t> data(entry.size);
    file.read(reinterpret_cast<char*>(data.data()), entry.size);
    if (!file) return {};

    return data;
}

bool RegionFile::writeChunkData(int localX, int localZ, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_ioMutex);

    const int index = localZ * REGION_SIZE + localX;

    // Append to end of file
    std::ofstream file(m_path, std::ios::binary | std::ios::ate | std::ios::in);
    if (!file.is_open()) {
        // Try creating the file if it doesn't exist
        file.open(m_path, std::ios::binary | std::ios::out);
        if (!file.is_open()) {
            // Write header and index first
            file.write(reinterpret_cast<const char*>(&m_header), sizeof(RegionHeader));
            file.write(reinterpret_cast<const char*>(m_index.data()),
                       sizeof(ChunkIndexEntry) * CHUNKS_PER_REGION);
        }
        file.close();
        file.open(m_path, std::ios::binary | std::ios::ate | std::ios::in);
        if (!file.is_open()) return false;
    }

    const uint32_t offset = static_cast<uint32_t>(file.tellp());
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file) return false;
    file.flush();

    // Update index
    m_index[index].offset = offset;
    m_index[index].size = static_cast<uint32_t>(data.size());
    m_index[index].crc32 = detail::crc32(data.data(), data.size());

    // Write updated index back to file
    file.seekp(sizeof(RegionHeader) + index * sizeof(ChunkIndexEntry));
    file.write(reinterpret_cast<const char*>(&m_index[index]), sizeof(ChunkIndexEntry));
    file.flush();

    m_dirty = true;
    return true;
}

} // namespace save
