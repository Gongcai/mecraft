#include "LightEngine.h"

#include <algorithm>
#include <cmath>

#include "Block.h"
#include "Chunk.h"
#include "World.h"

namespace {
int worldToChunkCoord(const int world, const int chunkSize) {
    return static_cast<int>(std::floor(static_cast<float>(world) / static_cast<float>(chunkSize)));
}

constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};
} // namespace

LightEngine::LightEngine(World& world) : m_world(world) {}

// ========================================================================
// Full initialization
// ========================================================================

void LightEngine::onChunkLoaded(Chunk& chunk) {
    // Step 1: Build height map
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            chunk.recalcHeightMap(x, z);
        }
    }

    // Step 2: Sky light column scan
    initSkyLight(chunk);

    // Step 3: Block light from light sources
    initBlockLight(chunk);

    chunk.markDirty();
}

void LightEngine::propagateBorderInto(Chunk& from, Chunk& into, int direction) {
    // Collect light seeds from the border face of 'from' and propagate them
    // into 'into'. This is much cheaper than re-initializing all light in 'into'.
    //
    // direction: 0=+X, 1=-X, 2=+Z, 3=-Z (which neighbor slot 'into' is relative to 'from')

    std::vector<LightNode> skySeeds;
    std::vector<LightNode> blockSeeds;

    // Determine the border slice of 'from' that faces 'into'
    const int fromBaseX = from.m_chunkX * Chunk::SIZE_X;
    const int fromBaseZ = from.m_chunkZ * Chunk::SIZE_Z;

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        if (direction == 0 || direction == 1) {
            // +X or -X border: iterate over z
            const int lx = (direction == 0) ? (Chunk::SIZE_X - 1) : 0;
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                const int wx = fromBaseX + lx;
                const int wz = fromBaseZ + z;

                const uint8_t sky = from.getSunlight(lx, y, z);
                if (sky > 1 && BlockRegistry::get(from.getBlock(lx, y, z)).opacity < 15) {
                    // Check if the adjacent voxel in 'into' would benefit
                    const int targetWx = (direction == 0) ? (wx + 1) : (wx - 1);
                    const uint8_t propagated = (sky > 1) ? static_cast<uint8_t>(sky - 1) : 0;
                    if (propagated > 0 && propagated > getSkyLight(targetWx, y, wz)) {
                        skySeeds.push_back({wx, y, wz, sky});
                    }
                }

                const uint8_t bl = from.getBlockLight(lx, y, z);
                if (bl > 1 && BlockRegistry::get(from.getBlock(lx, y, z)).opacity < 15) {
                    const int targetWx = (direction == 0) ? (wx + 1) : (wx - 1);
                    const uint8_t propagated = static_cast<uint8_t>(bl - 1);
                    if (propagated > getBlockLightAt(targetWx, y, wz)) {
                        blockSeeds.push_back({wx, y, wz, bl});
                    }
                }
            }
        } else {
            // +Z or -Z border: iterate over x
            const int lz = (direction == 2) ? (Chunk::SIZE_Z - 1) : 0;
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const int wx = fromBaseX + x;
                const int wz = fromBaseZ + lz;

                const uint8_t sky = from.getSunlight(x, y, lz);
                if (sky > 1 && BlockRegistry::get(from.getBlock(x, y, lz)).opacity < 15) {
                    const int targetWz = (direction == 2) ? (wz + 1) : (wz - 1);
                    const uint8_t propagated = (sky > 1) ? static_cast<uint8_t>(sky - 1) : 0;
                    if (propagated > 0 && propagated > getSkyLight(wx, y, targetWz)) {
                        skySeeds.push_back({wx, y, wz, sky});
                    }
                }

                const uint8_t bl = from.getBlockLight(x, y, lz);
                if (bl > 1 && BlockRegistry::get(from.getBlock(x, y, lz)).opacity < 15) {
                    const int targetWz = (direction == 2) ? (wz + 1) : (wz - 1);
                    const uint8_t propagated = static_cast<uint8_t>(bl - 1);
                    if (propagated > getBlockLightAt(wx, y, targetWz)) {
                        blockSeeds.push_back({wx, y, wz, bl});
                    }
                }
            }
        }
    }

    if (!skySeeds.empty()) {
        spreadSkyLight(skySeeds);
    }
    if (!blockSeeds.empty()) {
        spreadBlockLight(blockSeeds);
    }

    if (!skySeeds.empty() || !blockSeeds.empty()) {
        into.markDirty();
    }
}

// ========================================================================
// Sky light
// ========================================================================

void LightEngine::initSkyLight(Chunk& chunk) {
    // Column scan: from top to bottom
    // Track sky light level and attenuate through semi-transparent blocks
    for (int z = 0; z < Chunk::SIZE_Z; ++z) {
        for (int x = 0; x < Chunk::SIZE_X; ++x) {
            uint8_t skyLevel = 15;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
                const uint8_t blockOpacity = BlockRegistry::get(chunk.getBlock(x, y, z)).opacity;
                if (blockOpacity >= 15) {
                    // Fully opaque block: kills all light
                    chunk.setSunlight(x, y, z, 0);
                    skyLevel = 0;
                } else if (skyLevel > 0) {
                    // Attenuate by block opacity (downward propagation)
                    skyLevel = (skyLevel > blockOpacity)
                        ? static_cast<uint8_t>(skyLevel - blockOpacity) : 0;
                    chunk.setSunlight(x, y, z, skyLevel);
                } else {
                    chunk.setSunlight(x, y, z, 0);
                }
            }
        }
    }

    // BFS: lateral spread for overhangs and cave entrances
    propagateSkyLight(chunk);
}

void LightEngine::propagateSkyLight(Chunk& chunk) {
    std::vector<LightNode> seeds;

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const uint8_t sky = chunk.getSunlight(x, y, z);
                if (sky == 0) continue;
                if (BlockRegistry::get(chunk.getBlock(x, y, z)).opacity >= 15) continue;

                const int wx = chunk.m_chunkX * Chunk::SIZE_X + x;
                const int wz = chunk.m_chunkZ * Chunk::SIZE_Z + z;

                bool needsSpread = false;
                for (int d = 0; d < 6; ++d) {
                    const int nx = x + DX[d];
                    const int ny = y + DY[d];
                    const int nz = z + DZ[d];

                    const uint8_t propagated = (DY[d] == -1) ? sky
                                        : (sky > 1) ? static_cast<uint8_t>(sky - 1) : 0;

                    if (propagated == 0) continue;

                    uint8_t neighborSky;
                    if (nx >= 0 && nx < Chunk::SIZE_X &&
                        nz >= 0 && nz < Chunk::SIZE_Z &&
                        ny >= 0 && ny < Chunk::SIZE_Y) {
                        neighborSky = chunk.getSunlight(nx, ny, nz);
                    } else {
                        neighborSky = getSkyLight(wx + DX[d], ny, wz + DZ[d]);
                    }

                    if (propagated > neighborSky) {
                        needsSpread = true;
                        break;
                    }
                }

                if (needsSpread) {
                    seeds.push_back({wx, y, wz, sky});
                }
            }
        }
    }

    spreadSkyLight(seeds);
}

void LightEngine::spreadSkyLight(const std::vector<LightNode>& seeds) {
    for (const auto& s : seeds) {
        m_skySpreadQueue.push_back(s);
    }
}

void LightEngine::removeSkyLight(int wx, int wy, int wz) {
    const uint8_t oldLevel = getSkyLight(wx, wy, wz);
    if (oldLevel == 0) return;

    setSkyLight(wx, wy, wz, 0);
    m_skyRemoveQueue.push_back({wx, wy, wz, oldLevel});
}

// ========================================================================
// Block light
// ========================================================================

void LightEngine::initBlockLight(Chunk& chunk) {
    std::vector<LightNode> seeds;

    for (int y = 0; y < Chunk::SIZE_Y; ++y) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                const BlockID id = chunk.getBlock(x, y, z);
                const BlockDef& def = BlockRegistry::get(id);
                if (!def.isLightSource) continue;

                const uint8_t level = def.lightLevel;
                chunk.setBlockLight(x, y, z, level);

                const int wx = chunk.m_chunkX * Chunk::SIZE_X + x;
                const int wz = chunk.m_chunkZ * Chunk::SIZE_Z + z;
                seeds.push_back({wx, y, wz, level});
            }
        }
    }

    spreadBlockLight(seeds);
}

void LightEngine::spreadBlockLight(const std::vector<LightNode>& seeds) {
    for (const auto& s : seeds) {
        m_blockSpreadQueue.push_back(s);
    }
}

void LightEngine::removeBlockLight(int wx, int wy, int wz) {
    const uint8_t oldLevel = getBlockLightAt(wx, wy, wz);
    if (oldLevel == 0) return;

    setBlockLightAt(wx, wy, wz, 0);
    m_blockRemoveQueue.push_back({wx, wy, wz, oldLevel});
}

// ========================================================================
// Tick Scheduler
// ========================================================================

void LightEngine::tick(int budget) {
    int processed = 0;

    // 1. Sky Light Remove Phase
    while (!m_skyRemoveQueue.empty() && processed < budget) {
        const LightNode cur = m_skyRemoveQueue.front();
        m_skyRemoveQueue.pop_front();
        ++processed;

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;
            if (isOpaque(nx, ny, nz)) continue;

            const uint8_t neighborLevel = getSkyLight(nx, ny, nz);
            if (neighborLevel == 0) continue;

            const uint8_t provided = (DY[d] == -1) ? cur.level
                                    : (cur.level > 1) ? static_cast<uint8_t>(cur.level - 1) : 0;

            if (neighborLevel <= provided) {
                setSkyLight(nx, ny, nz, 0);
                markChunkDirtyAt(nx, nz);
                m_skyRemoveQueue.push_back({nx, ny, nz, neighborLevel});
            } else {
                m_skySpreadQueue.push_back({nx, ny, nz, neighborLevel});
            }
        }
    }

    // 2. Block Light Remove Phase
    while (!m_blockRemoveQueue.empty() && processed < budget) {
        const LightNode cur = m_blockRemoveQueue.front();
        m_blockRemoveQueue.pop_front();
        ++processed;

        for (int d = 0; d < 6; ++d) {
            const int nx = cur.x + DX[d];
            const int ny = cur.y + DY[d];
            const int nz = cur.z + DZ[d];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;
            if (isOpaque(nx, ny, nz)) continue;

            const uint8_t neighborLevel = getBlockLightAt(nx, ny, nz);
            if (neighborLevel == 0) continue;

            const uint8_t provided = (cur.level > 1) ? static_cast<uint8_t>(cur.level - 1) : 0;

            if (neighborLevel <= provided) {
                setBlockLightAt(nx, ny, nz, 0);
                markChunkDirtyAt(nx, nz);
                m_blockRemoveQueue.push_back({nx, ny, nz, neighborLevel});
            } else {
                m_blockSpreadQueue.push_back({nx, ny, nz, neighborLevel});
            }
        }
    }

    // 3. Sky Light Spread Phase
    if (m_skyRemoveQueue.empty()) {
        while (!m_skySpreadQueue.empty() && processed < budget) {
            const LightNode cur = m_skySpreadQueue.front();
            m_skySpreadQueue.pop_front();
            ++processed;

            const uint8_t curSky = getSkyLight(cur.x, cur.y, cur.z);
            if (curSky == 0) continue;

            for (int d = 0; d < 6; ++d) {
                const int nx = cur.x + DX[d];
                const int ny = cur.y + DY[d];
                const int nz = cur.z + DZ[d];

                if (ny < 0 || ny >= Chunk::SIZE_Y) continue;
                if (isOpaque(nx, ny, nz)) continue;

                const uint8_t opacity = getOpacity(nx, ny, nz);
                uint8_t propagated;
                if (DY[d] == -1) {
                    propagated = (curSky > opacity) ? static_cast<uint8_t>(curSky - opacity) : 0;
                } else {
                    const uint8_t attenuation = std::max<uint8_t>(1, opacity);
                    propagated = (curSky > attenuation) ? static_cast<uint8_t>(curSky - attenuation) : 0;
                }
                if (propagated == 0) continue;

                const uint8_t neighborSky = getSkyLight(nx, ny, nz);
                if (propagated > neighborSky) {
                    setSkyLight(nx, ny, nz, propagated);
                    markChunkDirtyAt(nx, nz);
                    m_skySpreadQueue.push_back({nx, ny, nz, propagated});
                }
            }
        }
    }

    // 4. Block Light Spread Phase
    if (m_blockRemoveQueue.empty()) {
        while (!m_blockSpreadQueue.empty() && processed < budget) {
            const LightNode cur = m_blockSpreadQueue.front();
            m_blockSpreadQueue.pop_front();
            ++processed;

            const uint8_t curBlock = getBlockLightAt(cur.x, cur.y, cur.z);
            if (curBlock == 0) continue;

            for (int d = 0; d < 6; ++d) {
                const int nx = cur.x + DX[d];
                const int ny = cur.y + DY[d];
                const int nz = cur.z + DZ[d];

                if (ny < 0 || ny >= Chunk::SIZE_Y) continue;
                if (isOpaque(nx, ny, nz)) continue;

                const uint8_t opacity = getOpacity(nx, ny, nz);
                const uint8_t attenuation = std::max<uint8_t>(1, opacity);
                const uint8_t propagated = (curBlock > attenuation)
                                         ? static_cast<uint8_t>(curBlock - attenuation) : 0;
                if (propagated == 0) continue;

                const uint8_t neighborBlock = getBlockLightAt(nx, ny, nz);
                if (propagated > neighborBlock) {
                    setBlockLightAt(nx, ny, nz, propagated);
                    markChunkDirtyAt(nx, nz);
                    m_blockSpreadQueue.push_back({nx, ny, nz, propagated});
                }
            }
        }
    }

    if (m_skyRemoveQueue.empty() && m_skySpreadQueue.empty() && 
        m_blockRemoveQueue.empty() && m_blockSpreadQueue.empty()) {
        const auto& chunks = m_world.getActiveChunks();
        for (int64_t key : m_dirtyChunks) {
            auto it = chunks.find(key);
            if (it != chunks.end()) {
                it->second->markDirty();
            }
        }
        m_dirtyChunks.clear();
    }
}

// ========================================================================
// Incremental update on block change
// ========================================================================

void LightEngine::onBlockChanged(int wx, int wy, int wz,
                                  uint8_t oldBlockId, uint8_t newBlockId) {
    const BlockDef& oldDef = BlockRegistry::get(oldBlockId);
    const BlockDef& newDef = BlockRegistry::get(newBlockId);

    const bool wasLightBlocking = (oldDef.opacity >= 15);
    const bool isLightBlocking  = (newDef.opacity >= 15);
    const bool wasLight = oldDef.isLightSource;
    const bool isLight  = newDef.isLightSource;

    // Update height map
    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);
    const int lx = wx - cx * Chunk::SIZE_X;
    const int lz = wz - cz * Chunk::SIZE_Z;
    auto it = m_world.getActiveChunks().find(World::chunkKey(cx, cz));
    if (it != m_world.getActiveChunks().end()) {
        it->second->recalcHeightMap(lx, lz);
    }

    // === Sky light ===

    if (wasLightBlocking && !isLightBlocking) {
        // Broke an opaque block — sky light and block light may enter

        // --- Sky light ---
        uint8_t aboveSky = getSkyLight(wx, wy + 1, wz);
        if (wy + 1 >= Chunk::SIZE_Y) aboveSky = 15;
        setSkyLight(wx, wy, wz, aboveSky);

        std::vector<LightNode> skySeeds;

        // Column: propagate downward without attenuation
        for (int y = wy - 1; y >= 0; --y) {
            if (isOpaque(wx, y, wz)) break;
            if (getSkyLight(wx, y, wz) < aboveSky) {
                setSkyLight(wx, y, wz, aboveSky);
                skySeeds.push_back({wx, y, wz, aboveSky});
            }
        }

        // Lateral: from current position
        if (aboveSky > 1) {
            skySeeds.push_back({wx, wy, wz, aboveSky});
        }

        // Pull sky light from all 6 neighbors (critical for lateral propagation into caves)
        for (int d = 0; d < 6; ++d) {
            const int nx = wx + DX[d];
            const int ny = wy + DY[d];
            const int nz = wz + DZ[d];
            if (ny >= 0 && ny < Chunk::SIZE_Y) {
                const uint8_t ns = getSkyLight(nx, ny, nz);
                if (ns > 0) {
                    skySeeds.push_back({nx, ny, nz, ns});
                }
            }
        }

        spreadSkyLight(skySeeds);

        // --- Block light: re-propagate from neighbors ---
        // The new AIR block can now receive block light from adjacent sources
        std::vector<LightNode> blockSeeds;
        for (int d = 0; d < 6; ++d) {
            const int nx = wx + DX[d];
            const int ny = wy + DY[d];
            const int nz = wz + DZ[d];
            const uint8_t nb = getBlockLightAt(nx, ny, nz);
            if (nb > 1) {
                blockSeeds.push_back({nx, ny, nz, nb});
            }
        }
        spreadBlockLight(blockSeeds);
    } else if (!wasLightBlocking && isLightBlocking) {
        // Placed an opaque block — both sky light and block light blocked
        removeSkyLight(wx, wy, wz);

        // Reseed sky light from neighbors that still have light
        std::vector<LightNode> skySeeds;
        for (int d = 0; d < 6; ++d) {
            const int nx = wx + DX[d];
            const int ny = wy + DY[d];
            const int nz = wz + DZ[d];
            const uint8_t ns = getSkyLight(nx, ny, nz);
            if (ns > 0) {
                skySeeds.push_back({nx, ny, nz, ns});
            }
        }
        spreadSkyLight(skySeeds);

        // Block light also blocked — remove and reseed
        removeBlockLight(wx, wy, wz);

        std::vector<LightNode> blockSeeds;
        for (int d = 0; d < 6; ++d) {
            const int nx = wx + DX[d];
            const int ny = wy + DY[d];
            const int nz = wz + DZ[d];
            const uint8_t nb = getBlockLightAt(nx, ny, nz);
            if (nb > 1) {
                blockSeeds.push_back({nx, ny, nz, nb});
            }
        }
        spreadBlockLight(blockSeeds);
    }

    // === Block light source changes ===

    if (wasLight && !isLight) {
        // Removed a light source
        removeBlockLight(wx, wy, wz);

        std::vector<LightNode> blockSeeds;
        for (int d = 0; d < 6; ++d) {
            const int nx = wx + DX[d];
            const int ny = wy + DY[d];
            const int nz = wz + DZ[d];
            const uint8_t nb = getBlockLightAt(nx, ny, nz);
            if (nb > 1) {
                blockSeeds.push_back({nx, ny, nz, nb});
            }
        }
        spreadBlockLight(blockSeeds);
    } else if (isLight && !wasLight) {
        // Placed a new light source
        const uint8_t level = newDef.lightLevel;
        setBlockLightAt(wx, wy, wz, level);
        std::vector<LightNode> blockSeeds = {{wx, wy, wz, level}};
        spreadBlockLight(blockSeeds);
    } else if (isLight && wasLight && newDef.lightLevel != oldDef.lightLevel) {
        // Replaced one light source with another of different intensity
        removeBlockLight(wx, wy, wz);
        const uint8_t level = newDef.lightLevel;
        setBlockLightAt(wx, wy, wz, level);
        std::vector<LightNode> blockSeeds = {{wx, wy, wz, level}};
        spreadBlockLight(blockSeeds);
    }

    markChunkDirtyAt(wx, wz);
}

void LightEngine::markNeighborDirty(int chunkX, int chunkZ) {
    m_dirtyChunks.insert(World::chunkKey(chunkX, chunkZ));
}

// ========================================================================
// World-coordinate accessors
// ========================================================================

uint8_t LightEngine::getSkyLight(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return 0;

    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);
    const int lx = wx - cx * Chunk::SIZE_X;
    const int lz = wz - cz * Chunk::SIZE_Z;

    const auto& chunks = m_world.getActiveChunks();
    const auto it = chunks.find(World::chunkKey(cx, cz));
    if (it != chunks.end()) {
        return it->second->getSunlight(lx, wy, lz);
    }
    return 0;
}

uint8_t LightEngine::getBlockLightAt(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return 0;

    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);
    const int lx = wx - cx * Chunk::SIZE_X;
    const int lz = wz - cz * Chunk::SIZE_Z;

    const auto& chunks = m_world.getActiveChunks();
    const auto it = chunks.find(World::chunkKey(cx, cz));
    if (it != chunks.end()) {
        return it->second->getBlockLight(lx, wy, lz);
    }
    return 0;
}

void LightEngine::setSkyLight(int wx, int wy, int wz, uint8_t val) {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return;

    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);
    const int lx = wx - cx * Chunk::SIZE_X;
    const int lz = wz - cz * Chunk::SIZE_Z;

    const auto& chunks = m_world.getActiveChunks();
    const auto it = chunks.find(World::chunkKey(cx, cz));
    if (it != chunks.end()) {
        it->second->setSunlight(lx, wy, lz, val);
    }
}

void LightEngine::setBlockLightAt(int wx, int wy, int wz, uint8_t val) {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return;

    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);
    const int lx = wx - cx * Chunk::SIZE_X;
    const int lz = wz - cz * Chunk::SIZE_Z;

    const auto& chunks = m_world.getActiveChunks();
    const auto it = chunks.find(World::chunkKey(cx, cz));
    if (it != chunks.end()) {
        it->second->setBlockLight(lx, wy, lz, val);
    }
}

bool LightEngine::isOpaque(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return true;

    const BlockID id = m_world.getBlock(wx, wy, wz);
    return BlockRegistry::get(id).opacity >= 15;
}

uint8_t LightEngine::getOpacity(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= Chunk::SIZE_Y) return 15;

    const BlockID id = m_world.getBlock(wx, wy, wz);
    return BlockRegistry::get(id).opacity;
}

void LightEngine::markChunkDirtyAt(int wx, int wz) {
    const int cx = worldToChunkCoord(wx, Chunk::SIZE_X);
    const int cz = worldToChunkCoord(wz, Chunk::SIZE_Z);

    m_dirtyChunks.insert(World::chunkKey(cx, cz));
}
