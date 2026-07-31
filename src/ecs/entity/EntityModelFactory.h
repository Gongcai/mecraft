#ifndef MECRAFT_ECS_ENTITY_MODEL_FACTORY_H
#define MECRAFT_ECS_ENTITY_MODEL_FACTORY_H

#include "../GameplayRegistry.h"

#include <glm/glm.hpp>

namespace ecs {

struct MobEntityDefinition;

class EntityModelFactory {
public:
    static entt::entity createMob(GameplayRegistry& registry, const MobEntityDefinition& definition,
                                  const glm::vec3& worldPosition, bool gameplayControlled = true);

    static entt::entity createMobReplica(GameplayRegistry& registry, const MobEntityDefinition& definition,
                                         const glm::vec3& worldPosition, float yaw = 0.0f);
};

} // namespace ecs

#endif // MECRAFT_ECS_ENTITY_MODEL_FACTORY_H
