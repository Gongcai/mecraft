//
// LightEngine.cpp - BFS Light Propagation Implementation
// High-performance lighting system with cross-chunk support
//

#include "LightEngine.h"
#include "World.h"
#include "Chunk.h"
#include "Block.h"
#include <algorithm>
#include <cmath>

constexpr int LightEngine::NEIGHBOR_OFFSETS[6][3];

LightEngine::LightEngine(World* world) : m_world(world) {
    m_increaseQueue.reserve(4096);
    m_decreaseQueue.reserve(4096);
}

// ============================================================================
// Queue Management
// ============================================================================
void LightEngine::clearQueues() {
    m_increaseQueue.clear();
    m_decreaseQueue.clear();
    m_increaseReadIdx = 0;
    m_decreaseReadIdx = 0;
}

void LightEngine::enqueueIncrease(int x, int y, int z, uint8_t level) {
    m_increaseQueue.emplace_back(static_cast<int16_t>(x), static_cast<int16_t>(y),
                                  static_cast<int16_t>(z), level);
}

void LightEngine::enqueueDecrease(int x, int y, int z, uint8_t level) {
    m_decreaseQueue.emplace_back(static_cast<int16_t>(x), static_cast<int16_t>(y),
                                  static_cast<int16_t>(z), level);
}

// ============================================================================
// Coordinate Conversion
// ============================================================================
void LightEngine::worldToLocal(int worldX, int worldY, int worldZ,
                               int& chunkX, int& chunkZ,
                               int& localX, int& localY, int& localZ) {
    // Proper floor division for negative coordinates
    chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::SIZE_X));
    chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::SIZE_Z));

    localX = worldX - chunkX * Chunk::SIZE_X;
    localY = worldY;
    localZ = worldZ - chunkZ * Chunk::SIZE_Z;

    // Wrap to valid range
    if (localX < 0) localX += Chunk::SIZE_X;
    if (localZ < 0) localZ += Chunk::SIZE_Z;
}

// ============================================================================
// Chunk Access
// ============================================================================
Chunk* LightEngine::getChunkAt(int worldX, int worldZ) const {
    int chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::SIZE_X));
    int chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::SIZE_Z));
    return getChunkAtChunkCoord(chunkX, chunkZ);
}

Chunk* LightEngine::getChunkAtChunkCoord(int chunkX, int chunkZ) const {
    int64_t key = (static_cast<int64_t>(chunkX) << 32) | (static_cast<uint32_t>(chunkZ));
    auto& chunks = m_world->getChunksForLightEngine();
    auto it = chunks.find(key);
    if (it != chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ============================================================================
// World-coordinate Light Access
// ============================================================================
uint8_t LightEngine::getLightWorld(int worldX, int worldY, int worldZ, LightType type) const {
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) {
        return 0;
    }

    int chunkX, chunkZ, localX, localY, localZ;
    worldToLocal(worldX, worldY, worldZ, chunkX, chunkZ, localX, localY, localZ);

    Chunk* chunk = getChunkAtChunkCoord(chunkX, chunkZ);
    if (!chunk) {
        // For sky light above heightmap, return full brightness
        if (type == LightType::SKY) {
            int height = chunk ? chunk->getHeightmap(localX, localZ) : Chunk::SIZE_Y - 1;
            if (worldY > height) {
                return 15;
            }
        }
        return 0;
    }

    return type == LightType::BLOCK ?
           chunk->getBlockLight(localX, localY, localZ) :
           chunk->getSunlight(localX, localY, localZ);
}

void LightEngine::setLightWorld(int worldX, int worldY, int worldZ, LightType type, uint8_t level) {
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) {
        return;
    }

    int chunkX, chunkZ, localX, localY, localZ;
    worldToLocal(worldX, worldY, worldZ, chunkX, chunkZ, localX, localY, localZ);

    Chunk* chunk = getChunkAtChunkCoord(chunkX, chunkZ);
    if (!chunk) {
        return;
    }

    if (type == LightType::BLOCK) {
        chunk->setBlockLight(localX, localY, localZ, level);
    } else {
        chunk->setSunlight(localX, localY, localZ, level);
    }
}

uint8_t LightEngine::getOpacityWorld(int worldX, int worldY, int worldZ) const {
    if (worldY < 0 || worldY >= Chunk::SIZE_Y) {
        return 15;  // Out of bounds is opaque
    }

    int chunkX, chunkZ, localX, localY, localZ;
    worldToLocal(worldX, worldY, worldZ, chunkX, chunkZ, localX, localY, localZ);

    Chunk* chunk = getChunkAtChunkCoord(chunkX, chunkZ);
    if (!chunk) {
        return 15;  // Missing chunk is opaque
    }

    uint8_t blockId = chunk->getBlock(localX, localY, localZ);
    return BlockRegistry::get(blockId).opacity;
}

// ============================================================================
// Initialize chunk lighting
// ============================================================================
void LightEngine::initChunkLight(Chunk& chunk) {
    // Rebuild heightmap
    chunk.rebuildHeightmap();

    // Clear lighting
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            for (int y = 0; y < Chunk::SIZE_Y; ++y) {
                chunk.setBlockLight(x, y, z, 0);
                chunk.setSunlight(x, y, z, 0);
            }
        }
    }

    // Initialize sky light using heightmap optimization
    initSkylightWithHeightmap(chunk);

    // Initialize block light
    spreadBlockLight(chunk);

    chunk.markDirty();
}

// ============================================================================
// Sky light with heightmap optimization
// ============================================================================
void LightEngine::initSkylightWithHeightmap(Chunk& chunk) {
    // Step 1: Set all blocks above heightmap to full sky light
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            int height = chunk.getHeightmap(x, z);
            for (int y = height + 1; y < Chunk::SIZE_Y; ++y) {
                chunk.setSunlight(x, y, z, 15);
            }
         }
    }

    // Step 2: Propagate sky light downward from exposed blocks
    propagateSkylightFromAbove(chunk);
}

void LightEngine::propagateSkylightFromAbove(Chunk& chunk) {
    clearQueues();

    // Add all blocks at heightmap level+1 as sources
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int z = 0; z < Chunk::SIZE_Z; ++z) {
            int height = chunk.getHeightmap(x, z);
            if (height + 1 < Chunk::SIZE_Y) {
                int worldX = chunk.m_chunkX * Chunk::SIZE_X + x;
                int worldZ = chunk.m_chunkZ * Chunk::SIZE_Z + z;
                enqueueIncrease(worldX, height + 1, worldZ, 15);
            }
        }
    }

    propagateIncrease(LightType::SKY);
}

void LightEngine::spreadSkylight(Chunk& chunk) {
    initSkylightWithHeightmap(chunk);
}

// ============================================================================
// Block light propagation
// ============================================================================
void LightEngine::spreadBlockLight(Chunk& chunk) {
    clearQueues();

    // Find all light sources in the chunk
    for (int x = 0; x < Chunk::SIZE_X; ++x) {
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                uint8_t blockId = chunk.getBlock(x, y, z);
                const auto& def = BlockRegistry::get(blockId);

                if (def.isLightSource && def.lightLevel > 0) {
                    chunk.setBlockLight(x, y, z, def.lightLevel);
                    int worldX = chunk.m_chunkX * Chunk::SIZE_X + x;
                    int worldZ = chunk.m_chunkZ * Chunk::SIZE_Z + z;
                    enqueueIncrease(worldX, y, worldZ, def.lightLevel);
                }
            }
        }
    }

    propagateIncrease(LightType::BLOCK);
}

// ============================================================================
// BFS propagation: light increase
// ============================================================================
void LightEngine::propagateIncrease(LightType type) {
    while (m_increaseReadIdx < m_increaseQueue.size()) {
        LightNode node = m_increaseQueue[m_increaseReadIdx++];

        // Skip if this node's light level has changed
        uint8_t currentLevel = getLightWorld(node.x, node.y, node.z, type);
        if (currentLevel != node.level && node.level != 15) {
            continue;  // This node was overwritten by a brighter source
        }

        // Iterate 6 neighbors
        for (int i = 0; i < 6; ++i) {
            int nx = node.x + NEIGHBOR_OFFSETS[i][0];
            int ny = node.y + NEIGHBOR_OFFSETS[i][1];
            int nz = node.z + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            // Calculate attenuated light level
            uint8_t opacity = getOpacityWorld(nx, ny, nz);
            uint8_t expected = node.level > opacity ? node.level - opacity : 0;

            // Early exit: if expected <= 1, stop propagation (0 brightness doesn't need to queue)
            if (expected <= 1) continue;

            uint8_t neighborLight = getLightWorld(nx, ny, nz, type);
            if (neighborLight < expected) {
                setLightWorld(nx, ny, nz, type, expected);
                enqueueIncrease(nx, ny, nz, expected);
            }
        }
    }
}

// ============================================================================
// BFS propagation: light decrease
// ============================================================================
void LightEngine::propagateDecrease(LightType type) {
    std::vector<LightNode> relightSources;
    relightSources.reserve(256);

    while (m_decreaseReadIdx < m_decreaseQueue.size()) {
        LightNode node = m_decreaseQueue[m_decreaseReadIdx++];

        uint8_t oldLevel = node.level;

        // Check if this position still has light (might have been re-lit)
        uint8_t currentLevel = getLightWorld(node.x, node.y, node.z, type);
        if (currentLevel == 0) {
            continue;  // Already dark
        }

        // Check if this light is still dependent on the removed source
        bool stillHasSource = false;
        for (int i = 0; i < 6; ++i) {
            int nx = node.x + NEIGHBOR_OFFSETS[i][0];
            int ny = node.y + NEIGHBOR_OFFSETS[i][1];
            int nz = node.z + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborLight = getLightWorld(nx, ny, nz, type);
            uint8_t opacity = getOpacityWorld(node.x, node.y, node.z);

            // If neighbor can provide this light level, we're not dependent
            if (neighborLight > opacity && neighborLight - opacity >= currentLevel) {
                stillHasSource = true;
                break;
            }
        }

        if (stillHasSource) {
            // This node has an independent source, add to relight
            relightSources.push_back(node);
            continue;
        }

        // Clear this position
        setLightWorld(node.x, node.y, node.z, type, 0);

        // Propagate to neighbors that were lit by this node
        for (int i = 0; i < 6; ++i) {
            int nx = node.x + NEIGHBOR_OFFSETS[i][0];
            int ny = node.y + NEIGHBOR_OFFSETS[i][1];
            int nz = node.z + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborLight = getLightWorld(nx, ny, nz, type);

            // If neighbor's light is less than or equal to what we provided, it depends on us
            if (neighborLight != 0 && neighborLight < oldLevel) {
                enqueueDecrease(nx, ny, nz, neighborLight);
            } else if (neighborLight >= oldLevel) {
                // Neighbor has independent light source
                relightSources.emplace_back(static_cast<int16_t>(nx),
                                             static_cast<int16_t>(ny),
                                             static_cast<int16_t>(nz),
                                             neighborLight);
            }
        }
    }

    // Re-light phase: propagate from surviving light sources
    clearQueues();
    for (const auto& source : relightSources) {
        uint8_t level = getLightWorld(source.x, source.y, source.z, type);
        if (level > 0) {
            enqueueIncrease(source.x, source.y, source.z, level);
        }
    }

    propagateIncrease(type);
}

// ============================================================================
// Block change handler (world coordinates)
// ============================================================================
void LightEngine::onBlockChange(int worldX, int worldY, int worldZ,
                                uint8_t oldBlock, uint8_t newBlock) {
    const auto& oldDef = BlockRegistry::get(oldBlock);
    const auto& newDef = BlockRegistry::get(newBlock);

    bool wasSource = oldDef.isLightSource && oldDef.lightLevel > 0;
    bool isSource = newDef.isLightSource && newDef.lightLevel > 0;

    // Update heightmap
    updateHeightmapAt(worldX, worldZ);

    // Handle sky light changes
    if (!oldDef.isSolid && newDef.isSolid) {
        // Placed solid block - this may block light propagation
        clearQueues();

        uint8_t oldSunlight = getLightWorld(worldX, worldY, worldZ, LightType::SKY);

        // Always set this position to 0 for solid blocks
        setLightWorld(worldX, worldY, worldZ, LightType::SKY, 0);
        
        if (oldSunlight > 0) {
            enqueueDecrease(worldX, worldY, worldZ, oldSunlight);
        }

        // Always check neighbors when placing a solid block
        // They might have been receiving light through this position
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborSky = getLightWorld(nx, ny, nz, LightType::SKY);
            if (neighborSky > 0) {
                // Check if this neighbor can still receive light from other directions
                bool hasOtherSource = false;
                
                // First check if sky is visible above (for "well" structures)
                for (int y = ny + 1; y < Chunk::SIZE_Y; ++y) {
                    uint8_t opacity = getOpacityWorld(nx, y, nz);
                    if (opacity >= 15) break;  // Blocked from above
                    uint8_t sunAtY = getLightWorld(nx, y, nz, LightType::SKY);
                    if (sunAtY == 15) {
                        hasOtherSource = true;
                        break;
                    }
                }
                
                // Then check horizontal neighbors if not already found a source
                if (!hasOtherSource) {
                    for (int j = 0; j < 6; ++j) {
                        int nx2 = nx + NEIGHBOR_OFFSETS[j][0];
                        int ny2 = ny + NEIGHBOR_OFFSETS[j][1];
                        int nz2 = nz + NEIGHBOR_OFFSETS[j][2];

                        if (ny2 < 0 || ny2 >= Chunk::SIZE_Y) continue;
                        // Skip the newly placed block
                        if (nx2 == worldX && ny2 == worldY && nz2 == worldZ) continue;

                        uint8_t neighbor2Sky = getLightWorld(nx2, ny2, nz2, LightType::SKY);
                        uint8_t opacity = getOpacityWorld(nx, ny, nz);
                        if (neighbor2Sky > opacity && neighbor2Sky - opacity >= neighborSky) {
                            hasOtherSource = true;
                            break;
                        }
                    }
                }

                if (!hasOtherSource) {
                    // This neighbor might have been receiving light through the blocked position
                    // Trigger decrease to let the algorithm recalculate
                    if (neighborSky <= oldSunlight || oldSunlight == 0) {
                        enqueueDecrease(nx, ny, nz, neighborSky);
                    }
                }
            }
        }

        if (!m_decreaseQueue.empty()) {
            propagateDecrease(LightType::SKY);
        }
    } else if (oldDef.isSolid && !newDef.isSolid) {
        // Removed solid block, sky light may enter
        // First check if sky is directly visible above (fast path for surface)
        uint8_t skyLightAbove = 0;
        for (int y = worldY + 1; y < Chunk::SIZE_Y; ++y) {
            uint8_t opacity = getOpacityWorld(worldX, y, worldZ);
            if (opacity >= 15) {
                skyLightAbove = 0;
                break;
            }
            uint8_t sunAtY = getLightWorld(worldX, y, worldZ, LightType::SKY);
            if (sunAtY == 15) {
                skyLightAbove = 15;
                break;
            }
        }

        // Check 6 neighbors for sky light
        uint8_t maxNeighborSky = 0;
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborSky = getLightWorld(nx, ny, nz, LightType::SKY);
            if (neighborSky > maxNeighborSky) {
                maxNeighborSky = neighborSky;
            }
        }

        // Use the best available sky light source
        maxNeighborSky = std::max(maxNeighborSky, skyLightAbove);

        uint8_t opacity = newDef.opacity;
        uint8_t newLevel = maxNeighborSky > opacity ? maxNeighborSky - opacity : 0;

        // Always process when removing a solid block to ensure proper lighting
        clearQueues();

        if (newLevel > 0) {
            setLightWorld(worldX, worldY, worldZ, LightType::SKY, newLevel);
            enqueueIncrease(worldX, worldY, worldZ, newLevel);
            propagateIncrease(LightType::SKY);
        } else {
            // Even if no light enters, clear any stale light value
            setLightWorld(worldX, worldY, worldZ, LightType::SKY, 0);
        }

        // Also trigger re-propagation from neighbors to ensure light spreads correctly
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborSky = getLightWorld(nx, ny, nz, LightType::SKY);
            if (neighborSky > 1) {
                enqueueIncrease(nx, ny, nz, neighborSky);
            }
        }

        if (!m_increaseQueue.empty()) {
            propagateIncrease(LightType::SKY);
        }
    }

    // Handle block light changes
    if (wasSource && !isSource) {
        // Light source removed
        setLightWorld(worldX, worldY, worldZ, LightType::BLOCK, 0);
        clearQueues();
        enqueueDecrease(worldX, worldY, worldZ, oldDef.lightLevel);
        propagateDecrease(LightType::BLOCK);
    } else if (!wasSource && !isSource && oldDef.isSolid && !newDef.isSolid) {
        // Removed a solid block that wasn't a light source - need to check if block light can now enter
        // Check all 6 neighbors for block light sources
        uint8_t maxNeighborBlock = 0;
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborBlock = getLightWorld(nx, ny, nz, LightType::BLOCK);
            if (neighborBlock > maxNeighborBlock) {
                maxNeighborBlock = neighborBlock;
            }
        }

        // Always propagate block light when removing a solid block, even if no immediate light source
        // This ensures proper light recalculation in the area
        clearQueues();

        uint8_t opacity = newDef.opacity;
        uint8_t newLevel = maxNeighborBlock > opacity ? maxNeighborBlock - opacity : 0;

        if (newLevel > 0) {
            setLightWorld(worldX, worldY, worldZ, LightType::BLOCK, newLevel);
            enqueueIncrease(worldX, worldY, worldZ, newLevel);
        }

        // Also trigger propagation from any existing block light neighbors
        // This is crucial to ensure light spreads into the newly opened space
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborBlock = getLightWorld(nx, ny, nz, LightType::BLOCK);
            if (neighborBlock > 1) {
                enqueueIncrease(nx, ny, nz, neighborBlock);
            }
        }

        if (!m_increaseQueue.empty()) {
            propagateIncrease(LightType::BLOCK);
        }
    } else if (!wasSource && !isSource && !oldDef.isSolid && newDef.isSolid) {
        // Placed solid block that's not a light source - check if it blocks block light
        clearQueues();

        uint8_t oldBlockLight = getLightWorld(worldX, worldY, worldZ, LightType::BLOCK);

        // Always set this position to 0 for solid blocks
        setLightWorld(worldX, worldY, worldZ, LightType::BLOCK, 0);
        
        if (oldBlockLight > 0) {
            enqueueDecrease(worldX, worldY, worldZ, oldBlockLight);
        }

        // Always check neighbors when placing a solid block
        for (int i = 0; i < 6; ++i) {
            int nx = worldX + NEIGHBOR_OFFSETS[i][0];
            int ny = worldY + NEIGHBOR_OFFSETS[i][1];
            int nz = worldZ + NEIGHBOR_OFFSETS[i][2];

            if (ny < 0 || ny >= Chunk::SIZE_Y) continue;

            uint8_t neighborBlock = getLightWorld(nx, ny, nz, LightType::BLOCK);
            if (neighborBlock > 0) {
                // Check if neighbor has other light sources
                bool hasOtherSource = false;
                for (int j = 0; j < 6; ++j) {
                    int nx2 = nx + NEIGHBOR_OFFSETS[j][0];
                    int ny2 = ny + NEIGHBOR_OFFSETS[j][1];
                    int nz2 = nz + NEIGHBOR_OFFSETS[j][2];

                    if (ny2 < 0 || ny2 >= Chunk::SIZE_Y) continue;
                    if (nx2 == worldX && ny2 == worldY && nz2 == worldZ) continue;

                    uint8_t neighbor2Block = getLightWorld(nx2, ny2, nz2, LightType::BLOCK);
                    uint8_t opacity = getOpacityWorld(nx, ny, nz);
                    if (neighbor2Block > opacity && neighbor2Block - opacity >= neighborBlock) {
                        hasOtherSource = true;
                        break;
                    }
                }

                if (!hasOtherSource) {
                    if (neighborBlock <= oldBlockLight || oldBlockLight == 0) {
                        enqueueDecrease(nx, ny, nz, neighborBlock);
                    }
                }
            }
        }

        if (!m_decreaseQueue.empty()) {
            propagateDecrease(LightType::BLOCK);
        }
    }

    if (isSource) {
        // New light source placed
        setLightWorld(worldX, worldY, worldZ, LightType::BLOCK, newDef.lightLevel);
        clearQueues();
        enqueueIncrease(worldX, worldY, worldZ, newDef.lightLevel);
        propagateIncrease(LightType::BLOCK);
    }

    // Mark affected chunks dirty
    // We need to mark a larger area because light propagation can affect adjacent chunks
    int chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::SIZE_X));
    int chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::SIZE_Z));

    // Mark all chunks in a 3x3 area as dirty since light can propagate across chunk boundaries
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            Chunk* chunk = getChunkAtChunkCoord(chunkX + dx, chunkZ + dz);
            if (chunk) {
                chunk->markDirty();
            }
        }
    }

    // Also mark chunks that share an edge with the block position
    // This ensures proper lighting updates at chunk boundaries
    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    if (localX < 0) localX += Chunk::SIZE_X;
    if (localZ < 0) localZ += Chunk::SIZE_Z;

    // If block is at chunk boundary, mark the neighbor chunk too
    if (localX == 0) {
        Chunk* neighbor = getChunkAtChunkCoord(chunkX - 1, chunkZ);
        if (neighbor) neighbor->markDirty();
    }
    if (localX == Chunk::SIZE_X - 1) {
        Chunk* neighbor = getChunkAtChunkCoord(chunkX + 1, chunkZ);
        if (neighbor) neighbor->markDirty();
    }
    if (localZ == 0) {
        Chunk* neighbor = getChunkAtChunkCoord(chunkX, chunkZ - 1);
        if (neighbor) neighbor->markDirty();
    }
    if (localZ == Chunk::SIZE_Z - 1) {
        Chunk* neighbor = getChunkAtChunkCoord(chunkX, chunkZ + 1);
        if (neighbor) neighbor->markDirty();
    }
}

// ============================================================================
// Heightmap update
// ============================================================================
void LightEngine::updateHeightmapAt(int worldX, int worldZ) {
    int chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::SIZE_X));
    int chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::SIZE_Z));

    Chunk* chunk = getChunkAtChunkCoord(chunkX, chunkZ);
    if (!chunk) return;

    int localX = worldX - chunkX * Chunk::SIZE_X;
    int localZ = worldZ - chunkZ * Chunk::SIZE_Z;
    if (localX < 0) localX += Chunk::SIZE_X;
    if (localZ < 0) localZ += Chunk::SIZE_Z;

    // Find new highest block
    int highestY = 0;
    for (int y = Chunk::SIZE_Y - 1; y >= 0; --y) {
        uint8_t blockId = chunk->getBlock(localX, y, localZ);
        if (blockId != BlockType::AIR) {
            highestY = y;
            break;
        }
    }

    chunk->setHeightmap(localX, localZ, highestY);
}

// ============================================================================
// Propagate from neighbors (called when a chunk is loaded)
// ============================================================================
void LightEngine::propagateFromNeighbors(Chunk& chunk) {
    // Check all 4 neighbors for light that can spread into this chunk
    clearQueues();

    const int baseX = chunk.m_chunkX * Chunk::SIZE_X;
    const int baseZ = chunk.m_chunkZ * Chunk::SIZE_Z;

    // Check each edge
    // -X edge: check neighbor's +X edge
    if (chunk.neighbors[1]) {  // -X neighbor
        Chunk* neighbor = chunk.neighbors[1];
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                uint8_t blockLight = neighbor->getBlockLight(Chunk::SIZE_X - 1, y, z);
                uint8_t sunLight = neighbor->getSunlight(Chunk::SIZE_X - 1, y, z);

                if (blockLight > 1) {
                    enqueueIncrease(baseX, y, baseZ + z, blockLight);
                }
                if (sunLight > 1) {
                    // Sky light propagation handled separately
                }
            }
        }
    }

    // +X edge
    if (chunk.neighbors[0]) {  // +X neighbor
        Chunk* neighbor = chunk.neighbors[0];
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int z = 0; z < Chunk::SIZE_Z; ++z) {
                uint8_t blockLight = neighbor->getBlockLight(0, y, z);
                if (blockLight > 1) {
                    enqueueIncrease(baseX + Chunk::SIZE_X - 1, y, baseZ + z, blockLight);
                }
            }
        }
    }

    // -Z edge
    if (chunk.neighbors[3]) {  // -Z neighbor
        Chunk* neighbor = chunk.neighbors[3];
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                uint8_t blockLight = neighbor->getBlockLight(x, y, Chunk::SIZE_Z - 1);
                if (blockLight > 1) {
                    enqueueIncrease(baseX + x, y, baseZ, blockLight);
                }
            }
        }
    }

    // +Z edge
    if (chunk.neighbors[2]) {  // +Z neighbor
        Chunk* neighbor = chunk.neighbors[2];
        for (int y = 0; y < Chunk::SIZE_Y; ++y) {
            for (int x = 0; x < Chunk::SIZE_X; ++x) {
                uint8_t blockLight = neighbor->getBlockLight(x, y, 0);
                if (blockLight > 1) {
                    enqueueIncrease(baseX + x, y, baseZ + Chunk::SIZE_Z - 1, blockLight);
                }
            }
        }
    }

    if (!m_increaseQueue.empty()) {
        propagateIncrease(LightType::BLOCK);
    }
}
