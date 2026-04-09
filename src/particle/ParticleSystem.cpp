#include "ParticleSystem.h"
#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"
#include "../world/Block.h"

#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <algorithm>

namespace {
std::random_device rd;
std::mt19937 gen(rd());

float randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}
}

void ParticleSystem::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("particle");
    m_atlas = &resourceMgr.getAtlas();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Pre-allocate: MAX_PARTICLES * 6 vertices * 6 floats (pos3 + uv2 + alpha1)
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);

    // UV (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    // Alpha (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));

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

void ParticleSystem::emit(const glm::ivec3& blockPos, BlockID blockType) {
    const BlockDef& blockDef = BlockRegistry::get(blockType);

    int texIndices[] = {
        blockDef.texTop, blockDef.texBottom,
        blockDef.texLeft, blockDef.texRight,
        blockDef.texFront, blockDef.texBack
    };

    for (int i = 0; i < PARTICLES_PER_BREAK; ++i) {
        // Remove oldest if at capacity
        if (m_particles.size() >= static_cast<size_t>(MAX_PARTICLES)) {
            m_particles.erase(m_particles.begin());
        }

        // Random face texture
        int texIdx = texIndices[static_cast<int>(randomFloat(0, 6))];
        auto [uvMin, uvMax] = m_atlas->getUV(texIdx);

        // Sub-region of the tile for particle (like MC samples a small area)
        float uRange = uvMax.x - uvMin.x;
        float vRange = uvMax.y - uvMin.y;
        float uSubMin = uvMin.x + randomFloat(0.0f, 0.5f) * uRange;
        float vSubMin = uvMin.y + randomFloat(0.0f, 0.5f) * vRange;
        float uSubMax = uSubMin + 0.5f * uRange;
        float vSubMax = vSubMin + 0.5f * vRange;

        Particle p;
        // Spawn within block volume
        p.position = glm::vec3(blockPos) + glm::vec3(
            randomFloat(0.2f, 0.8f),
            randomFloat(0.2f, 0.8f),
            randomFloat(0.2f, 0.8f)
        );
        // Random velocity with upward bias
        p.velocity = glm::vec3(
            randomFloat(-2.0f, 2.0f),
            randomFloat(0.5f, 3.5f),
            randomFloat(-2.0f, 2.0f)
        );
        p.maxLife = randomFloat(0.4f, 0.8f);
        p.life = p.maxLife;
        p.size = randomFloat(0.06f, 0.14f);
        p.uvMin = glm::vec2(uSubMin, vSubMin);
        p.uvMax = glm::vec2(uSubMax, vSubMax);

        m_particles.push_back(p);
    }
}

void ParticleSystem::update(float dt) {
    constexpr float gravity = -14.0f;

    for (auto& p : m_particles) {
        p.life -= dt;
        if (p.life > 0.0f) {
            p.velocity.y += gravity * dt;
            p.position += p.velocity * dt;
        }
    }

    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end());
}

void ParticleSystem::render(const glm::mat4& projection, const glm::mat4& view) {
    if (m_particles.empty() || m_shader == nullptr || m_atlas == nullptr) {
        return;
    }

    // Extract camera right and up vectors from view matrix
    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    // Build vertex data on CPU
    std::vector<float> vertices;
    vertices.reserve(m_particles.size() * 36);

    for (const auto& p : m_particles) {
        float alpha = p.life / p.maxLife;
        float halfSize = p.size * 0.5f;

        // Billboard quad corners
        glm::vec3 c0 = p.position - right * halfSize - up * halfSize; // bottom-left
        glm::vec3 c1 = p.position + right * halfSize - up * halfSize; // bottom-right
        glm::vec3 c2 = p.position + right * halfSize + up * halfSize; // top-right
        glm::vec3 c3 = p.position - right * halfSize + up * halfSize; // top-left

        // Triangle 1: c0, c1, c2
        // c0
        vertices.push_back(c0.x); vertices.push_back(c0.y); vertices.push_back(c0.z);
        vertices.push_back(p.uvMin.x); vertices.push_back(p.uvMin.y);
        vertices.push_back(alpha);
        // c1
        vertices.push_back(c1.x); vertices.push_back(c1.y); vertices.push_back(c1.z);
        vertices.push_back(p.uvMax.x); vertices.push_back(p.uvMin.y);
        vertices.push_back(alpha);
        // c2
        vertices.push_back(c2.x); vertices.push_back(c2.y); vertices.push_back(c2.z);
        vertices.push_back(p.uvMax.x); vertices.push_back(p.uvMax.y);
        vertices.push_back(alpha);

        // Triangle 2: c0, c2, c3
        // c0
        vertices.push_back(c0.x); vertices.push_back(c0.y); vertices.push_back(c0.z);
        vertices.push_back(p.uvMin.x); vertices.push_back(p.uvMin.y);
        vertices.push_back(alpha);
        // c2
        vertices.push_back(c2.x); vertices.push_back(c2.y); vertices.push_back(c2.z);
        vertices.push_back(p.uvMax.x); vertices.push_back(p.uvMax.y);
        vertices.push_back(alpha);
        // c3
        vertices.push_back(c3.x); vertices.push_back(c3.y); vertices.push_back(c3.z);
        vertices.push_back(p.uvMin.x); vertices.push_back(p.uvMax.y);
        vertices.push_back(alpha);
    }

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    m_shader->setInt("texAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlas->textureID);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 6));

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
