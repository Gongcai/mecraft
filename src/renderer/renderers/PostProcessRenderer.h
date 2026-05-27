#ifndef MECRAFT_POSTPROCESSRENDERER_H
#define MECRAFT_POSTPROCESSRENDERER_H

#include <glad/glad.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Use PostProcessEffects from PostProcessPass.h (single source of truth)
#include "../passes/PostProcessPass.h"

class ResourceMgr;
class Shader;
class Window;

class PostProcessRenderer {
public:
    ~PostProcessRenderer();

    void init(ResourceMgr& resourceMgr);
    void shutdown();

    // Start capturing world-space rendering into an off-screen scene target.
    void beginScene(const Window& window);

    // Composite captured scene to back buffer with active effects.
    // gbufDepthTex is the GBuffer depth texture for sky-pixel rain masking.
    void endSceneAndComposite(const Window& window, float frameTime,
                              GLuint gbufDepthTex = 0,
                              GLuint weatherMaskTex = 0);

    // Blit captured scene directly to back buffer without any postprocessing.
    // Used when deferredLightDebugMode > 0 to show raw lighting debug output.
    void blitSceneToBackbuffer(const Window& window);

    void setEffects(const PostProcessEffects& effects);

    // Debug accessors for exposure diagnostics
    [[nodiscard]] float getAdaptedExposure() const { return m_adaptedExposure; }
    [[nodiscard]] float getAverageLuminance() const { return m_lastAverageLum; }
    [[nodiscard]] float getTargetExposure() const { return m_lastTargetExposure; }

private:
    static constexpr int kBloomMipCount = 7;
    static constexpr int kExposureMipCount = 13;
    static constexpr int kAutoExposureLod = 6; // DerivativeMain AUTO_EXPOSURE_LOD

    bool ensureRenderTargets(int width, int height);
    void destroyRenderTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();
    float updateAutoExposure(float frameTime);

    Shader* m_postProcessShader = nullptr;
    Shader* m_bloomExtractShader = nullptr;
    Shader* m_bloomBlurShader = nullptr;
    Shader* m_exposureDownsampleShader = nullptr;
    Shader* m_blitShader = nullptr;
    GLuint m_noiseTexture = 0;

    GLuint m_sceneFbo = 0;
    GLuint m_sceneColorTex = 0;
    GLuint m_sceneDepthTex = 0;
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
    float m_lastAverageLum = 0.0f;
    float m_lastTargetExposure = 1.0f;

    PostProcessEffects m_effects{};
};

#endif //MECRAFT_POSTPROCESSRENDERER_H
