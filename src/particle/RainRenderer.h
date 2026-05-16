#ifndef MECRAFT_RAINRENDERER_H
#define MECRAFT_RAINRENDERER_H

#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

class Shader;
class ResourceMgr;

// Procedural rain streak renderer.
// Spawns camera-relative rain lines in a cylinder, renders as alpha-blended
// camera-facing quads. No texture needed — procedural white-to-transparent gradient.
class RainRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Render rain around the given camera position.
    // rainStrength [0,1] controls density and opacity.
    // skyLightAtCamera is the sky light level at the camera (0=indoors, 1=outdoors).
    // dt is the real frame delta time in seconds.
    void render(const glm::mat4& projection,
                const glm::mat4& view,
                const glm::vec3& cameraPos,
                float rainStrength,
                float skyLightAtCamera,
                float dt);

private:
    struct RainDrop {
        glm::vec3 offset;   // relative to camera (xz = horizontal offset, y = height above camera)
        float speed;        // fall speed (units/sec)
        float length;       // streak length
        float alpha;        // base opacity
    };

    void ensureDrops();
    void updateDrops(float dt, float rainStrength);

    Shader* m_shader = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    std::vector<RainDrop> m_drops;

    static constexpr int MAX_DROPS = 4000;
    static constexpr float SPAWN_RADIUS = 24.0f;
    static constexpr float SPAWN_HEIGHT = 20.0f;
    static constexpr float DESPAWN_BELOW = -8.0f;
    static constexpr float BASE_FALL_SPEED = 18.0f;
    static constexpr float DROP_LENGTH = 1.2f;
};

#endif // MECRAFT_RAINRENDERER_H
