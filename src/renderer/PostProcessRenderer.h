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
    float bloomThreshold = 0.0f;    // DerivativeMain: no threshold, raw HDR feeds bloom
    float bloomStrength = 1.0f;
    bool autoExposureEnabled = true;
    float autoExposureMin = 0.001f; // Allow very bright scenes
    float autoExposureMax = 64.0f;  // Allow very dark scenes
    float autoExposureSpeed = 1.0f; // DerivativeMain EXPOSURE_SPEED
    float autoExposureBias = 0.0f;
    float autoExposureDayFactor = 1.0f;
    bool sunRaysEnabled = false;
    glm::vec2 sunScreenPos = glm::vec2(0.5f);
    float sunVisibility = 0.0f;
    float sunRayStrength = 0.18f;
    bool shaderpackGradingEnabled = true;
    int tonemapMode = 1;
    float colorTemperature = 1.0f;
    float vibrance = 0.0f;
    float kappaGradingStrength = 0.0f;
    float highlightCompression = 0.0f;
    float filmEmulationStrength = 0.0f;
    float redModifierStrength = 0.35f;
    glm::vec3 colorLuma = glm::vec3(1.02f, 1.0f, 0.96f);
    float splitToneStrength = 0.0f;
    float vignetteStrength = 0.0f;
    float noiseDitherStrength = 0.015f;
    float sharpenStrength = 0.3f; // DerivativeMain CAS_STRENGTH
    float exposure = 12.0f;   // DerivativeMain MANUAL_EXPOSURE_VALUE
    float gamma = 1.0f;
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
    static constexpr int kBloomMipCount = 7;
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
