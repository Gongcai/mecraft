#ifndef MECRAFT_POST_PROCESS_PASS_H
#define MECRAFT_POST_PROCESS_PASS_H

#include "RenderPass.h"
#include "../rhi/RhiHandles.h"
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class ResourceMgr;
class RhiCommandList;
class RhiDevice;
class Shader;
class Window;

/// Per-frame post-process effects configuration.
struct PostProcessEffects {
    bool underwaterEnabled = false;
    glm::vec3 underwaterTint = glm::vec3(0.24f, 0.46f, 0.72f);
    float underwaterStrength = 0.68f;
    float screenRollRadians = 0.0f;
    bool bloomEnabled = true;
    int bloomMipCount = 5;
    float bloomThreshold = 0.0f;
    float bloomStrength = 1.0f;
    bool autoExposureEnabled = true;
    float autoExposureMin = 0.001f;
    float autoExposureMax = 64.0f;
    float autoExposureSpeed = 1.0f;
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
    float highlightCompression = 0.0f;
    float filmEmulationStrength = 0.0f;
    float redModifierStrength = 0.35f;
    glm::vec3 colorLuma = glm::vec3(1.02f, 1.0f, 0.96f);
    float splitToneStrength = 0.0f;
    float vignetteStrength = 0.0f;
    float noiseDitherStrength = 0.015f;
    float sharpenStrength = 0.3f;
    float exposure = 12.0f;
    float gamma = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool purkinjeShiftEnabled = false;
    bool bloomyFogEnabled = true;
    float weatherWetness = 0.0f;
    float weatherStorm = 0.0f;
    float snowStrength = 0.0f;
    float skyWetness = 0.0f;
    float fogWetness = 0.0f;
    float cloudWetness = 0.0f;
    float cameraRainVisibility = 1.0f;
    float weatherExposureBias = 0.0f;
    float weatherPostRainFog = 1.0f;
    float gameTime = 0.0f; // Game time in seconds for animated effects
    int postprocessDebugMode = 0;
};

/// Shared post-processing pass: bloom, auto-exposure, tonemap, color grading, underwater, etc.
/// Used by both Forward and Deferred pipelines through RenderScene.
class PostProcessPass : public RenderPass {
public:
    ~PostProcessPass() override;

    void init(ResourceMgr& resourceMgr) override;
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "PostProcess"; }

    /// Start capturing world-space rendering into an off-screen scene target.
    void beginSceneCapture(const Window& window);
    void beginSceneCapture(int width, int height);

    /// Composite captured scene to back buffer with active effects.
    void compositeToBackbuffer(RhiDevice& rhiDevice,
                               RhiTextureViewHandle swapchainColorView,
                               const Window& window,
                               float frameTime,
                               uint32_t gbufDepthTex = 0,
                               uint32_t weatherMaskTex = 0);

    /// Composite captured scene into an internal LDR texture instead of the back buffer.
    [[nodiscard]] uint32_t compositeToTexture(const Window& window, float frameTime,
                                              uint32_t gbufDepthTex = 0,
                                              uint32_t weatherMaskTex = 0);

    /// Blit captured scene directly to back buffer without any postprocessing.
    void blitSceneCaptureToBackbuffer(RhiDevice& rhiDevice,
                                      RhiTextureViewHandle swapchainColorView,
                                      const Window& window);

    /// Blit the internal composited LDR texture to the back buffer.
    void blitCompositeToBackbuffer(RhiDevice& rhiDevice,
                                   RhiTextureViewHandle swapchainColorView,
                                   const Window& window);

    /// Set effects configuration for the current frame.
    void setFrameEffects(const PostProcessEffects& effects);

    // Debug accessors for exposure diagnostics
    [[nodiscard]] float getAdaptedExposure() const { return m_adaptedExposure; }
    [[nodiscard]] float getAverageLuminance() const { return m_lastAverageLum; }
    [[nodiscard]] float getTargetExposure() const { return m_lastTargetExposure; }
    [[nodiscard]] bool isAutoExposureGpuResolved() const {
        return m_effects.autoExposureEnabled && m_autoExposureInitialized;
    }
    [[nodiscard]] int targetWidth() const { return m_targetWidth; }
    [[nodiscard]] int targetHeight() const { return m_targetHeight; }

private:
    static constexpr int kBloomMipCount = 7;
    static constexpr int kExposureMipCount = 13;
    static constexpr int kAutoExposureLod = 6;
    static constexpr double kAutoExposureSampleIntervalSeconds = 0.25;
    static constexpr uint32_t kCompositeParamsBinding = 0;

    bool ensureRenderTargets(int width, int height);
    void destroyRenderTargets();
    void initFullscreenTriangle();
    void destroyFullscreenTriangle();
    float updateAutoExposure(float frameTime);
    void initializeExposureState(float manualExposure);

    /// Apply bloom extraction and blur passes.
    /// @return true if bloom was applied
    bool renderBloom(int maxMipCount);

    /// Apply final composite (tonemap, color grading, underwater, etc.)
    void renderComposite(uint32_t gbufDepthTex, uint32_t weatherMaskTex, bool bloomReady);
    void updateCompositeParams(bool useAutoExposureTexture, bool hasBloom);
    bool ensureCompositeTarget(int width, int height);
    void bindCompositeOutput(int width, int height);
    void bindBackbufferOutput(RhiCommandList& commandList,
                              RhiTextureViewHandle swapchainColorView,
                              int width,
                              int height,
                              bool clearColor);

    struct alignas(16) PostProcessCompositeParams {
        glm::ivec4 flags0;
        glm::ivec4 modes0;
        glm::ivec4 flags1;
        glm::vec4 bloomExposure;
        glm::vec4 sunParams;
        glm::vec4 colorParams0;
        glm::vec4 colorParams1;
        glm::vec4 underwaterParams;
        glm::vec4 weatherParams0;
        glm::vec4 weatherParams1;
        glm::vec4 weatherParams2;
    };
    static_assert(sizeof(PostProcessCompositeParams) == 176,
                  "Post-process UBO layout must match the std140 shader block.");

    Shader* m_postProcessShader = nullptr;
    Shader* m_bloomExtractShader = nullptr;
    Shader* m_bloomBlurShader = nullptr;
    Shader* m_exposureDownsampleShader = nullptr;
    Shader* m_exposureResolveShader = nullptr;
    RhiTextureHandle m_noiseTexture;

    // Scene capture FBO
    uint32_t m_sceneFbo = 0;
    uint32_t m_sceneColorTex = 0;
    RhiTextureHandle m_sceneColorHandle;
    uint32_t m_sceneDepthTex = 0;

    uint32_t m_compositeFbo = 0;
    uint32_t m_compositeTex = 0;
    RhiTextureHandle m_compositeHandle;
    bool m_renderCompositeToTexture = false;

    // Bloom chain
    uint32_t m_bloomFbos[kBloomMipCount][2] = {};
    uint32_t m_bloomTex[kBloomMipCount][2] = {};
    glm::ivec2 m_bloomMipSize[kBloomMipCount] = {};

    // Auto-exposure downsample chain
    uint32_t m_exposureFbos[kExposureMipCount] = {};
    uint32_t m_exposureTex[kExposureMipCount] = {};
    glm::ivec2 m_exposureMipSize[kExposureMipCount] = {};
    int m_exposureMipCount = 0;
    uint32_t m_exposureStateFbos[2] = {};
    uint32_t m_exposureStateTex[2] = {};
    int m_exposureStateReadIndex = 0;
    double m_autoExposureSampleAccumulator = 0.0;

    uint32_t m_fullscreenVao = 0;
    uint32_t m_compositeParamsBuffer = 0;

    int m_targetWidth = 0;
    int m_targetHeight = 0;
    bool m_sceneCaptured = false;
    bool m_autoExposureInitialized = false;
    float m_adaptedExposure = 1.0f;
    float m_lastAverageLum = 0.0f;
    float m_lastTargetExposure = 1.0f;

    PostProcessEffects m_effects{};
};

#endif // MECRAFT_POST_PROCESS_PASS_H
