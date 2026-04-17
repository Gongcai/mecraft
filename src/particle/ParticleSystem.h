#ifndef MECRAFT_PARTICLESYSTEM_H
#define MECRAFT_PARTICLESYSTEM_H

#include <glm/glm.hpp>
#include <glad/glad.h>
#include "../world/Block.h"

class ResourceMgr;
class Shader;
struct TextureArray;

namespace ecs {
class GameplayRegistry;
}

class ParticleSystem {
public:
    void bindRegistry(ecs::GameplayRegistry& registry);

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void emit(const glm::ivec3& blockPos, BlockID blockType);
    void update(float dt);
    void render(const glm::mat4& projection, const glm::mat4& view);

private:
    ecs::GameplayRegistry* m_registry = nullptr;
    Shader* m_shader = nullptr;
    const TextureArray* m_texArray = nullptr;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    static constexpr int MAX_PARTICLES = 1000;
};

#endif // MECRAFT_PARTICLESYSTEM_H
