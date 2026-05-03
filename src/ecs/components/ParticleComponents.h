#ifndef MECRAFT_ECS_PARTICLE_COMPONENTS_H
#define MECRAFT_ECS_PARTICLE_COMPONENTS_H

#include <glm/glm.hpp>

namespace ecs {

struct ParticleComponent {
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 0.1f;
    float grassTintFactor = 0.0f;
    float layer = 0.0f;
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{1.0f};
};

} // namespace ecs

#endif // MECRAFT_ECS_PARTICLE_COMPONENTS_H
