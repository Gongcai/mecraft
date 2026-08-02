#ifndef MECRAFT_TERRAIN_GPU_KEY_H
#define MECRAFT_TERRAIN_GPU_KEY_H

#include <cstddef>
#include <cstdint>

/// Identifies one vertically partitioned terrain mesh inside a chunk column.
struct SubChunkGpuKey {
    int64_t chunkKey = 0;
    int scy = 0;

    [[nodiscard]] bool operator==(const SubChunkGpuKey& other) const {
        return chunkKey == other.chunkKey && scy == other.scy;
    }
};

/// Hashes a terrain GPU key for stable lookup in unordered containers.
struct SubChunkGpuKeyHash {
    [[nodiscard]] size_t operator()(const SubChunkGpuKey& key) const {
        const uint64_t chunk = static_cast<uint64_t>(key.chunkKey);
        const uint64_t mixed =
            chunk ^ (static_cast<uint64_t>(key.scy) + 0x9e3779b97f4a7c15ULL + (chunk << 6u) + (chunk >> 2u));
        return static_cast<size_t>(mixed);
    }
};

#endif // MECRAFT_TERRAIN_GPU_KEY_H
