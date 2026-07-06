#ifndef MECRAFT_SKYBOXRENDERER_H
#define MECRAFT_SKYBOXRENDERER_H

#include <cstdint>

class ResourceMgr;
class Shader;

class SkyboxRenderer {
public:
    void init(ResourceMgr& resourceMgr);
    void shutdown();
    void render(float aspect, float yawDegrees, float pitchDegrees);

private:
    void initCubeMesh();
    void destroyCubeMesh();
    bool ensureBlurTargets(int width, int height);
    void destroyBlurTargets();

    Shader* m_shader = nullptr;
    Shader* m_blurShader = nullptr;
    uint32_t m_cubemapTexture = 0;
    uint32_t m_cubeVao = 0;
    uint32_t m_cubeVbo = 0;
    uint32_t m_fullscreenVao = 0;

    // Blur pipeline: scene FBO + two ping-pong FBOs at half resolution
    uint32_t m_sceneFbo = 0;
    uint32_t m_sceneColorTex = 0;
    uint32_t m_pingFbo = 0;
    uint32_t m_pingColorTex = 0;
    uint32_t m_pongFbo = 0;
    uint32_t m_pongColorTex = 0;
    int m_blurWidth = 0;
    int m_blurHeight = 0;
};

#endif // MECRAFT_SKYBOXRENDERER_H
