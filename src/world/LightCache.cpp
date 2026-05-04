#include "LightCache.h"

#include "Block.h"

namespace {

inline std::size_t packedIndex(int x, int y, int z) {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(z) * Chunk::SIZE_X +
           static_cast<std::size_t>(y) * Chunk::SIZE_X * Chunk::SIZE_Z;
}

inline uint8_t getBlock(const std::vector<uint8_t>& packed, std::size_t idx) {
    return static_cast<uint8_t>(packed[idx] & 0x0F);
}

inline void setSky(std::vector<uint8_t>& packed, std::size_t idx, uint8_t value) {
    packed[idx] = static_cast<uint8_t>((packed[idx] & 0x0F) | ((value & 0x0F) << 4));
}

inline void setBlock(std::vector<uint8_t>& packed, std::size_t idx, uint8_t value) {
    packed[idx] = static_cast<uint8_t>((packed[idx] & 0xF0) | (value & 0x0F));
}

} // namespace

CachedBaseLight buildBaseLightFromChunk(const Chunk& chunk) {
    BlockRegistry::ensureInitialized();

    CachedBaseLight cache;
    cache.packed.assign(Chunk::BLOCK_COUNT, 0);
    cache.sources.reserve(256);

    // Sky light: top-down scan for each column.
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            uint8_t skyLevel = 15;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                const BlockID blockId = chunk.getBlock(x, y, z);
                const uint8_t opacity = BlockRegistry::getOpacityFast(blockId);
                if (opacity >= 15) {
                    skyLevel = 0;
                    continue;
                }
                if (skyLevel > 0) {
                    skyLevel = (skyLevel > opacity)
                        ? static_cast<uint8_t>(skyLevel - opacity)
                        : 0;
                }
                if (skyLevel > 0) {
                    const std::size_t idx = packedIndex(x, y, z);
                    setSky(cache.packed, idx, skyLevel);
                }
            }
        }
    }

    // Block light: collect light source positions.
    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID blockId = chunk.getBlock(x, y, z);
                if (!BlockRegistry::isLightSourceFast(blockId)) {
                    continue;
                }
                const uint8_t lightLevel = BlockRegistry::getLightLevelFast(blockId);
                if (lightLevel == 0) {
                    continue;
                }
                const std::size_t idx = packedIndex(x, y, z);
                const uint8_t current = getBlock(cache.packed, idx);
                if (lightLevel > current) {
                    setBlock(cache.packed, idx, lightLevel);
                }
                cache.sources.push_back({static_cast<uint16_t>(idx), lightLevel});
            }
        }
    }

    return cache;
}

void recomputeSkyColumn(const Chunk& chunk, int x, int z, std::vector<uint8_t>& packed) {
    uint8_t skyLevel = 15;
    for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
        const BlockID blockId = chunk.getBlock(x, y, z);
        const uint8_t opacity = BlockRegistry::getOpacityFast(blockId);
        const std::size_t idx = packedIndex(x, y, z);

        if (opacity >= 15) {
            skyLevel = 0;
            setSky(packed, idx, 0);
            continue;
        }
        if (skyLevel > 0) {
            skyLevel = (skyLevel > opacity)
                ? static_cast<uint8_t>(skyLevel - opacity)
                : 0;
        }
        if (skyLevel > 0) {
            setSky(packed, idx, skyLevel);
        } else {
            setSky(packed, idx, 0);
        }
    }
}

void rebuildBlockLightFromSources(const std::vector<LightSourceEntry>& sources,
                                  std::vector<uint8_t>& packed) {
    // Clear only the block-light nibble across the entire volume.
    for (std::size_t i = 0; i < packed.size(); ++i) {
        packed[i] &= 0xF0;
    }

    for (const auto& src : sources) {
        const uint8_t current = static_cast<uint8_t>(packed[src.packedIndex] & 0x0F);
        if (src.level > current) {
            packed[src.packedIndex] = static_cast<uint8_t>(
                (packed[src.packedIndex] & 0xF0) | (src.level & 0x0F));
        }
    }
}

