#ifndef MECRAFT_DEFERRED_FRAME_TARGETS_H
#define MECRAFT_DEFERRED_FRAME_TARGETS_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>

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
    void copyFramebufferColorToSceneLighting(int32_t framebuffer, int width, int height) const;
    void copyFramebufferColorToSceneResolved(int32_t framebuffer, int width, int height) const;
    void copyFramebufferColorToTransparentComposite(int32_t framebuffer, int width, int height) const;
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
    void blitSceneLightingTo(int32_t framebuffer, int width, int height) const;
    void blitSceneCompositeTo(int32_t framebuffer, int width, int height) const;
    void blitSceneResolvedTo(int32_t framebuffer, int width, int height) const;
    void blitTransparentCompositeTo(int32_t framebuffer, int width, int height) const;
    void blitDepthTo(int32_t framebuffer, int width, int height) const;

    // GBuffer texture accessors
    [[nodiscard]] uint32_t albedoTexture() const { return m_gAlbedo; }
    [[nodiscard]] uint32_t normalAoTexture() const { return m_gNormalAo; }
    [[nodiscard]] uint32_t voxelLightTexture() const { return m_gVoxelLight; }
    [[nodiscard]] uint32_t materialTexture() const { return m_gMaterial; }
    [[nodiscard]] uint32_t materialAuxTexture() const { return m_gMaterialAux; }
    [[nodiscard]] uint32_t depthTexture() const { return m_gDepth; }

    // SSAO texture accessors
    [[nodiscard]] uint32_t ssaoTexture() const { return m_ssaoTex; }
    [[nodiscard]] uint32_t ssaoFilteredTexture() const { return m_ssaoFilteredTex; }
    [[nodiscard]] uint32_t ssaoHalfResTexture() const { return m_ssaoHalfResTex; }
    [[nodiscard]] uint32_t ssaoHalfResFilteredTexture() const { return m_ssaoHalfResFilteredTex; }
    [[nodiscard]] uint32_t ssaoHistoryTexture() const { return m_ssaoHistoryTex[m_ssaoHistoryIndex]; }
    [[nodiscard]] uint32_t ssaoHistoryTexturePrev() const { return m_ssaoHistoryTex[1 - m_ssaoHistoryIndex]; }
    [[nodiscard]] uint32_t ssaoTemporalTexture() const { return m_ssaoTemporalTex; }

    // Scene HDR texture accessors
    [[nodiscard]] uint32_t sceneLightingTexture() const { return m_sceneLightingTex; }
    [[nodiscard]] uint32_t sceneCompositeTexture() const { return m_sceneCompositeTex; }
    [[nodiscard]] uint32_t sceneResolvedTexture() const { return m_sceneResolvedTex; }
    [[nodiscard]] uint32_t transparentCompositeTexture() const { return m_transparentCompositeTex; }
    [[nodiscard]] uint32_t transparentCompositeDepthTexture() const { return m_transparentCompositeDepth; }
    [[nodiscard]] uint32_t halfResTexture() const { return m_halfResTex; }
    [[nodiscard]] uint32_t reflectionTexture() const { return m_reflectionTex; }
    [[nodiscard]] uint32_t reflectionTemporalScratchTexture() const { return m_reflectionTemporalScratchTex; }
    [[nodiscard]] uint32_t cloudTexture() const { return m_cloudTex; }
    [[nodiscard]] uint32_t velocityTexture() const { return m_velocityTex; }
    [[nodiscard]] uint32_t perObjectVelocityTexture() const { return m_perObjectVelocityTex; }
    [[nodiscard]] uint32_t weatherMaskTexture() const { return m_weatherMaskTex; }

    // Sky capture
    [[nodiscard]] uint32_t skyCaptureFramebuffer() const { return m_skyCaptureFbo; }
    [[nodiscard]] uint32_t skyCaptureTexture() const { return m_skyCaptureTex; }
    [[nodiscard]] int skyCaptureWidth() const { return kSkyCaptureWidth; }
    [[nodiscard]] int skyCaptureHeight() const { return kSkyCaptureHeight; }

    // History ping-pong for temporal accumulation
    [[nodiscard]] uint32_t historySceneTexture() const { return m_historySceneTex[m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historySceneTexturePrev() const { return m_historySceneTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyDepthTexture() const { return m_historyDepthTex[m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyDepthTexturePrev() const { return m_historyDepthTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyReflectionTexture() const { return m_historyReflectionTex[m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyReflectionTexturePrev() const { return m_historyReflectionTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyCloudTexture() const { return m_historyCloudTex[m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyCloudTexturePrev() const { return m_historyCloudTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyVolumetricTexture() const { return m_historyVolumetricTex[m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t historyVolumetricTexturePrev() const { return m_historyVolumetricTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] uint32_t temporalCurrentTexture() const { return m_temporalCurrentTex; }
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }

    // Atmosphere LUT
    [[nodiscard]] uint32_t atmosphereLutTexture() const { return m_atmosphereLut3d; }
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

    void destroyFramebuffers();

    // GBuffer
    uint32_t m_gBufferFbo = 0;
    uint32_t m_gAlbedo = 0;
    uint32_t m_gNormalAo = 0;
    uint32_t m_gVoxelLight = 0;
    uint32_t m_gMaterial = 0;
    uint32_t m_gMaterialAux = 0;
    uint32_t m_gDepth = 0;

    // SSAO
    uint32_t m_ssaoFbo = 0;
    uint32_t m_ssaoTex = 0;
    uint32_t m_ssaoFilteredFbo = 0;
    uint32_t m_ssaoFilteredTex = 0;
    uint32_t m_ssaoHalfResFbo = 0;
    uint32_t m_ssaoHalfResTex = 0;
    uint32_t m_ssaoHalfResFilteredFbo = 0;
    uint32_t m_ssaoHalfResFilteredTex = 0;
    uint32_t m_ssaoHistoryFbo[2] = {0, 0};
    uint32_t m_ssaoHistoryTex[2] = {0, 0};
    int m_ssaoHistoryIndex = 0;
    uint32_t m_ssaoTemporalFbo = 0;
    uint32_t m_ssaoTemporalTex = 0;

    // Scene HDR buffers
    uint32_t m_sceneLightingFbo = 0;
    uint32_t m_sceneLightingTex = 0;
    uint32_t m_sceneCompositeFbo = 0;
    uint32_t m_sceneCompositeTex = 0;
    uint32_t m_sceneResolvedFbo = 0;
    uint32_t m_sceneResolvedTex = 0;
    uint32_t m_transparentCompositeFbo = 0;
    uint32_t m_transparentCompositeTex = 0;
    uint32_t m_transparentCompositeDepth = 0;
    uint32_t m_halfResFbo = 0;
    uint32_t m_halfResTex = 0;
    uint32_t m_reflectionFbo = 0;
    uint32_t m_reflectionTex = 0;
    uint32_t m_reflectionTemporalScratchFbo = 0;
    uint32_t m_reflectionTemporalScratchTex = 0;
    uint32_t m_cloudFbo = 0;
    uint32_t m_cloudTex = 0;

    // Sky capture
    uint32_t m_skyCaptureFbo = 0;
    uint32_t m_skyCaptureTex = 0;

    // History ping-pong
    uint32_t m_historySceneFbo[2] = {0, 0};
    uint32_t m_historySceneTex[2] = {0, 0};
    uint32_t m_historyDepthTex[2] = {0, 0};
    uint32_t m_historyReflectionFbo[2] = {0, 0};
    uint32_t m_historyReflectionTex[2] = {0, 0};
    uint32_t m_historyCloudFbo[2] = {0, 0};
    uint32_t m_historyCloudTex[2] = {0, 0};
    uint32_t m_historyVolumetricFbo[2] = {0, 0};
    uint32_t m_historyVolumetricTex[2] = {0, 0};
    int m_currentHistoryIndex = 0;

    // TAA current-frame scratch
    uint32_t m_temporalCurrentFbo = 0;
    uint32_t m_temporalCurrentTex = 0;

    // Velocity
    uint32_t m_velocityFbo = 0;
    uint32_t m_velocityTex = 0;
    uint32_t m_perObjectVelocityTex = 0;

    // Weather mask
    uint32_t m_weatherMaskFbo = 0;
    uint32_t m_weatherMaskTex = 0;

    // Atmosphere LUT
    uint32_t m_atmosphereLut3d = 0;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
    bool m_rebuiltSinceCheck = false;
};

#endif // MECRAFT_DEFERRED_FRAME_TARGETS_H
