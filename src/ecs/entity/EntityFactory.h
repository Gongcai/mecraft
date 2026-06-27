#ifndef MECRAFT_ECS_ENTITY_FACTORY_H
#define MECRAFT_ECS_ENTITY_FACTORY_H

#include "../GameplayRegistry.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

#include "../../item/Item.h"
#include "../../world/block/Block.h"
#include "../../world/block/BlockStateRegistry.h"

namespace ecs {

struct ProjectileDefinition;

struct ItemDropSpawnParams {
    ItemID itemId = 0;
    uint32_t stackCount = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 halfExtents{0.175f};
    float yawRadians = 0.0f;
    float spinSpeedRadians = 0.0f;
    float ageSeconds = 0.0f;
    float lifeTimeSeconds = 30.0f;
    bool grounded = false;
    std::size_t dropId = 0;
};

/// Spawn parameters for a falling block entity.
/// `gridPosition` is the integer cell the block occupied before falling;
/// the entity renders centered on that cell and advances one cell per tick.
struct FallingBlockSpawnParams {
    BlockID blockId = 0;
    glm::ivec3 gridPosition{};
};

/// Spawn parameters for a piston-driven moving block entity.
/// `stateId` stores the exact block state restored when the motion completes.
struct MovingBlockSpawnParams {
    StateID stateId = 0;
    glm::ivec3 sourcePosition{};
    glm::ivec3 targetPosition{};
    glm::ivec3 direction{};
    float durationSeconds = 0.1f;
    bool placeAtTarget = true;
};

class EntityFactory {
public:
    static entt::entity createServerPlayerProxy(GameplayRegistry& registry,
                                                const glm::vec3& position,
                                                const glm::vec3& velocity);
    static void ensureServerPlayerProxy(GameplayRegistry& registry,
                                        entt::entity entity,
                                        const glm::vec3& position,
                                        const glm::vec3& velocity);

    static entt::entity createMob(GameplayRegistry& registry,
                                  std::string_view entityId,
                                  const glm::vec3& position);
    static entt::entity createZombie(GameplayRegistry& registry, const glm::vec3& position);
    static entt::entity createZombie(entt::registry& registry, const glm::vec3& position);
    static entt::entity createItemDrop(GameplayRegistry& registry, const ItemDropSpawnParams& params);
    static entt::entity createFallingBlock(GameplayRegistry& registry, const FallingBlockSpawnParams& params);
    static entt::entity createMovingBlock(GameplayRegistry& registry, const MovingBlockSpawnParams& params);
    static entt::entity createProjectile(GameplayRegistry& registry,
                                         entt::entity owner,
                                         const glm::vec3& position,
                                         const glm::vec3& velocity,
                                         const ProjectileDefinition& definition);
    static entt::entity createAppleProjectile(GameplayRegistry& registry,
                                              entt::entity owner,
                                              const glm::vec3& position,
                                              const glm::vec3& velocity);
};

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_FACTORY_H
