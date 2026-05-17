#include "RainRenderer.h"

#include <random>
#include <glm/gtc/random.hpp>

#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"

static std::mt19937 s_rng{42};
static constexpr int RAIN_ATLAS_COLUMNS = 64;

void RainRenderer::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("rain");
    m_rainTex = resourceMgr.getTexture2D("rain");
    if (!m_shader || m_rainTex == 0) return;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // 6 vertices per drop (2 tris), 5 floats per vertex (xyz + uv)
    glBufferData(GL_ARRAY_BUFFER, MAX_DROPS * 6 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // Position: location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    // UV: location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

void RainRenderer::shutdown() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
}

void RainRenderer::ensureDrops() {
    if (m_drops.empty()) {
        m_drops.reserve(MAX_DROPS);
        std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
        std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> distHeight(0.0f, SPAWN_HEIGHT);
        std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
        std::uniform_int_distribution<int> distCol(0, RAIN_ATLAS_COLUMNS - 1);

        for (int i = 0; i < MAX_DROPS; ++i) {
            RainDrop d;
            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.offset = glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = BASE_FALL_SPEED * distSpeed(s_rng);
            d.length = DROP_LENGTH * distSpeed(s_rng);
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(RAIN_ATLAS_COLUMNS);
            m_drops.push_back(d);
        }
    }
}

void RainRenderer::updateDrops(float dt) {
    ensureDrops();

    for (auto& d : m_drops) {
        d.offset.y -= d.speed * dt;

        // Recycle drops that fell below despawn threshold.
        if (d.offset.y < DESPAWN_BELOW) {
            std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
            std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
            std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
            std::uniform_int_distribution<int> distCol(0, RAIN_ATLAS_COLUMNS - 1);

            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.offset.x = r * cosf(a);
            d.offset.z = r * sinf(a);
            d.offset.y = SPAWN_HEIGHT;
            d.speed = BASE_FALL_SPEED * distSpeed(s_rng);
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(RAIN_ATLAS_COLUMNS);
        }
    }
}

void RainRenderer::render(const glm::mat4& projection,
                           const glm::mat4& view,
                           const glm::vec3& cameraPos,
                           float rainStrength,
                           float skyLightAtCamera,
                           float dt) {
    if (!m_shader || m_rainTex == 0 || rainStrength < 0.01f || skyLightAtCamera < 0.05f) return;

    // Clamp dt to avoid physics explosion on frame hitches.
    dt = std::max(0.001f, std::min(dt, 0.1f));
    updateDrops(dt);

    // Camera-facing billboards: extract right/up from view matrix.
    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    // Rain falls mostly vertically, with slight wind.
    glm::vec3 fallDir = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));

    const int visibleCount = static_cast<int>(m_drops.size() * rainStrength);
    const float streakWidth = 0.02f;

    std::vector<float> vertices;
    vertices.reserve(visibleCount * 6 * 5);

    for (int i = 0; i < visibleCount && i < MAX_DROPS; ++i) {
        const auto& d = m_drops[i];
        glm::vec3 worldPos = cameraPos + d.offset;

        // Streak: top point and bottom point (offset by fall direction * length)
        glm::vec3 top = worldPos;
        glm::vec3 bot = worldPos + fallDir * d.length;

        // Build a thin camera-facing quad from top to bot.
        glm::vec3 rOff = right * streakWidth;

        glm::vec3 v0 = top - rOff;
        glm::vec3 v1 = top + rOff;
        glm::vec3 v2 = bot + rOff;
        glm::vec3 v3 = bot - rOff;

        // UV: u = random column, v = 0 at top, 1 at bottom
        float u = d.texU;

        // Triangle 1: v0 v1 v2
        vertices.push_back(v0.x); vertices.push_back(v0.y); vertices.push_back(v0.z); vertices.push_back(u); vertices.push_back(0.0f);
        vertices.push_back(v1.x); vertices.push_back(v1.y); vertices.push_back(v1.z); vertices.push_back(u); vertices.push_back(0.0f);
        vertices.push_back(v2.x); vertices.push_back(v2.y); vertices.push_back(v2.z); vertices.push_back(u); vertices.push_back(1.0f);
        // Triangle 2: v0 v2 v3
        vertices.push_back(v0.x); vertices.push_back(v0.y); vertices.push_back(v0.z); vertices.push_back(u); vertices.push_back(0.0f);
        vertices.push_back(v2.x); vertices.push_back(v2.y); vertices.push_back(v2.z); vertices.push_back(u); vertices.push_back(1.0f);
        vertices.push_back(v3.x); vertices.push_back(v3.y); vertices.push_back(v3.z); vertices.push_back(u); vertices.push_back(1.0f);
    }

    if (vertices.empty()) return;

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    // Fold skyLightAtCamera into rainStrength so the texture alpha fades indoors.
    m_shader->setFloat("uRainStrength", rainStrength * skyLightAtCamera);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_rainTex);
    m_shader->setInt("uRainTex", 0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                    vertices.data());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 5));

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
