#include "ParticleSystem.h"

#include <vector>

#include <glm/common.hpp>

#include "../ecs/components/Components.h"
#include "../ecs/GameplayRegistry.h"
#include "../ecs/util/ParticleEventBuffer.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"

void ParticleSystem::bindRegistry(ecs::GameplayRegistry& registry) {
    m_registry = &registry;
}

void ParticleSystem::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("particle");
    m_gbufferShader = resourceMgr.getShader("particle_gbuffer");
    m_texArray = &resourceMgr.getTextureArray();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 6 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(7 * sizeof(float)));

    glBindVertexArray(0);
}

void ParticleSystem::shutdown() {
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void ParticleSystem::emit(const glm::ivec3& blockPos, const BlockID blockType) {
    if (m_registry == nullptr || blockType == 0) {
        return;
    }

    auto& bus = ecs::ensureParticleEventBus(*m_registry);
    bus.push({blockPos, blockType});
}

void ParticleSystem::update(const float dt) {
    static_cast<void>(dt);
}

int ParticleSystem::buildVertices(const glm::mat4& view, std::vector<float>& vertices) {
    auto particleView = m_registry->view<ecs::ParticleTag, ecs::TransformComponent, ecs::ParticleComponent>();
    if (particleView.begin() == particleView.end()) {
        return 0;
    }

    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    vertices.clear();
    vertices.reserve(static_cast<size_t>(MAX_PARTICLES) * 48u);

    int count = 0;
    for (const entt::entity e : particleView) {
        const auto& transform = particleView.get<ecs::TransformComponent>(e);
        const auto& particle = particleView.get<ecs::ParticleComponent>(e);
        if (particle.life <= 0.0f || particle.maxLife <= 0.0f) {
            continue;
        }
        if (count >= MAX_PARTICLES) {
            break;
        }

        const float alpha = glm::clamp(particle.life / particle.maxLife, 0.0f, 1.0f);
        const float halfSize = particle.size * 0.5f;

        glm::vec3 c0 = transform.position - right * halfSize - up * halfSize;
        glm::vec3 c1 = transform.position + right * halfSize - up * halfSize;
        glm::vec3 c2 = transform.position + right * halfSize + up * halfSize;
        glm::vec3 c3 = transform.position - right * halfSize + up * halfSize;

        const float uvMinX = particle.uvMin.x, uvMinY = particle.uvMin.y;
        const float uvMaxX = particle.uvMax.x, uvMaxY = particle.uvMax.y;
        const float layer = particle.layer;
        const float btf = particle.biomeTintFactor;

        // Triangle 1: c0-c1-c2
        vertices.insert(vertices.end(), {c0.x, c0.y, c0.z, uvMinX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c1.x, c1.y, c1.z, uvMaxX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c2.x, c2.y, c2.z, uvMaxX, uvMaxY, layer, alpha, btf});
        // Triangle 2: c0-c2-c3
        vertices.insert(vertices.end(), {c0.x, c0.y, c0.z, uvMinX, uvMinY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c2.x, c2.y, c2.z, uvMaxX, uvMaxY, layer, alpha, btf});
        vertices.insert(vertices.end(), {c3.x, c3.y, c3.z, uvMinX, uvMaxY, layer, alpha, btf});

        ++count;
    }
    return count;
}

void ParticleSystem::render(const glm::mat4& projection, const glm::mat4& view) {
    if (m_registry == nullptr || m_shader == nullptr || m_texArray == nullptr) {
        return;
    }

    std::vector<float> vertices;
    if (buildVertices(view, vertices) == 0) {
        return;
    }

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    m_shader->setInt("texArray", 0);
    m_shader->setVec3("uBiomeTintColor", glm::vec3(0.50f, 0.78f, 0.34f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texArray->textureID);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void ParticleSystem::renderToSceneResolved(Shader& shader, GLuint voxelLightTex, GLuint depthTex,
                                            const glm::mat4& view, const glm::mat4& viewProj,
                                            const glm::vec2& screenSize) {
    if (m_registry == nullptr || m_texArray == nullptr) {
        return;
    }

    std::vector<float> vertices;
    if (buildVertices(view, vertices) == 0) {
        return;
    }

    shader.use();
    shader.setMat4("viewProj", viewProj);
    shader.setInt("texArray", 0);
    shader.setInt("uVoxelLightTex", 1);
    shader.setInt("uDepthTex", 2);
    shader.setVec2("uScreenSize", screenSize);
    shader.setVec3("uBiomeTintColor", glm::vec3(0.50f, 0.78f, 0.34f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texArray->textureID);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, voxelLightTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, depthTex);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}
