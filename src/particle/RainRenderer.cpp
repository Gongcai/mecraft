#include "RainRenderer.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <glm/gtc/random.hpp>

#include "../resource/ResourceMgr.h"
#include "../renderer/core/Shader.h"
#include "../renderer/rhi/gl/GlRhiTextureRegistry.h"

static std::mt19937 s_rng{42};
static constexpr int PRECIP_ATLAS_COLUMNS = 64;

void RainRenderer::init(ResourceMgr& resourceMgr) {
    m_shader = resourceMgr.getShader("rain");
    m_rainTex = resourceMgr.getTexture2DHandle("rain");
    m_snowTex = resourceMgr.getTexture2DHandle("snow");
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
    m_rainTex = {};
    m_snowTex = {};
}

void RainRenderer::prepareFrame(const glm::vec3& cameraPos,
                                const glm::mat4& view,
                                const float rainStrength,
                                const float snowStrength,
                                float dt) {
    dt = std::clamp(dt, 0.001f, 0.1f);
    if (rainStrength > 0.01f) {
        ensureDrops(m_rainDrops, MAX_RAIN_DROPS, cameraPos);
        updateDrops(m_rainDrops, dt, RAIN_FALL_SPEED, cameraPos);
        buildVertices(m_rainDrops, rainStrength, RAIN_DROP_LENGTH, 0.006f,
                      true, view, m_rainVertices);
    } else {
        m_rainVertices.clear();
    }
    if (snowStrength > 0.01f) {
        ensureDrops(m_snowDrops, MAX_SNOW_DROPS, cameraPos);
        updateDrops(m_snowDrops, dt, SNOW_FALL_SPEED, cameraPos);
        buildVertices(m_snowDrops, snowStrength, SNOW_DROP_LENGTH, 0.025f,
                      false, view, m_snowVertices);
    } else {
        m_snowVertices.clear();
    }
}

void RainRenderer::ensureDrops(std::vector<PrecipDrop>& drops, int maxDrops, const glm::vec3& cameraPos) {
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
            d.position = cameraPos + glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = distSpeed(s_rng); // multiplier, base speed applied at render
            d.length = distSpeed(s_rng); // multiplier, base length applied at render
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
            drops.push_back(d);
        }
    }
}

void RainRenderer::updateDrops(std::vector<PrecipDrop>& drops, float dt, float baseSpeed, const glm::vec3& cameraPos) {
    m_time += dt;
    if (m_time > 628.0f) m_time -= 628.0f;  // wrap at ~100*PI to prevent float precision loss
    for (auto& d : drops) {
        d.position.y -= d.speed * baseSpeed * dt;
    }
    wrapDrops(drops, cameraPos);
}

void RainRenderer::wrapDrops(std::vector<PrecipDrop>& drops, const glm::vec3& cameraPos) {
    std::uniform_real_distribution<float> distRadius(0.0f, SPAWN_RADIUS);
    std::uniform_real_distribution<float> distAngle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> distHeight(0.0f, SPAWN_HEIGHT);
    std::uniform_real_distribution<float> distSpeed(0.8f, 1.2f);
    std::uniform_int_distribution<int> distCol(0, PRECIP_ATLAS_COLUMNS - 1);

    for (auto& d : drops) {
        const glm::vec2 toCamera = glm::vec2(d.position.x - cameraPos.x, d.position.z - cameraPos.z);
        const bool outsideCylinder = glm::dot(toCamera, toCamera) > SPAWN_RADIUS * SPAWN_RADIUS;
        const bool belowCamera = d.position.y < cameraPos.y + DESPAWN_BELOW;
        const bool farAboveCamera = d.position.y > cameraPos.y + SPAWN_HEIGHT + 4.0f;
        if (outsideCylinder || belowCamera || farAboveCamera) {
            float r = distRadius(s_rng);
            float a = distAngle(s_rng);
            d.position = cameraPos + glm::vec3(r * cosf(a), distHeight(s_rng), r * sinf(a));
            d.speed = distSpeed(s_rng);
            d.texU = (distCol(s_rng) + 0.5f) / static_cast<float>(PRECIP_ATLAS_COLUMNS);
        }
    }
}

void RainRenderer::buildVertices(const std::vector<PrecipDrop>& drops,
                                 const float strength,
                                 const float dropLength,
                                 const float streakWidth,
                                 const bool proceduralLines,
                                 const glm::mat4& view,
                                 std::vector<float>& vertices) const {
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    const float wind = std::sin(m_time * 0.1f) * 0.25f + 0.25f;
    constexpr float kWindAngle = 3.14159f / 60.0f;
    const glm::vec3 fallDir = glm::normalize(glm::vec3(
        -wind * std::cos(kWindAngle),
        -1.0f,
        -wind * std::sin(kWindAngle) * 0.3f));
    const int visibleCount = std::min(static_cast<int>(drops.size()),
                                      static_cast<int>(drops.size() * strength));

    vertices.clear();
    vertices.reserve(static_cast<size_t>(visibleCount) * 6u * 5u);
    for (int i = 0; i < visibleCount; ++i) {
        const PrecipDrop& drop = drops[static_cast<size_t>(i)];
        const glm::vec3 top = drop.position;
        const glm::vec3 bottom = top + fallDir * (dropLength * drop.length);
        const glm::vec3 rightOffset = right * streakWidth;
        const glm::vec3 corners[4] = {
            top - rightOffset,
            top + rightOffset,
            bottom + rightOffset,
            bottom - rightOffset
        };
        const float u0 = proceduralLines ? 0.0f : drop.texU;
        const float u1 = proceduralLines ? 1.0f : drop.texU;
        const int cornerIndices[6] = {0, 1, 2, 0, 2, 3};
        const float u[4] = {u0, u1, u1, u0};
        const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        for (const int cornerIndex : cornerIndices) {
            const glm::vec3& position = corners[cornerIndex];
            vertices.insert(vertices.end(), {position.x, position.y, position.z,
                                             u[cornerIndex], v[cornerIndex]});
        }
    }
}

void RainRenderer::renderPrecipitation(const glm::mat4& projection,
                                        const glm::mat4& view,
                                        RhiTextureHandle texture,
                                        const std::vector<float>& vertices,
                                        float strength,
                                        float skyLightAtCamera,
                                        float alphaScale,
                                        const glm::vec3& color,
                                        bool proceduralLines,
                                        const RhiTextureHandle sceneDepthTexture,
                                        const glm::vec2& screenSize,
                                        bool hardwareDepthTest) {
    const uint32_t textureId = renderer::rhi::gl::textureId(texture);
    if (!m_shader || textureId == 0 || strength < 0.01f || skyLightAtCamera < 0.05f) return;
    const uint32_t sceneDepthTex = renderer::rhi::gl::textureId(sceneDepthTexture);

    if (vertices.empty()) return;

    m_shader->use();
    m_shader->setMat4("viewProj", projection * view);
    m_shader->setFloat("uPrecipStrength", strength * skyLightAtCamera);
    m_shader->setFloat("uPrecipAlphaScale", alphaScale);
    m_shader->setVec3("uPrecipColor", color);
    m_shader->setInt("uMaskPass", 0);
    m_shader->setInt("uProceduralLines", proceduralLines ? 1 : 0);
    m_shader->setInt("uDepthFadeEnabled", sceneDepthTex != 0 ? 1 : 0);
    m_shader->setVec2("uScreenSize", screenSize);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    m_shader->setInt("uPrecipTex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    m_shader->setInt("uSceneDepthTex", 1);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                    vertices.data());

    GLint previousDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (hardwareDepthTest) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 5));

    glDepthFunc(static_cast<GLenum>(previousDepthFunc));
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void RainRenderer::render(const glm::mat4& projection,
                           const glm::mat4& view,
                           float rainStrength,
                           float skyLightAtCamera,
                           float alphaScale,
                           const RhiTextureHandle sceneDepthTexture,
                           const glm::vec2& screenSize,
                           bool hardwareDepthTest) {
    renderPrecipitation(projection, view,
                        m_rainTex, m_rainVertices,
                        rainStrength, skyLightAtCamera,
                        alphaScale,
                        glm::vec3(0.72f, 0.78f, 0.85f), // rain blue-gray
                        true,
                        sceneDepthTexture,
                        screenSize,
                        hardwareDepthTest);
}

void RainRenderer::renderSnow(const glm::mat4& projection,
                               const glm::mat4& view,
                               float snowStrength,
                               float skyLightAtCamera,
                               float alphaScale,
                               const RhiTextureHandle sceneDepthTexture,
                               const glm::vec2& screenSize,
                               bool hardwareDepthTest) {
    renderPrecipitation(projection, view,
                        m_snowTex, m_snowVertices,
                        snowStrength, skyLightAtCamera,
                        alphaScale,
                        glm::vec3(0.92f, 0.95f, 1.0f), // snow white
                        false,
                        sceneDepthTexture,
                        screenSize,
                        hardwareDepthTest);
}
