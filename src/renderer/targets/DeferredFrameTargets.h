#ifndef MECRAFT_DEFERRED_FRAME_TARGETS_H
#define MECRAFT_DEFERRED_FRAME_TARGETS_H

#include <glad/glad.h>

/// Deferred pipeline frame targets.
/// These are resources exclusive to the deferred rendering path.
/// Forward pipeline does NOT allocate these (saves ~200-300MB VRAM at 1080p).
class DeferredFrameTargets {
public:
    ~DeferredFrameTargets();

    bool init();
    void shutdown();

    /// Resize targets to match window dimensions
    bool ensureSize(int width, int height, int shadowResolution);

    // GBuffer operations
    void bindGBuffer();
    void attachPerObjectVelocityToGBuffer();
    void detachPerObjectVelocityFromGBuffer();
    void clearPerObjectVelocity();

    // SSAO operations
    void bindSsao();
    void bindSsaoFiltered();
    void bindSsaoTemporal();
    void bindSsaoHalfRes();
    void bindSsaoHalfResFiltered();
    void copySsaoTemporalToHistory();
    void swapSsaoHistory();

    // Scene lighting/composite (deferred-specific HDR buffers)
    void bindSceneLighting();
    void bindSceneComposite();
    void bindSceneResolved();
    void bindTransparentComposite();
    void bindHalfRes();
    void bindReflection();
    void bindReflectionTemporalScratch();
    void bindCloud();
    void bindVolumetricTemporal();
    void bindVelocity();
    void bindWeatherMask();
    void clearWeatherMask();

    // Copy operations
    void copyFramebufferColorToSceneLighting(GLint framebuffer, int width, int height) const;
    void copyFramebufferColorToSceneResolved(GLint framebuffer, int width, int height) const;
    void copyFramebufferColorToTransparentComposite(GLint framebuffer, int width, int height) const;
    void copySceneLightingToTransparentComposite() const;
    void copySceneLightingToSceneComposite() const;
    void copySceneCompositeToSceneResolved() const;
    void copySceneCompositeToTransparentComposite() const;
    void copySceneResolvedToTransparentComposite() const;
    void copyTransparentCompositeToSceneComposite() const;
    void copyTransparentCompositeToSceneResolved() const;
    void copyDepthToTransparentComposite() const;
    void copySceneResolvedToHistory() const;
    void copySceneResolvedToTemporalCurrent() const;
    void copyDepthToHistory() const;
    void copyReflectionToHistory() const;
    void copyReflectionToTemporalScratch() const;
    void copyCloudToHistory() const;
    void copyVolumetricToHistory() const;
    void blitSceneLightingTo(GLint framebuffer, int width, int height) const;
    void blitSceneCompositeTo(GLint framebuffer, int width, int height) const;
    void blitSceneResolvedTo(GLint framebuffer, int width, int height) const;
    void blitTransparentCompositeTo(GLint framebuffer, int width, int height) const;
    void blitDepthTo(GLint framebuffer, int width, int height) const;

    // GBuffer texture accessors
    [[nodiscard]] GLuint albedoTexture() const { return m_gAlbedo; }
    [[nodiscard]] GLuint normalAoTexture() const { return m_gNormalAo; }
    [[nodiscard]] GLuint voxelLightTexture() const { return m_gVoxelLight; }
    [[nodiscard]] GLuint materialTexture() const { return m_gMaterial; }
    [[nodiscard]] GLuint materialAuxTexture() const { return m_gMaterialAux; }
    [[nodiscard]] GLuint depthTexture() const { return m_gDepth; }

    // SSAO texture accessors
    [[nodiscard]] GLuint ssaoTexture() const { return m_ssaoTex; }
    [[nodiscard]] GLuint ssaoFilteredTexture() const { return m_ssaoFilteredTex; }
    [[nodiscard]] GLuint ssaoHalfResTexture() const { return m_ssaoHalfResTex; }
    [[nodiscard]] GLuint ssaoHalfResFilteredTexture() const { return m_ssaoHalfResFilteredTex; }
    [[nodiscard]] GLuint ssaoHistoryTexture() const { return m_ssaoHistoryTex[m_ssaoHistoryIndex]; }
    [[nodiscard]] GLuint ssaoHistoryTexturePrev() const { return m_ssaoHistoryTex[1 - m_ssaoHistoryIndex]; }
    [[nodiscard]] GLuint ssaoTemporalTexture() const { return m_ssaoTemporalTex; }

    // Scene HDR texture accessors
    [[nodiscard]] GLuint sceneLightingTexture() const { return m_sceneLightingTex; }
    [[nodiscard]] GLuint sceneCompositeTexture() const { return m_sceneCompositeTex; }
    [[nodiscard]] GLuint sceneResolvedTexture() const { return m_sceneResolvedTex; }
    [[nodiscard]] GLuint transparentCompositeTexture() const { return m_transparentCompositeTex; }
    [[nodiscard]] GLuint transparentCompositeDepthTexture() const { return m_transparentCompositeDepth; }
    [[nodiscard]] GLuint halfResTexture() const { return m_halfResTex; }
    [[nodiscard]] GLuint reflectionTexture() const { return m_reflectionTex; }
    [[nodiscard]] GLuint reflectionTemporalScratchTexture() const { return m_reflectionTemporalScratchTex; }
    [[nodiscard]] GLuint cloudTexture() const { return m_cloudTex; }
    [[nodiscard]] GLuint velocityTexture() const { return m_velocityTex; }
    [[nodiscard]] GLuint perObjectVelocityTexture() const { return m_perObjectVelocityTex; }
    [[nodiscard]] GLuint weatherMaskTexture() const { return m_weatherMaskTex; }

    // Sky capture
    [[nodiscard]] GLuint skyCaptureFramebuffer() const { return m_skyCaptureFbo; }
    [[nodiscard]] GLuint skyCaptureTexture() const { return m_skyCaptureTex; }
    [[nodiscard]] int skyCaptureWidth() const { return kSkyCaptureWidth; }
    [[nodiscard]] int skyCaptureHeight() const { return kSkyCaptureHeight; }

    // History ping-pong for temporal accumulation
    [[nodiscard]] GLuint historySceneTexture() const { return m_historySceneTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historySceneTexturePrev() const { return m_historySceneTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyDepthTexture() const { return m_historyDepthTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyDepthTexturePrev() const { return m_historyDepthTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyReflectionTexture() const { return m_historyReflectionTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyReflectionTexturePrev() const { return m_historyReflectionTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyCloudTexture() const { return m_historyCloudTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyCloudTexturePrev() const { return m_historyCloudTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyVolumetricTexture() const { return m_historyVolumetricTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyVolumetricTexturePrev() const { return m_historyVolumetricTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint temporalCurrentTexture() const { return m_temporalCurrentTex; }
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }

    // Atmosphere LUT
    [[nodiscard]] GLuint atmosphereLutTexture() const { return m_atmosphereLut3d; }
    bool loadAtmosphereLut(const char* path);

    // Dimensions
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] int halfWidth() const { return m_width / 2; }
    [[nodiscard]] int halfHeight() const { return m_height / 2; }
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] bool isReady() const { return m_ready; }
    [[nodiscard]] bool consumeRebuiltFlag() { bool v = m_rebuiltSinceCheck; m_rebuiltSinceCheck = false; return v; }

private:
    static constexpr int kSkyCaptureWidth = 256;
    static constexpr int kSkyCaptureHeight = 514;
    static constexpr GLenum kGAlbedoAttachment = GL_COLOR_ATTACHMENT0;
    static constexpr GLenum kGNormalAoAttachment = GL_COLOR_ATTACHMENT1;
    static constexpr GLenum kGVoxelLightAttachment = GL_COLOR_ATTACHMENT2;
    static constexpr GLenum kGMaterialAttachment = GL_COLOR_ATTACHMENT3;
    static constexpr GLenum kGMaterialAuxAttachment = GL_COLOR_ATTACHMENT4;
    static constexpr GLsizei kGBufferAttachmentCount = 5;

    static GLuint createTexture2D(GLenum internalFormat, int width, int height,
                                  GLenum format, GLenum type, GLenum minFilter,
                                  GLenum magFilter, GLenum wrap, int levels = 1);
    static void generateMipmaps(GLuint texture);
    static bool checkFramebufferComplete(GLuint framebuffer, const char* label);
    void destroyFramebuffers();

    // GBuffer
    GLuint m_gBufferFbo = 0;
    GLuint m_gAlbedo = 0;
    GLuint m_gNormalAo = 0;
    GLuint m_gVoxelLight = 0;
    GLuint m_gMaterial = 0;
    GLuint m_gMaterialAux = 0;
    GLuint m_gDepth = 0;

    // SSAO
    GLuint m_ssaoFbo = 0;
    GLuint m_ssaoTex = 0;
    GLuint m_ssaoFilteredFbo = 0;
    GLuint m_ssaoFilteredTex = 0;
    GLuint m_ssaoHalfResFbo = 0;
    GLuint m_ssaoHalfResTex = 0;
    GLuint m_ssaoHalfResFilteredFbo = 0;
    GLuint m_ssaoHalfResFilteredTex = 0;
    GLuint m_ssaoHistoryFbo[2] = {0, 0};
    GLuint m_ssaoHistoryTex[2] = {0, 0};
    int m_ssaoHistoryIndex = 0;
    GLuint m_ssaoTemporalFbo = 0;
    GLuint m_ssaoTemporalTex = 0;

    // Scene HDR buffers
    GLuint m_sceneLightingFbo = 0;
    GLuint m_sceneLightingTex = 0;
    GLuint m_sceneCompositeFbo = 0;
    GLuint m_sceneCompositeTex = 0;
    GLuint m_sceneResolvedFbo = 0;
    GLuint m_sceneResolvedTex = 0;
    GLuint m_transparentCompositeFbo = 0;
    GLuint m_transparentCompositeTex = 0;
    GLuint m_transparentCompositeDepth = 0;
    GLuint m_halfResFbo = 0;
    GLuint m_halfResTex = 0;
    GLuint m_reflectionFbo = 0;
    GLuint m_reflectionTex = 0;
    GLuint m_reflectionTemporalScratchFbo = 0;
    GLuint m_reflectionTemporalScratchTex = 0;
    GLuint m_cloudFbo = 0;
    GLuint m_cloudTex = 0;

    // Sky capture
    GLuint m_skyCaptureFbo = 0;
    GLuint m_skyCaptureTex = 0;

    // History ping-pong
    GLuint m_historySceneFbo[2] = {0, 0};
    GLuint m_historySceneTex[2] = {0, 0};
    GLuint m_historyDepthTex[2] = {0, 0};
    GLuint m_historyReflectionFbo[2] = {0, 0};
    GLuint m_historyReflectionTex[2] = {0, 0};
    GLuint m_historyCloudFbo[2] = {0, 0};
    GLuint m_historyCloudTex[2] = {0, 0};
    GLuint m_historyVolumetricFbo[2] = {0, 0};
    GLuint m_historyVolumetricTex[2] = {0, 0};
    int m_currentHistoryIndex = 0;

    // TAA current-frame scratch
    GLuint m_temporalCurrentFbo = 0;
    GLuint m_temporalCurrentTex = 0;

    // Velocity
    GLuint m_velocityFbo = 0;
    GLuint m_velocityTex = 0;
    GLuint m_perObjectVelocityTex = 0;

    // Weather mask
    GLuint m_weatherMaskFbo = 0;
    GLuint m_weatherMaskTex = 0;

    // Atmosphere LUT
    GLuint m_atmosphereLut3d = 0;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
    bool m_rebuiltSinceCheck = false;
};

#endif // MECRAFT_DEFERRED_FRAME_TARGETS_H
