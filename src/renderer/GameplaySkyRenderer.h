#ifndef MECRAFT_GAMEPLAY_SKY_RENDERER_H
#define MECRAFT_GAMEPLAY_SKY_RENDERER_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <utility>

class Camera;
class DayNightSystem;
class ResourceMgr;
class Shader;

class GameplaySkyRenderer {
public:
    struct SkyColors {
        glm::vec3 top = glm::vec3(0.55f, 0.75f, 1.0f);
        glm::vec3 horizon = glm::vec3(0.70f, 0.86f, 1.0f);
        glm::vec3 fog = glm::vec3(0.70f, 0.86f, 1.0f);
        glm::vec4 halo = glm::vec4(1.0f, 0.42f, 0.10f, 0.0f);
        float haloStrength = 0.0f;
    };

    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(const Camera& camera, float aspect, const DayNightSystem& dayNight);

    [[nodiscard]] SkyColors computeSkyColors(const DayNightSystem& dayNight) const;
    [[nodiscard]] glm::vec3 getLastFogColor() const;
    [[nodiscard]] static std::pair<glm::vec2, glm::vec2> getMoonPhaseUv(int phaseIndex);

private:
    struct MoonPhaseUv {
        glm::vec2 uvMin = glm::vec2(0.0f);
        glm::vec2 uvMax = glm::vec2(1.0f);
    };

    struct CloudMeshInfo {
        float tileWorldSize = 0.0f;
        bool valid = false;
    };

    void initMeshes();
    void destroyMeshes();
    void initCloudMesh();
    void renderSkyGradient(const Camera& camera, float aspect, const SkyColors& colors);
    void renderClouds(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors);
    void renderHalo(const Camera& camera, float aspect, const DayNightSystem& dayNight, const SkyColors& colors);
    void renderCelestialBody(const Camera& camera,
                             float aspect,
                             float angleRadians,
                             float size,
                             GLuint texture,
                             const glm::vec2& uvMin,
                             const glm::vec2& uvMax,
                             float alpha);
    [[nodiscard]] glm::mat4 buildSkyView(const Camera& camera) const;
    [[nodiscard]] glm::vec3 directionFromAngle(float angleRadians) const;
    [[nodiscard]] MoonPhaseUv getMoonPhaseUvInternal(int phaseIndex) const;

    Shader* m_shader = nullptr;
    GLuint m_sunTexture = 0;
    GLuint m_moonTexture = 0;

    GLuint m_skyVao = 0;
    GLuint m_skyVbo = 0;
    GLuint m_haloVao = 0;
    GLuint m_haloVbo = 0;
    GLuint m_bodyVao = 0;
    GLuint m_bodyVbo = 0;
    GLuint m_cloudVao = 0;
    GLuint m_cloudVbo = 0;
    GLsizei m_haloVertexCount = 0;
    GLsizei m_bodyVertexCount = 0;
    GLsizei m_cloudVertexCount = 0;
    CloudMeshInfo m_cloudMeshInfo{};

    SkyColors m_lastColors{};
};

#endif // MECRAFT_GAMEPLAY_SKY_RENDERER_H
