#ifndef MECRAFT_POSTPROCESSRENDERER_H
#define MECRAFT_POSTPROCESSRENDERER_H

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

class ResourceMgr;
class Shader;
class Window;

struct PostProcessEffects {
    bool underwaterEnabled = false;
    glm::vec3 underwaterTint = glm::vec3(0.24f, 0.46f, 0.72f);
    float underwaterStrength = 0.68f;
    float screenRollRadians = 0.0f;
    bool bloomEnabled = true;
    float bloomThreshold = 0.82f;
    float bloomStrength = 0.18f;
    bool sunRaysEnabled = true;
    glm::vec2 sunScreenPos = glm::vec2(0.5f);
    float sunVisibility = 0.0f;
    float sunRayStrength = 0.14f;
    bool shaderpackGradingEnabled = true;
    int tonemapMode = 1;
    float colorTemperature = 1.0f;
    float vibrance = 0.06f;
    float noiseDitherStrength = 0.015f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float saturation = 1.0f;
    float contrast = 1.10f;
};

class PostProcessRenderer {
public:
    ~PostProcessRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Start capturing world-space rendering into an off-screen scene target.
    void beginScene(const Window& window);

    // Composite captured scene to back buffer with active effects.
    void endSceneAndComposite(const Window& window);

    void setEffects(const PostProcessEffects& effects);

private:
    bool ensureRenderTargets(int width, int height);
    void destroyRenderTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();

    Shader* m_postProcessShader = nullptr;
    Shader* m_bloomExtractShader = nullptr;
    Shader* m_bloomBlurShader = nullptr;
    GLuint m_noiseTexture = 0;

    GLuint m_sceneFbo = 0;
    GLuint m_sceneColorTex = 0;
    GLuint m_sceneDepthRbo = 0;
    GLuint m_bloomFbos[2] = {0, 0};
    GLuint m_bloomTex[2] = {0, 0};

    GLuint m_fullscreenVao = 0;

    int m_targetWidth = 0;
    int m_targetHeight = 0;
    bool m_sceneCaptured = false;

    PostProcessEffects m_effects{};
};

#endif //MECRAFT_POSTPROCESSRENDERER_H


