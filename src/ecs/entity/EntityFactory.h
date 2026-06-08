#ifndef MECRAFT_ECS_ENTITY_FACTORY_H
#define MECRAFT_ECS_ENTITY_FACTORY_H

#include "../GameplayRegistry.h"

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

namespace ecs {

struct ProjectileDefinition;

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
