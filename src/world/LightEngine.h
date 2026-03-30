//
// LightEngine.h - High Performance Lighting System
// BFS-based propagation algorithm, inspired by Minecraft Starlight
//

#ifndef MECRAFT_LIGHTENGINE_H
#define MECRAFT_LIGHTENGINE_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_set>
#include <vector>
#include <glm/vec3.hpp>

class Chunk;
class World;

// Light type enumeration
enum class LightType : uint8_t {
    BLOCK = 0,
    SKY   = 1
};

// Light propagation node (world coordinates for cross-chunk support)
struct LightNode {
    int16_t x, y, z;
    uint8_t level;  // Current light level at this node

    LightNode(int16_t x_ = 0, int16_t y_ = 0, int16_t z_ = 0, uint8_t level_ = 0)
        : x(x_), y(y_), z(z_), level(level_) {}
};

class LightEngine {
public:
    explicit LightEngine(World* world);

    // Initialize chunk lighting (called after terrain generation)
    void initChunkLight(Chunk& chunk);

    // Update lighting when block changes (world coordinates)
    void onBlockChange(int worldX, int worldY, int worldZ, uint8_t oldBlock, uint8_t newBlock);

    // Sky light propagation entry point
    void spreadSkylight(Chunk& chunk);

    // Block light propagation entry point
    void spreadBlockLight(Chunk& chunk);

    // Propagate light from neighbor chunks (called when a chunk is loaded)
    void propagateFromNeighbors(Chunk& chunk);

private:
    World* m_world;

    // BFS queues - using vector for better cache locality
    std::vector<LightNode> m_increaseQueue;
    std::vector<LightNode> m_decreaseQueue;
    size_t m_increaseReadIdx = 0;
    size_t m_decreaseReadIdx = 0;

    // Neighbor offsets (6 directions)
    static constexpr int NEIGHBOR_OFFSETS[6][3] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };

    // Core algorithms
    void propagateIncrease(LightType type);
    void propagateDecrease(LightType type);

    // Queue management
    void clearQueues();
    void enqueueIncrease(int x, int y, int z, uint8_t level);
    void enqueueDecrease(int x, int y, int z, uint8_t level);
    bool processIncreaseNode(const LightNode& node, LightType type);
    bool processDecreaseNode(const LightNode& node, LightType type);

    // Light value access (world coordinates)
    [[nodiscard]] uint8_t getLightWorld(int worldX, int worldY, int worldZ, LightType type) const;
    void setLightWorld(int worldX, int worldY, int worldZ, LightType type, uint8_t level);

    // Opacity access (world coordinates)
    [[nodiscard]] uint8_t getOpacityWorld(int worldX, int worldY, int worldZ) const;

    // Get chunk from world coordinates
    [[nodiscard]] Chunk* getChunkAt(int worldX, int worldZ) const;
    [[nodiscard]] Chunk* getChunkAtChunkCoord(int chunkX, int chunkZ) const;

    // Convert world to local coordinates
    static void worldToLocal(int worldX, int worldY, int worldZ,
                             int& chunkX, int& chunkZ,
                             int& localX, int& localY, int& localZ);

    // Heightmap helper
    void updateHeightmapAt(int worldX, int worldZ);

    // Sky light specific
    void initSkylightWithHeightmap(Chunk& chunk);
    void propagateSkylightFromAbove(Chunk& chunk);

    // Re-light phase for decrease
    void collectRelightSources(std::vector<LightNode>& sources, LightType type);
};

#endif // MECRAFT_LIGHTENGINE_H
