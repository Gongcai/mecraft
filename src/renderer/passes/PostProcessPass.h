#ifndef MECRAFT_POST_PROCESS_PASS_H
#define MECRAFT_POST_PROCESS_PASS_H

#include "RenderPass.h"
#include "../rhi/RhiHandles.h"
#include "../rhi/RhiRenderGraph.h"
#include "../rhi/RhiTypes.h"
#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

class ResourceMgr;
class RhiCommandList;
class RhiCommandListPool;
class RhiDevice;
class RenderDebugService;

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
    // Top-left screen UV position used by the post-process sun-ray integration.
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

    void init(ResourceMgr& resourceMgr, RhiCommandListPool& commandListPool);
    void shutdown() override;
    [[nodiscard]] const char* name() const override { return "PostProcess"; }

    /// Start capturing world-space rendering into an off-screen scene target.
    [[nodiscard]] bool beginSceneCapture(RhiDevice& rhiDevice, int width, int height);

    /// Select the HDR texture produced by temporal reconstruction for post-processing.
    /// @param texture HDR texture sampled by bloom, exposure, and final composition.
    /// @param view Two-dimensional view covering the selected HDR texture.
    /// @param width HDR input width in pixels.
    /// @param height HDR input height in pixels.
    /// @return True when the input is valid and processing targets match its extent.
    [[nodiscard]] bool setHdrInput(RhiTextureHandle texture,
                                   RhiTextureViewHandle view,
                                   int width,
                                   int height);

    /// Composite captured scene to back buffer with active effects.
    [[nodiscard]] bool compositeToBackbuffer(
        RhiDevice& rhiDevice,
        RhiTextureViewHandle swapchainColorView,
        RhiTextureFormat swapchainColorFormat,
        int outputWidth,
        int outputHeight,
        float frameTime,
        RhiTextureHandle gbufferDepthTexture,
        RenderDebugService& debugService);

    /// Composite captured scene into an internal LDR texture instead of the back buffer.
    [[nodiscard]] RhiTextureHandle compositeToTexture(RhiDevice& rhiDevice,
                                                      float frameTime,
                                                      RhiTextureHandle gbufferDepthTexture,
                                                      RenderDebugService& debugService);

    /// Blit captured scene directly to back buffer without any postprocessing.
    [[nodiscard]] bool blitSceneCaptureToBackbuffer(
        RhiDevice& rhiDevice,
        RhiTextureViewHandle swapchainColorView,
        RenderDebugService& debugService);

    /// Blit the internal composited LDR texture to the back buffer.
    [[nodiscard]] bool blitCompositeToBackbuffer(
        RhiDevice& rhiDevice,
        RhiTextureViewHandle swapchainColorView,
        RenderDebugService& debugService);

    /// Set effects configuration for the current frame.
    void setFrameEffects(const PostProcessEffects& effects);

    // Debug accessors for exposure diagnostics
    [[nodiscard]] float getAdaptedExposure() const { return m_adaptedExposure; }
    [[nodiscard]] float getAverageLuminance() const { return m_lastAverageLum; }
    [[nodiscard]] float getTargetExposure() const { return m_lastTargetExposure; }
    [[nodiscard]] bool isAutoExposureGpuResolved() const {
        return m_effects.autoExposureEnabled && m_autoExposureInitialized;
    }
    [[nodiscard]] int targetWidth() const { return m_processingWidth; }
    [[nodiscard]] int targetHeight() const { return m_processingHeight; }
    [[nodiscard]] RhiTextureHandle sceneColorTextureHandle() const { return m_sceneColorHandle; }
    [[nodiscard]] RhiTextureHandle sceneDepthTextureHandle() const { return m_sceneDepthHandle; }
    [[nodiscard]] RhiTextureViewHandle sceneColorTextureViewHandle() const { return m_sceneColorView; }
    [[nodiscard]] RhiTextureViewHandle sceneDepthTextureViewHandle() const { return m_sceneDepthView; }
    [[nodiscard]] RhiTextureHandle compositeTextureHandle() const { return m_compositeHandle; }
    [[nodiscard]] RhiTextureViewHandle compositeTextureViewHandle() const { return m_compositeView; }
    /// Expose the current 1x1 HDR exposure state for temporal reconstruction bridges.
    [[nodiscard]] RhiTextureHandle exposureTextureHandle() const {
        return m_exposureStateHandle[m_exposureStateReadIndex];
    }
    [[nodiscard]] RhiTextureViewHandle exposureTextureViewHandle() const {
        return m_exposureStateView[m_exposureStateReadIndex];
    }

private:
    static constexpr int kBloomMipCount = 7;
    static constexpr int kExposureMipCount = 13;
    static constexpr int kAutoExposureLod = 6;
    static constexpr double kAutoExposureSampleIntervalSeconds = 0.25;
    enum class CompositeDestination {
        Backbuffer,
        Texture
    };
    struct PostProcessCompositeParams;
    [[nodiscard]] RhiCommandList& beginCommandList(const char* debugName) const;
    void submitCommandList(RhiDevice& rhiDevice,
                           RhiCommandList& commandList,
                           const char* debugName) const;
    bool ensureSceneCaptureTargets(RhiDevice& rhiDevice, int width, int height);
    bool ensureProcessingTargets(RhiDevice& rhiDevice, int width, int height);
    bool ensureCompositeTarget(RhiDevice& rhiDevice, int width, int height);
    bool ensureRhiPipelines(RhiDevice& rhiDevice);
    bool ensureSwapchainCompositePipeline(RhiDevice& rhiDevice,
                                          RhiTextureFormat colorFormat);
    bool ensureNoiseTextureView(RhiDevice& rhiDevice);
    bool ensureGbufferDepthTextureView(RhiDevice& rhiDevice, RhiTextureHandle texture);
    bool rebuildTargetBindGroups();
    bool rebuildCompositeBindGroups();
    void destroySceneCaptureTargets();
    void destroyProcessingTargets();
    void destroyTargetBindGroups();
    void destroyCompositeBindGroups();
    void destroyRhiResources();
    [[nodiscard]] bool executeCompositeGraph(
        RhiDevice& rhiDevice,
        CompositeDestination destination,
        RhiTextureViewHandle outputView,
        int outputWidth,
        int outputHeight,
        float frameTime,
        RenderDebugService& debugService);
    [[nodiscard]] bool executeBlitGraph(
        RhiDevice& rhiDevice,
        RhiTextureHandle sourceTexture,
        RhiTextureViewHandle swapchainColorView,
        RenderDebugService& debugService);
    void recordExposureStateInitialization(RhiCommandList& commandList,
                                           float manualExposure);
    void recordExposureDownsample(RhiCommandList& commandList,
                                  int mip,
                                  const glm::ivec2& sourceSize,
                                  bool sourceIsScene,
                                  int sourceLod);
    void recordExposureResolve(RhiCommandList& commandList,
                               int readIndex,
                               int writeIndex,
                               float elapsedFrameTime,
                               float manualExposure,
                               bool historyAvailable,
                               bool reuseExposure);
    void recordBloomExtract(RhiCommandList& commandList, int mip);
    void recordBloomBlur(RhiCommandList& commandList,
                         int mip,
                         bool horizontal);

    /// Apply final composite (tonemap, color grading, underwater, etc.)
    void renderComposite(RhiCommandList& commandList,
                         RhiPipelineHandle pipeline,
                         int exposureStateIndex);
    void uploadCompositeParams(
        RhiCommandList& commandList,
        const PostProcessCompositeParams& params);
    [[nodiscard]] PostProcessCompositeParams buildCompositeParams(
        bool useAutoExposureTexture, bool hasBloom) const;
    void bindCompositeOutput(RhiCommandList& commandList, int width, int height);
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

    RhiDevice* m_rhiDevice = nullptr;
    RhiCommandListPool* m_commandListPool = nullptr;
    RhiTextureHandle m_noiseTexture;
    RhiTextureHandle m_noiseViewTexture;
    RhiTextureViewHandle m_noiseTextureView;
    RhiTextureHandle m_gbufferDepthViewTexture;
    RhiTextureViewHandle m_gbufferDepthTextureView;

    RhiBufferHandle m_compositeParamsBuffer;
    RhiSamplerHandle m_linearClampSampler;
    RhiSamplerHandle m_nearestClampSampler;
    RhiSamplerHandle m_nearestRepeatSampler;
    RhiBindGroupLayoutHandle m_singleTextureBindGroupLayout;
    RhiBindGroupLayoutHandle m_twoTextureBindGroupLayout;
    RhiBindGroupLayoutHandle m_compositeBindGroupLayout;
    RhiPipelineLayoutHandle m_exposureDownsamplePipelineLayout;
    RhiPipelineLayoutHandle m_exposureResolvePipelineLayout;
    RhiPipelineLayoutHandle m_bloomExtractPipelineLayout;
    RhiPipelineLayoutHandle m_bloomBlurPipelineLayout;
    RhiPipelineLayoutHandle m_compositePipelineLayout;
    RhiShaderHandle m_fullscreenVertexShader;
    RhiShaderHandle m_postProcessFragmentShader;
    RhiShaderHandle m_bloomExtractFragmentShader;
    RhiShaderHandle m_bloomBlurFragmentShader;
    RhiShaderHandle m_exposureDownsampleFragmentShader;
    RhiShaderHandle m_exposureResolveFragmentShader;
    RhiPipelineHandle m_exposureDownsamplePipeline;
    RhiPipelineHandle m_exposureResolvePipeline;
    RhiPipelineHandle m_bloomExtractPipeline;
    RhiPipelineHandle m_bloomBlurPipeline;
    RhiPipelineHandle m_compositeTexturePipeline;
    RhiPipelineHandle m_compositeSwapchainPipeline;
    RhiTextureFormat m_compositeSwapchainFormat = RhiTextureFormat::Undefined;
    RhiBindGroupHandle m_bloomExtractBindGroup;
    RhiBindGroupHandle m_bloomBlurBindGroup[kBloomMipCount][2] = {};
    RhiBindGroupHandle m_exposureDownsampleBindGroup[kExposureMipCount] = {};
    RhiBindGroupHandle m_exposureResolveBindGroup[2] = {};
    RhiBindGroupHandle m_compositeBindGroup[2] = {};

    // Scene capture textures
    RhiTextureHandle m_sceneColorHandle;
    RhiTextureViewHandle m_sceneColorView;
    RhiTextureHandle m_sceneDepthHandle;
    RhiTextureViewHandle m_sceneDepthView;
    RhiTextureHandle m_hdrInputHandle;
    RhiTextureViewHandle m_hdrInputView;
    int m_hdrInputWidth = 0;
    int m_hdrInputHeight = 0;

    RhiTextureHandle m_compositeHandle;
    RhiTextureViewHandle m_compositeView;

    // Bloom chain
    RhiTextureHandle m_bloomHandle[kBloomMipCount][2] = {};
    RhiTextureViewHandle m_bloomView[kBloomMipCount][2] = {};
    glm::ivec2 m_bloomMipSize[kBloomMipCount] = {};

    // Auto-exposure downsample chain
    RhiTextureHandle m_exposureHandle[kExposureMipCount] = {};
    RhiTextureViewHandle m_exposureView[kExposureMipCount] = {};
    glm::ivec2 m_exposureMipSize[kExposureMipCount] = {};
    int m_exposureMipCount = 0;
    RhiTextureHandle m_exposureStateHandle[2] = {};
    RhiTextureViewHandle m_exposureStateView[2] = {};
    int m_exposureStateReadIndex = 0;
    double m_autoExposureSampleAccumulator = 0.0;

    int m_captureWidth = 0;
    int m_captureHeight = 0;
    int m_processingWidth = 0;
    int m_processingHeight = 0;
    bool m_sceneCaptured = false;
    bool m_autoExposureInitialized = false;
    float m_adaptedExposure = 1.0f;
    float m_lastAverageLum = 0.0f;
    float m_lastTargetExposure = 1.0f;

    PostProcessEffects m_effects{};
    RenderGraph m_renderGraph;
};

#endif // MECRAFT_POST_PROCESS_PASS_H
