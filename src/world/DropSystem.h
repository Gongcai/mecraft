#ifndef MECRAFT_DROPSYSTEM_H
#define MECRAFT_DROPSYSTEM_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Block.h"

class World;

enum class DropKind {
    Item,
    Block
};

struct DropEntity {
    std::size_t id = 0;
    DropKind kind = DropKind::Block;
    BlockID blockId = BlockType::AIR;

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 halfExtents = glm::vec3(0.175f);

    float yawRadians = 0.0f;
    float spinSpeedRadians = 0.0f;
    float ageSeconds = 0.0f;
    float lifeTimeSeconds = 30.0f;
    uint32_t stackCount = 1;

    bool grounded = false;
};

class DropSystem {
public:
    void spawnBlockDrop(BlockID blockId, const glm::ivec3& blockPos);
    void onBlockPlaced(const glm::ivec3& blockPos, const World& world);
    void update(float dt, const World& world);
    void clear();

    [[nodiscard]] const std::vector<DropEntity>& getDrops() const;

private:
    std::vector<DropEntity> m_drops;
    std::size_t m_nextId = 1;
    float m_mergeAccumulator = 0.0f;
};

#endif // MECRAFT_DROPSYSTEM_H

