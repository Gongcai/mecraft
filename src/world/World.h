//
// Created by Caiwe on 2026/3/24.
//

#ifndef MECRAFT_WORLD_H
#define MECRAFT_WORLD_H
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Chunk.h"
#include "LightService.h"
#include "TerrainGenerator.h"
#include "DayNightSystem.h"
#include "../thread/ThreadPool.h"
#include "../physics/PhysicsInfo.h"

class World {
public:
    void init(uint32_t seed);
    void update(const glm::vec3& playerPos);  // 每帧调用

    // 方块操作 (世界坐标)
    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    [[nodiscard]] BlockID sampleGeneratedBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);

    // 射线拾取：返回命中的方块位置和放置位置
    bool raycast(const PhysicsInfo& ray, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;

    // 获取所有需要渲染的区块
    [[nodiscard]] const auto& getActiveChunks() const { return m_chunks; }
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

    // Chunk key packing — public for light/meshing services
    static int64_t chunkKey(int cx, int cz);

    DayNightSystem& getDayNightSystem() { return m_dayNightSystem; }
    const DayNightSystem& getDayNightSystem() const { return m_dayNightSystem; }

private:
    // 区块存储: key = (chunkX, chunkZ) 打包为 int64_t
    // shared_ptr so worker threads can hold references during async snapshot capture
    std::unordered_map<int64_t, std::shared_ptr<Chunk>> m_chunks;

    TerrainGenerator m_terrainGen;
    std::unique_ptr<LightService> m_lightService;
    DayNightSystem m_dayNightSystem;
    ThreadPool* m_threadPool = nullptr;

    int m_renderDistance = 8;   // 以区块为单位
    uint32_t m_seed = 0;
    int m_flatSurfaceY = 63;

    // 加载/卸载
    void loadChunk(int cx, int cz);
    void unloadChunk(int cx, int cz);

    // 区块加载队列 (按距离排序, 近的优先)
    std::vector<glm::ivec2> m_loadQueue;
    void updateLoadQueue(int playerChunkX, int playerChunkZ);
};

#endif //MECRAFT_WORLD_H
