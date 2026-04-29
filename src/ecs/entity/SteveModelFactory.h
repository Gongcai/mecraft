#ifndef MECRAFT_STEVE_MODEL_FACTORY_H
#define MECRAFT_STEVE_MODEL_FACTORY_H

#include "../GameplayRegistry.h"
#include <glm/glm.hpp>

namespace ecs {

class SteveModelFactory {
public:
    static entt::entity createSteve(GameplayRegistry& registry,
                                     const glm::vec3& worldPosition);

    static void destroySteve(GameplayRegistry& registry, entt::entity steveRoot);
};

} // namespace ecs

#endif // MECRAFT_STEVE_MODEL_FACTORY_H
