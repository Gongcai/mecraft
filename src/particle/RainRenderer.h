#ifndef MECRAFT_RAINRENDERER_H
#define MECRAFT_RAINRENDERER_H

#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

class Shader;
class ResourceMgr;

// Textured precipitation renderer using vanilla rain.png / snow.png atlas.
// Spawns camera-relative billboard quads in a cylinder, samples streak columns
// from a 64x256 texture atlas (each drop picks a random column).
// Supports both rain and snow with separate drop pools and physics.
class RainRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Render rain around the given camera position.
    void render(const glm::mat4& projection,
                const glm::mat4& view,
                const glm::vec3& cameraPos,
                float rainStrength,
                float skyLightAtCamera,
                float alphaScale,
                GLuint sceneDepthTex,
                const glm::vec2& screenSize,
                float dt);

    // Render snow around the given camera position.
    void renderSnow(const glm::mat4& projection,
                    const glm::mat4& view,
                    const glm::vec3& cameraPos,
                    float snowStrength,
                    float skyLightAtCamera,
                    float alphaScale,
                    GLuint sceneDepthTex,
                    const glm::vec2& screenSize,
                    float dt);

    // Render weather mask to the currently bound FBO with additive blending.
    // Equivalent to DerivativeMain gbuffers_weather: writes particle alpha to a single channel.
    // Caller must bind the weather mask FBO before calling.
    void renderWeatherMask(const glm::mat4& projection,
                           const glm::mat4& view,
                           const glm::vec3& cameraPos,
                           float rainStrength,
                           float snowStrength,
                           float skyLightAtCamera,
                           float alphaScale,
                           float dt);

private:
    struct PrecipDrop {
        glm::vec3 position; // world-space position; wrapped around the camera when out of range
        float speed;        // fall speed (units/sec)
        float length;       // streak length
        float texU;         // random column in rain atlas [0,1]
    };

    void ensureDrops(std::vector<PrecipDrop>& drops, int maxDrops, const glm::vec3& cameraPos);
    void updateDrops(std::vector<PrecipDrop>& drops, float dt, float baseSpeed, const glm::vec3& cameraPos);
    void wrapDrops(std::vector<PrecipDrop>& drops, const glm::vec3& cameraPos);
    void renderPrecipitation(const glm::mat4& projection,
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
                             bool proceduralLines,
                             GLuint sceneDepthTex,
                             const glm::vec2& screenSize,
                             float dt);

    Shader* m_shader = nullptr;
    GLuint m_rainTex = 0;
    GLuint m_snowTex = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    float m_time = 0.0f;  // accumulated time for wind animation

    std::vector<PrecipDrop> m_rainDrops;
    std::vector<PrecipDrop> m_snowDrops;

    static constexpr int MAX_RAIN_DROPS = 4000;
    static constexpr int MAX_SNOW_DROPS = 2500;
    static constexpr float SPAWN_RADIUS = 24.0f;
    static constexpr float SPAWN_HEIGHT = 20.0f;
    static constexpr float DESPAWN_BELOW = -8.0f;
    static constexpr float RAIN_FALL_SPEED = 18.0f;
    static constexpr float SNOW_FALL_SPEED = 6.0f;
    static constexpr float RAIN_DROP_LENGTH = 1.2f;
    static constexpr float SNOW_DROP_LENGTH = 0.4f;
};

#endif // MECRAFT_RAINRENDERER_H
