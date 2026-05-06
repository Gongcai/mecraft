#ifndef MECRAFT_SKYBOXRENDERER_H
#define MECRAFT_SKYBOXRENDERER_H

#include <glad/glad.h>

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
    GLuint m_cubemapTexture = 0;
    GLuint m_cubeVao = 0;
    GLuint m_cubeVbo = 0;
    GLuint m_fullscreenVao = 0;

    // Blur pipeline: scene FBO + two ping-pong FBOs at half resolution
    GLuint m_sceneFbo = 0;
    GLuint m_sceneColorTex = 0;
    GLuint m_pingFbo = 0;
    GLuint m_pingColorTex = 0;
    GLuint m_pongFbo = 0;
    GLuint m_pongColorTex = 0;
    int m_blurWidth = 0;
    int m_blurHeight = 0;
};

#endif // MECRAFT_SKYBOXRENDERER_H
