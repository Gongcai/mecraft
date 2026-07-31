#ifndef MECRAFT_DROPSYSTEM_H
#define MECRAFT_DROPSYSTEM_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "block/Block.h"
#include "../item/Item.h"

class World;
class Inventory;

namespace ecs {
class GameplayRegistry;
class GameplayServices;
} // namespace ecs

struct DropEntity {
    std::size_t id = 0;
    ItemID itemId = 0;

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
    void bindRegistry(ecs::GameplayRegistry& registry);
    void bindServices(ecs::GameplayServices& services);

    void spawnItemDrop(ItemID itemId, const glm::ivec3& blockPos, uint32_t stackCount = 1);
    void spawnBlockDrop(BlockID blockId, const glm::ivec3& blockPos);
    void onBlockPlaced(const glm::ivec3& blockPos, const World& world);
    void update(float dt, const World& world);
    uint32_t collectNearbyDrops(const glm::vec3& position, float radius, Inventory& inventory);
    void clear();

    [[nodiscard]] const std::vector<DropEntity>& getDrops() const;

    /// Restore drops from saved data (used by save system on world load).
    void restoreDrops(const std::vector<DropEntity>& drops);

private:
    ecs::GameplayRegistry* m_registry = nullptr;
    ecs::GameplayServices* m_services = nullptr;
    mutable std::vector<DropEntity> m_dropCache;
};

#endif // MECRAFT_DROPSYSTEM_H
