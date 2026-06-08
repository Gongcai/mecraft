#ifndef MECRAFT_ECS_ENTITY_FACTORY_H
#define MECRAFT_ECS_ENTITY_FACTORY_H

#include "../GameplayRegistry.h"

#include <cstddef>
#include <cstdint>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

#include "../../item/Item.h"

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

class EntityFactory {
public:
    static entt::entity createServerPlayerProxy(GameplayRegistry& registry,
                                                const glm::vec3& position,
                                                const glm::vec3& velocity);
    static void ensureServerPlayerProxy(GameplayRegistry& registry,
                                        entt::entity entity,
                                        const glm::vec3& position,
                                        const glm::vec3& velocity);

    static entt::entity createZombie(GameplayRegistry& registry, const glm::vec3& position);
    static entt::entity createZombie(entt::registry& registry, const glm::vec3& position);
    static entt::entity createItemDrop(GameplayRegistry& registry, const ItemDropSpawnParams& params);
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
