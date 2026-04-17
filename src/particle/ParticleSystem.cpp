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

    auto& buffer = ecs::ensureParticleEventBuffer(*m_registry);
    buffer.blockBreakEvents.push_back({blockPos, blockType});
}

void ParticleSystem::update(const float dt) {
    static_cast<void>(dt);
}

void ParticleSystem::render(const glm::mat4& projection, const glm::mat4& view) {
    if (m_registry == nullptr || m_shader == nullptr || m_texArray == nullptr) {
        return;
    }

    auto particleView = m_registry->view<ecs::ParticleTag, ecs::TransformComponent, ecs::ParticleComponent>();
    if (particleView.begin() == particleView.end()) {
        return;
    }

    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(MAX_PARTICLES) * 48u);

    int emittedParticles = 0;
    for (const entt::entity e : particleView) {
        const auto& transform = particleView.get<ecs::TransformComponent>(e);
        const auto& particle = particleView.get<ecs::ParticleComponent>(e);
        if (particle.life <= 0.0f || particle.maxLife <= 0.0f) {
            continue;
        }
        if (emittedParticles >= MAX_PARTICLES) {
            break;
        }

        const float alpha = glm::clamp(particle.life / particle.maxLife, 0.0f, 1.0f);
        const float halfSize = particle.size * 0.5f;

        glm::vec3 c0 = transform.position - right * halfSize - up * halfSize;
        glm::vec3 c1 = transform.position + right * halfSize - up * halfSize;
        glm::vec3 c2 = transform.position + right * halfSize + up * halfSize;
        glm::vec3 c3 = transform.position - right * halfSize + up * halfSize;

        vertices.push_back(c0.x); vertices.push_back(c0.y); vertices.push_back(c0.z);
        vertices.push_back(particle.uvMin.x); vertices.push_back(particle.uvMin.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        vertices.push_back(c1.x); vertices.push_back(c1.y); vertices.push_back(c1.z);
        vertices.push_back(particle.uvMax.x); vertices.push_back(particle.uvMin.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        vertices.push_back(c2.x); vertices.push_back(c2.y); vertices.push_back(c2.z);
        vertices.push_back(particle.uvMax.x); vertices.push_back(particle.uvMax.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        vertices.push_back(c0.x); vertices.push_back(c0.y); vertices.push_back(c0.z);
        vertices.push_back(particle.uvMin.x); vertices.push_back(particle.uvMin.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        vertices.push_back(c2.x); vertices.push_back(c2.y); vertices.push_back(c2.z);
        vertices.push_back(particle.uvMax.x); vertices.push_back(particle.uvMax.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        vertices.push_back(c3.x); vertices.push_back(c3.y); vertices.push_back(c3.z);
        vertices.push_back(particle.uvMin.x); vertices.push_back(particle.uvMax.y);
        vertices.push_back(particle.layer);
        vertices.push_back(alpha);
        vertices.push_back(particle.grassTintFactor);

        ++emittedParticles;
    }

    if (vertices.empty()) {
        return;
    }

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    m_shader->setInt("texArray", 0);
    m_shader->setVec3("uGrassTintColor", glm::vec3(0.50f, 0.78f, 0.34f));

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
