#ifndef MECRAFT_PARTICLESYSTEM_H
#define MECRAFT_PARTICLESYSTEM_H

#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "../world/Block.h"

class ResourceMgr;
class Shader;
struct TextureAtlas;

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float life;
    float maxLife;
    float size;
    glm::vec2 uvMin;
    glm::vec2 uvMax;
};

class ParticleSystem {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    void emit(const glm::ivec3& blockPos, BlockID blockType);
    void update(float dt);
    void render(const glm::mat4& projection, const glm::mat4& view);

private:
    std::vector<Particle> m_particles;
    Shader* m_shader = nullptr;
    const TextureAtlas* m_atlas = nullptr;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    static constexpr int MAX_PARTICLES = 1000;
    static constexpr int PARTICLES_PER_BREAK = 24;
};

#endif // MECRAFT_PARTICLESYSTEM_H
