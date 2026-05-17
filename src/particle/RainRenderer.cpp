#include "RainRenderer.h"

#include <random>
#include <glm/gtc/random.hpp>

#include "../resource/ResourceMgr.h"
#include "../renderer/Shader.h"

static std::mt19937 s_rng{42};
static constexpr int PRECIP_ATLAS_COLUMNS = 64;

void RainRenderer::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("rain");
    m_rainTex = resourceMgr.getTexture2D("rain");
    m_snowTex = resourceMgr.getTexture2D("snow");
    if (!m_shader) return;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // 6 vertices per drop (2 tris), 5 floats per vertex (xyz + uv)
    int maxVerts = std::max(MAX_RAIN_DROPS, MAX_SNOW_DROPS) * 6 * 5;
    glBufferData(GL_ARRAY_BUFFER, maxVerts * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

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

void RainRenderer::ensureDrops(std::vector<PrecipDrop>& drops, int maxDrops) {
    if (drops.empty()) {
        drops.reserve(maxDrops);
        std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
        std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> distHeight(0.0f, SPAWN_HEIGHT);
        std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
        std::uniform_int_distribution<int> distCol(0, PRECIP_ATLAS_COLUMNS - 1);

        for (int i = 0; i < maxDrops; ++i) {
            PrecipDrop d;
            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.offset = glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = distSpeed(s_rng); // multiplier, base speed applied at render
            d.length = distSpeed(s_rng); // multiplier, base length applied at render
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
            drops.push_back(d);
        }
    }
}

void RainRenderer::updateDrops(std::vector<PrecipDrop>& drops, float dt, float baseSpeed) {
    m_time += dt;
    if (m_time > 628.0f) m_time -= 628.0f;  // wrap at ~100*PI to prevent float precision loss
    for (auto& d : drops) {
        d.offset.y -= d.speed * baseSpeed * dt;

        // Recycle drops that fell below despawn threshold.
        if (d.offset.y < DESPAWN_BELOW) {
            std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
            std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
            std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
            std::uniform_int_distribution<int> distCol(0, PRECIP_ATLAS_COLUMNS - 1);

            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.offset.x = r * cosf(a);
            d.offset.z = r * sinf(a);
            d.offset.y = SPAWN_HEIGHT;
            d.speed = distSpeed(s_rng);
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
        }
    }
}

void RainRenderer::renderPrecipitation(const glm::mat4& projection,
                                        const glm::mat4& view,
                                        const glm::vec3& cameraPos,
                                        GLuint texture,
                                        std::vector<PrecipDrop>& drops,
                                        float strength,
                                        float skyLightAtCamera,
                                        float baseSpeed,
                                        float dropLength,
                                        float streakWidth,
                                        float alphaScale,
                                        const glm::vec3& color,
                                        float dt) {
    if (!m_shader || texture == 0 || strength < 0.01f || skyLightAtCamera < 0.05f) return;

    // Clamp dt to avoid physics explosion on frame hitches.
    dt = std::max(0.001f, std::min(dt, 0.1f));
    updateDrops(drops, dt, baseSpeed);

    // Camera-facing billboards: extract right/up from view matrix.
    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up(view[0][1], view[1][1], view[2][1]);

    // DerivativeMain gbuffers_weather.vsh:19-23 — wind animation.
    // windPos = dot(worldPos + cameraPosition, vec3(2.0))
    // wind = fma(sin(windPos + frameTimeCounter * 0.1), 0.25, 0.25)
    // worldPos.xz -= worldPos.y * wind * vec2(cos(windAngle), sin(windAngle))
    // Simplified for camera-relative particles: tilt fall direction with time.
    float windPhase = m_time * 0.1f;
    float wind = sin(windPhase) * 0.25f + 0.25f;  // oscillates [0, 0.5]
    const float windAngle = 3.14159f / 60.0f;      // PI/60 = 3 degrees (DerivativeMain)
    glm::vec3 fallDir = glm::normalize(glm::vec3(
        -wind * cosf(windAngle),
        -1.0f,
        -wind * sinf(windAngle) * 0.3f  // reduced Z component for subtle effect
    ));

    const int visibleCount = static_cast<int>(drops.size() * strength);

    std::vector<float> vertices;
    vertices.reserve(visibleCount * 6 * 5);

    for (int i = 0; i < visibleCount && i < static_cast<int>(drops.size()); ++i) {
        const auto& d = drops[i];
        glm::vec3 worldPos = cameraPos + d.offset;

        float len = dropLength * d.length;
        glm::vec3 top = worldPos;
        glm::vec3 bot = worldPos + fallDir * len;

        glm::vec3 rOff = right * streakWidth;

        glm::vec3 v0 = top - rOff;
        glm::vec3 v1 = top + rOff;
        glm::vec3 v2 = bot + rOff;
        glm::vec3 v3 = bot - rOff;

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
    m_shader->setFloat("uPrecipStrength", strength * skyLightAtCamera);
    m_shader->setFloat("uPrecipAlphaScale", alphaScale);
    m_shader->setVec3("uPrecipColor", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    m_shader->setInt("uPrecipTex", 0);

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

void RainRenderer::render(const glm::mat4& projection,
                           const glm::mat4& view,
                           const glm::vec3& cameraPos,
                           float rainStrength,
                           float skyLightAtCamera,
                           float alphaScale,
                           float dt) {
    ensureDrops(m_rainDrops, MAX_RAIN_DROPS);
    renderPrecipitation(projection, view, cameraPos,
                        m_rainTex, m_rainDrops,
                        rainStrength, skyLightAtCamera,
                        RAIN_FALL_SPEED, RAIN_DROP_LENGTH,
                        0.035f,  // streakWidth: wider for visibility
                        alphaScale,
                        glm::vec3(0.72f, 0.78f, 0.85f), // rain blue-gray
                        dt);
}

void RainRenderer::renderSnow(const glm::mat4& projection,
                               const glm::mat4& view,
                               const glm::vec3& cameraPos,
                               float snowStrength,
                               float skyLightAtCamera,
                               float alphaScale,
                               float dt) {
    ensureDrops(m_snowDrops, MAX_SNOW_DROPS);
    renderPrecipitation(projection, view, cameraPos,
                        m_snowTex, m_snowDrops,
                        snowStrength, skyLightAtCamera,
                        SNOW_FALL_SPEED, SNOW_DROP_LENGTH,
                        0.025f,  // streakWidth: snow flakes are shorter
                        alphaScale,
                        glm::vec3(0.92f, 0.95f, 1.0f), // snow white
                        dt);
}

void RainRenderer::renderWeatherMask(const glm::mat4& projection,
                                      const glm::mat4& view,
                                      const glm::vec3& cameraPos,
                                      float rainStrength,
                                      float snowStrength,
                                      float skyLightAtCamera,
                                      float alphaScale,
                                      float dt) {
    if (!m_shader) return;

    // Ensure drop pools are initialized.
    if (rainStrength > 0.01f) ensureDrops(m_rainDrops, MAX_RAIN_DROPS);
    if (snowStrength > 0.01f) ensureDrops(m_snowDrops, MAX_SNOW_DROPS);

    // Clamp dt to avoid physics explosion on frame hitches.
    dt = std::max(0.001f, std::min(dt, 0.1f));

    // Camera-facing billboards: extract right/up from view matrix.
    glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    glm::vec3 fallDir = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));

    auto buildVertices = [&](std::vector<PrecipDrop>& drops, float strength,
                              float baseSpeed, float dropLength, float streakWidth,
                              GLuint texture) -> std::vector<float> {
        if (strength < 0.01f || skyLightAtCamera < 0.05f || texture == 0) return {};

        updateDrops(drops, dt, baseSpeed);
        const int visibleCount = static_cast<int>(drops.size() * strength);
        std::vector<float> vertices;
        vertices.reserve(visibleCount * 6 * 5);

        for (int i = 0; i < visibleCount && i < static_cast<int>(drops.size()); ++i) {
            const auto& d = drops[i];
            glm::vec3 worldPos = cameraPos + d.offset;
            float len = dropLength * d.length;
            glm::vec3 top = worldPos;
            glm::vec3 bot = worldPos + fallDir * len;
            glm::vec3 rOff = right * streakWidth;
            glm::vec3 v0 = top - rOff, v1 = top + rOff, v2 = bot + rOff, v3 = bot - rOff;
            float u = d.texU;
            vertices.push_back(v0.x); vertices.push_back(v0.y); vertices.push_back(v0.z); vertices.push_back(u); vertices.push_back(0.0f);
            vertices.push_back(v1.x); vertices.push_back(v1.y); vertices.push_back(v1.z); vertices.push_back(u); vertices.push_back(0.0f);
            vertices.push_back(v2.x); vertices.push_back(v2.y); vertices.push_back(v2.z); vertices.push_back(u); vertices.push_back(1.0f);
            vertices.push_back(v0.x); vertices.push_back(v0.y); vertices.push_back(v0.z); vertices.push_back(u); vertices.push_back(0.0f);
            vertices.push_back(v2.x); vertices.push_back(v2.y); vertices.push_back(v2.z); vertices.push_back(u); vertices.push_back(1.0f);
            vertices.push_back(v3.x); vertices.push_back(v3.y); vertices.push_back(v3.z); vertices.push_back(u); vertices.push_back(1.0f);
        }
        return vertices;
    };

    // DerivativeMain gbuffers_weather: additive blending (ONE ONE ONE ONE)
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    m_shader->setVec3("uPrecipColor", glm::vec3(1.0f)); // unused for mask, but required by shader

    // Render rain to weather mask
    {
        auto verts = buildVertices(m_rainDrops, rainStrength,
                                   RAIN_FALL_SPEED, RAIN_DROP_LENGTH, 0.035f,
                                   m_rainTex);
        if (!verts.empty()) {
            m_shader->setFloat("uPrecipStrength", rainStrength * skyLightAtCamera);
            m_shader->setFloat("uPrecipAlphaScale", alphaScale);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_rainTex);
            m_shader->setInt("uPrecipTex", 0);

            glBindVertexArray(m_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                            verts.data());
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 5));
        }
    }

    // Render snow to weather mask
    {
        auto verts = buildVertices(m_snowDrops, snowStrength,
                                   SNOW_FALL_SPEED, SNOW_DROP_LENGTH, 0.025f,
                                   m_snowTex);
        if (!verts.empty()) {
            m_shader->setFloat("uPrecipStrength", snowStrength * skyLightAtCamera);
            m_shader->setFloat("uPrecipAlphaScale", alphaScale * 0.6f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_snowTex);
            m_shader->setInt("uPrecipTex", 0);

            glBindVertexArray(m_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                            verts.data());
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 5));
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
