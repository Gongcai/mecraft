#ifndef MECRAFT_MOB_MODEL_FACTORY_H
#define MECRAFT_MOB_MODEL_FACTORY_H

#include "../GameplayRegistry.h"
#include <glm/glm.hpp>

namespace ecs {

class MobModelFactory {
public:
    static entt::entity createZombie(GameplayRegistry& registry,
                                     const glm::vec3& worldPosition);

    static void destroyMob(GameplayRegistry& registry, entt::entity mobRoot);
};

} // namespace ecs

#endif // MECRAFT_MOB_MODEL_FACTORY_H
