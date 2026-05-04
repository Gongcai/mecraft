#ifndef MECRAFT_LIGHTCACHE_H
#define MECRAFT_LIGHTCACHE_H

#include <cstdint>
#include <vector>

#include "Chunk.h"

struct LightSourceEntry {
    uint16_t packedIndex = 0;
    uint8_t level = 0;
};

struct CachedBaseLight {
    std::vector<uint8_t> packed;
    std::vector<LightSourceEntry> sources;
};

/// Build the full intrinsic base light for a chunk (sky + block light sources,
/// without boundary seeds). Used when a chunk is first loaded.
CachedBaseLight buildBaseLightFromChunk(const Chunk& chunk);

/// Recompute a single (x,z) column of sky light in the cached base.
/// Reads block opacity from the chunk. The column index is packedIndex(x, 0, z).
void recomputeSkyColumn(const Chunk& chunk, int x, int z, std::vector<uint8_t>& packed);

/// Rebuild all block light in the cache from the sparse sources list.
void rebuildBlockLightFromSources(const std::vector<LightSourceEntry>& sources,
                                  std::vector<uint8_t>& packed);

#endif // MECRAFT_LIGHTCACHE_H
