#ifndef MECRAFT_LIGHTENGINE_H
#define MECRAFT_LIGHTENGINE_H

#include <cstdint>
#include <deque>
#include <vector>
#include <unordered_set>

class Chunk;
class World;

class LightEngine {
public:
    explicit LightEngine(World& world);

    // Full initialization when a chunk is first loaded
    void onChunkLoaded(Chunk& chunk);

    // Propagate light from a newly loaded chunk into an existing neighbor
    void propagateBorderInto(Chunk& from, Chunk& into, int direction);

    // Incremental update when a block is placed or broken
    void onBlockChanged(int wx, int wy, int wz,
                        uint8_t oldBlockId, uint8_t newBlockId);

    // --- Tick-based budget logic ---
    void tick(int budget = 32768);

    // Mark a chunk (by chunk coords) as needing re-meshing, deferred until BFS completes
    void markNeighborDirty(int chunkX, int chunkZ);

private:
    World& m_world;

    // --- BFS node ---
    struct LightNode {
        int32_t x, y, z;
        uint8_t level;
    };

    // --- Sky light ---
    void initSkyLight(Chunk& chunk);
    void propagateSkyLight(Chunk& chunk);
    void spreadSkyLight(const std::vector<LightNode>& seeds);
    void removeSkyLight(int wx, int wy, int wz);

    // --- Block light ---
    void initBlockLight(Chunk& chunk);
    void spreadBlockLight(const std::vector<LightNode>& seeds);
    void removeBlockLight(int wx, int wy, int wz);

    // --- Persistence queues ---
    std::deque<LightNode> m_skyRemoveQueue;
    std::deque<LightNode> m_skySpreadQueue;
    std::deque<LightNode> m_blockRemoveQueue;
    std::deque<LightNode> m_blockSpreadQueue;

    std::unordered_set<int64_t> m_dirtyChunks;

    // --- World-coordinate light access (crosses chunk boundaries) ---
    uint8_t getSkyLight(int wx, int wy, int wz) const;
    uint8_t getBlockLightAt(int wx, int wy, int wz) const;
    void setSkyLight(int wx, int wy, int wz, uint8_t val);
    void setBlockLightAt(int wx, int wy, int wz, uint8_t val);
    bool isOpaque(int wx, int wy, int wz) const;
    uint8_t getOpacity(int wx, int wy, int wz) const;

    // Mark chunks dirty when their light changes
    void markChunkDirtyAt(int wx, int wz);
};

#endif // MECRAFT_LIGHTENGINE_H
