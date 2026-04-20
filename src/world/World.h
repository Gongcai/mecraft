#ifndef MECRAFT_WORLD_H
#define MECRAFT_WORLD_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Chunk.h"
#include "DayNightSystem.h"
#include "LightService.h"
#include "TerrainGenerator.h"
#include "../physics/PhysicsInfo.h"
#include "../thread/ThreadPool.h"

class World {
public:
    void init(uint32_t seed);
    void update(const glm::vec3& playerPos);

    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    [[nodiscard]] BlockID sampleGeneratedBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);

    [[nodiscard]] RayHit raycast(const PhysicsInfo& ray, float maxDist) const;
    bool raycast(const PhysicsInfo& ray, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;

    [[nodiscard]] const auto& getActiveChunks() const { return m_chunks; }
    [[nodiscard]] uint64_t getActiveChunkRevision() const { return m_activeChunkRevision; }
    void setThreadPool(ThreadPool* pool);
    [[nodiscard]] LightFrameStats getLightFrameStats() const;

    [[nodiscard]] size_t getTotalVertexCount() const;

    [[nodiscard]] int getRenderDistance() const { return m_renderDistance; }
    void setRenderDistance(int dist);

    [[nodiscard]] int getFlatSurfaceY() const { return m_flatSurfaceY; }
    [[nodiscard]] int getSurfaceY(int x, int z) const;
    [[nodiscard]] TerrainBiome getBiome(int x, int z) const;
    [[nodiscard]] glm::ivec2 getChunkCoords(int worldX, int worldZ) const;
    [[nodiscard]] static const char* biomeToString(TerrainBiome biome);

    static int64_t chunkKey(int cx, int cz);

    DayNightSystem& getDayNightSystem() { return m_dayNightSystem; }
    const DayNightSystem& getDayNightSystem() const { return m_dayNightSystem; }

private:
    std::unordered_map<int64_t, std::shared_ptr<Chunk>> m_chunks;

    TerrainGenerator m_terrainGen;
    std::unique_ptr<LightService> m_lightService;
    DayNightSystem m_dayNightSystem;
    ThreadPool* m_threadPool = nullptr;

    int m_renderDistance = 8;
    uint32_t m_seed = 0;
    int m_flatSurfaceY = 63;
    uint64_t m_activeChunkRevision = 1;

    void loadChunk(int cx, int cz);
    void unloadChunk(int cx, int cz);

    std::vector<glm::ivec2> m_loadQueue;
    void updateLoadQueue(int playerChunkX, int playerChunkZ);
};

#endif //MECRAFT_WORLD_H
