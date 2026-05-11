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
    float bloomStrength = 0.24f;
    bool autoExposureEnabled = true;
    float autoExposureMin = 0.55f;
    float autoExposureMax = 2.05f;
    float autoExposureSpeed = 1.4f;
    float autoExposureBias = 0.0f;
    float autoExposureDayFactor = 1.0f;
    bool sunRaysEnabled = true;
    glm::vec2 sunScreenPos = glm::vec2(0.5f);
    float sunVisibility = 0.0f;
    float sunRayStrength = 0.18f;
    bool shaderpackGradingEnabled = true;
    int tonemapMode = 3;
    float colorTemperature = 1.0f;
    float vibrance = 0.0f;
    float kappaGradingStrength = 0.65f;
    float highlightCompression = 0.72f;
    float filmEmulationStrength = 0.55f;
    float redModifierStrength = 0.35f;
    glm::vec3 colorLuma = glm::vec3(1.02f, 1.0f, 0.96f);
    float splitToneStrength = 0.28f;
    float vignetteStrength = 0.08f;
    float noiseDitherStrength = 0.015f;
    float sharpenStrength = 0.08f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float saturation = 1.0f;
    float contrast = 1.0f;
};

class PostProcessRenderer {
public:
    ~PostProcessRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Start capturing world-space rendering into an off-screen scene target.
    void beginScene(const Window& window);

    // Composite captured scene to back buffer with active effects.
    void endSceneAndComposite(const Window& window, float frameTime);

    void setEffects(const PostProcessEffects& effects);

private:
    static constexpr int kBloomMipCount = 5;
    static constexpr int kExposureMipCount = 13;

    bool ensureRenderTargets(int width, int height);
    void destroyRenderTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();
    float updateAutoExposure(float frameTime);

    Shader* m_postProcessShader = nullptr;
    Shader* m_bloomExtractShader = nullptr;
    Shader* m_bloomBlurShader = nullptr;
    Shader* m_exposureDownsampleShader = nullptr;
    GLuint m_noiseTexture = 0;

    GLuint m_sceneFbo = 0;
    GLuint m_sceneColorTex = 0;
    GLuint m_sceneDepthRbo = 0;
    GLuint m_bloomFbos[kBloomMipCount][2] = {};
    GLuint m_bloomTex[kBloomMipCount][2] = {};
    glm::ivec2 m_bloomMipSize[kBloomMipCount] = {};
    GLuint m_exposureFbos[kExposureMipCount] = {};
    GLuint m_exposureTex[kExposureMipCount] = {};
    glm::ivec2 m_exposureMipSize[kExposureMipCount] = {};
    int m_exposureMipCount = 0;

    GLuint m_fullscreenVao = 0;

    int m_targetWidth = 0;
    int m_targetHeight = 0;
    bool m_sceneCaptured = false;
    bool m_autoExposureInitialized = false;
    float m_adaptedExposure = 1.0f;

    PostProcessEffects m_effects{};
};

#endif //MECRAFT_POSTPROCESSRENDERER_H


