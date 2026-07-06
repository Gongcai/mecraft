#ifndef MECRAFT_PARTICLESYSTEM_H
#define MECRAFT_PARTICLESYSTEM_H

#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "../world/block/Block.h"

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
    // Render particles into SceneComposite with voxel light and depth from GBuffer.
    // Called by Renderer before volumetric fog composite so particles
    // receive unified fog. The caller must have already bound the target FBO
    // and set up blend/depth-write state.
    void renderToSceneResolved(Shader& shader, GLuint voxelLightTex, GLuint depthTex,
                               const glm::mat4& view, const glm::mat4& viewProj,
                               const glm::vec2& screenSize);

private:
    // Build billboard vertices from ECS particle data. Returns vertex count.
    int buildVertices(const glm::mat4& view, std::vector<float>& vertices);

    ecs::GameplayRegistry* m_registry = nullptr;
    Shader* m_shader = nullptr;
    Shader* m_gbufferShader = nullptr;
    const TextureArray* m_texArray = nullptr;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    std::vector<float> m_vertexBuffer;

    static constexpr int MAX_PARTICLES = 1000;
};

#endif // MECRAFT_PARTICLESYSTEM_H
