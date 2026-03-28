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
#include "../physics/PhysicsInfo.h"

class World {
public:
    void init(uint32_t seed);
    void update(const glm::vec3& playerPos);  // 每帧调用

    // 方块操作 (世界坐标)
    [[nodiscard]] BlockID getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockID id);

    // 射线拾取：返回命中的方块位置和放置位置
    bool raycast(const PhysicsInfo& ray, float maxDist,
                 glm::ivec3& hitBlock, glm::ivec3& placeBlock) const;

    // 获取所有需要渲染的区块
    [[nodiscard]] const auto& getActiveChunks() const { return m_chunks; }

    [[nodiscard]] size_t getTotalVertexCount() const;

    [[nodiscard]] int getRenderDistance() const { return m_renderDistance; }
    void setRenderDistance(int dist);

    [[nodiscard]] int getFlatSurfaceY() const { return m_flatSurfaceY; }

private:
    // 区块存储: key = (chunkX, chunkZ) 打包为 int64_t
    std::unordered_map<int64_t, std::unique_ptr<Chunk>> m_chunks;

    //TerrainGenerator m_terrainGen;

    int m_renderDistance = 8;   // 以区块为单位
    uint32_t m_seed = 0;
    int m_flatSurfaceY = 63;

    // 区块坐标打包
    static int64_t chunkKey(int cx, int cz);

    // 加载/卸载
    void loadChunk(int cx, int cz);
    void unloadChunk(int cx, int cz);

    // 区块加载队列 (按距离排序, 近的优先)
    std::vector<glm::ivec2> m_loadQueue;
    void updateLoadQueue(int playerChunkX, int playerChunkZ);
};

#endif //MECRAFT_WORLD_H