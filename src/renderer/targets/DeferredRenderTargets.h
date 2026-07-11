#ifndef MECRAFT_DEFERRED_RENDER_TARGETS_H
#define MECRAFT_DEFERRED_RENDER_TARGETS_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>

class RhiDevice;

class DeferredRenderTargets {
public:
    ~DeferredRenderTargets();

    bool init(RhiDevice& rhiDevice);
    void shutdown();

    bool ensureSize(int width, int height, int shadowResolution);

    void copyTextureColorToSceneLighting(RhiDevice& rhiDevice, RhiTextureHandle source) const;
    void copySceneLightingToTransparentComposite(RhiDevice& rhiDevice) const;
    void copySceneLightingToSceneComposite(RhiDevice& rhiDevice) const;
    void copySceneCompositeToSceneResolved(RhiDevice& rhiDevice) const;
    void copySceneCompositeToTransparentComposite(RhiDevice& rhiDevice) const;
    void copySceneResolvedToTransparentComposite(RhiDevice& rhiDevice) const;
    void copyTransparentCompositeToSceneComposite(RhiDevice& rhiDevice) const;
    void copyTransparentCompositeToSceneResolved(RhiDevice& rhiDevice) const;
    void copyDepthToTransparentComposite(RhiDevice& rhiDevice) const;
    void copySceneResolvedToHistory(RhiDevice& rhiDevice) const;
    void copySceneResolvedToTemporalCurrent(RhiDevice& rhiDevice) const;
    void copyDepthToHistory(RhiDevice& rhiDevice) const;
    void copyReflectionToHistory(RhiDevice& rhiDevice) const;
    void copyReflectionToTemporalScratch(RhiDevice& rhiDevice) const;
    void copyCloudToHistory(RhiDevice& rhiDevice) const;
    void copyHistoryCloudToCloud(RhiDevice& rhiDevice) const;
    void copyVolumetricToHistory(RhiDevice& rhiDevice) const;
    void copyHistoryVolumetricToHalfRes(RhiDevice& rhiDevice) const;
    void copySceneResolvedToTexture(RhiDevice& rhiDevice, RhiTextureHandle destination) const;
    void copyDepthToTexture(RhiDevice& rhiDevice, RhiTextureHandle destination) const;
    void copyTransparentCompositeToTexture(RhiDevice& rhiDevice, RhiTextureHandle destination) const;

    [[nodiscard]] RhiTextureHandle albedoTextureHandle() const { return m_gAlbedoHandle; }
    [[nodiscard]] RhiTextureHandle normalAoTextureHandle() const { return m_gNormalAoHandle; }
    [[nodiscard]] RhiTextureHandle voxelLightTextureHandle() const { return m_gVoxelLightHandle; }
    [[nodiscard]] RhiTextureHandle materialTextureHandle() const { return m_gMaterialHandle; }
    [[nodiscard]] RhiTextureHandle materialAuxTextureHandle() const { return m_gMaterialAuxHandle; }
    [[nodiscard]] RhiTextureHandle depthTextureHandle() const { return m_gDepthHandle; }
    [[nodiscard]] RhiTextureViewHandle albedoTextureViewHandle() const { return m_gAlbedoView; }
    [[nodiscard]] RhiTextureViewHandle normalAoTextureViewHandle() const { return m_gNormalAoView; }
    [[nodiscard]] RhiTextureViewHandle voxelLightTextureViewHandle() const { return m_gVoxelLightView; }
    [[nodiscard]] RhiTextureViewHandle materialTextureViewHandle() const { return m_gMaterialView; }
    [[nodiscard]] RhiTextureViewHandle materialAuxTextureViewHandle() const { return m_gMaterialAuxView; }
    [[nodiscard]] RhiTextureViewHandle depthTextureViewHandle() const { return m_gDepthView; }
    bool ensureGBufferTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle shadowDepthTextureHandle() const { return m_shadowDepthHandle; }
    [[nodiscard]] RhiTextureHandle shadowDepthComparisonTextureHandle() const { return m_shadowDepthComparisonHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthTextureHandle() const { return m_csmShadowDepthHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthComparisonTextureHandle() const { return m_csmShadowDepthComparisonHandle; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthTextureViewHandle(int cascadeIndex) const;
    bool ensureCsmShadowDepthTextureView(RhiDevice& rhiDevice, int cascadeIndex);
    // CSM transparent shadow contract (DerivativeMain shadowtex0/shadowcolor0/1 equivalent)
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllTextureHandle() const { return m_csmShadowDepthAllHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllComparisonTextureHandle() const { return m_csmShadowDepthAllComparisonHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor0TextureHandle() const { return m_csmShadowColor0Handle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor1TextureHandle() const { return m_csmShadowColor1Handle; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthAllTextureViewHandle(int cascadeIndex) const;
    [[nodiscard]] RhiTextureViewHandle csmShadowColor0TextureViewHandle(int cascadeIndex) const;
    [[nodiscard]] RhiTextureViewHandle csmShadowColor1TextureViewHandle(int cascadeIndex) const;
    bool ensureCsmShadowTransparentTextureViews(RhiDevice& rhiDevice, int cascadeIndex);
    [[nodiscard]] RhiTextureHandle shadowColorTextureHandle() const { return m_shadowColorHandle; }
    [[nodiscard]] RhiTextureHandle shadowNormalTextureHandle() const { return m_shadowNormalHandle; }
    [[nodiscard]] RhiTextureViewHandle shadowDepthTextureViewHandle() const { return m_shadowDepthView; }
    [[nodiscard]] RhiTextureViewHandle shadowColorTextureViewHandle() const { return m_shadowColorView; }
    [[nodiscard]] RhiTextureHandle ssaoTextureHandle() const { return m_ssaoHandle; }
    [[nodiscard]] RhiTextureHandle ssaoFilteredTextureHandle() const { return m_ssaoFilteredHandle; }
    [[nodiscard]] RhiTextureViewHandle ssaoFilteredTextureViewHandle() const { return m_ssaoFilteredView; }
    bool ensureSsaoFilteredTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle ssaoHalfResTextureHandle() const { return m_ssaoHalfResHandle; }
    [[nodiscard]] RhiTextureViewHandle ssaoHalfResTextureViewHandle() const { return m_ssaoHalfResView; }
    bool ensureSsaoHalfResTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle ssaoHalfResFilteredTextureHandle() const { return m_ssaoHalfResFilteredHandle; }
    [[nodiscard]] RhiTextureViewHandle ssaoHalfResFilteredTextureViewHandle() const { return m_ssaoHalfResFilteredView; }
    bool ensureSsaoHalfResFilteredTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] int halfWidth() const { return m_width / 2; }
    [[nodiscard]] int halfHeight() const { return m_height / 2; }
    [[nodiscard]] RhiTextureHandle ssaoHistoryTextureHandle() const { return m_ssaoHistoryHandle[m_ssaoHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssaoHistoryTexturePrevHandle() const { return m_ssaoHistoryHandle[1 - m_ssaoHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle ssaoHistoryTexturePrevViewHandle() const { return m_ssaoHistoryView[1 - m_ssaoHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssaoTemporalTextureHandle() const { return m_ssaoTemporalHandle; }
    [[nodiscard]] RhiTextureViewHandle ssaoTemporalTextureViewHandle() const { return m_ssaoTemporalView; }
    bool ensureSsaoTemporalTextureView(RhiDevice& rhiDevice);
    bool ensureSsaoHistoryTextureViews(RhiDevice& rhiDevice);
    void swapSsaoHistory() { m_ssaoHistoryIndex = 1 - m_ssaoHistoryIndex; }
    void copySsaoTemporalToHistory(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle ssgiTextureHandle() const { return m_ssgiHandle; }
    [[nodiscard]] RhiTextureViewHandle ssgiTextureViewHandle() const { return m_ssgiView; }
    bool ensureSsgiTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle ssgiHalfResTextureHandle() const { return m_ssgiHalfResHandle; }
    [[nodiscard]] RhiTextureViewHandle ssgiHalfResTextureViewHandle() const { return m_ssgiHalfResView; }
    bool ensureSsgiHalfResTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle ssgiDenoiseTextureHandle(int slot) const;
    [[nodiscard]] RhiTextureHandle ssgiHistoryTextureHandle() const { return m_ssgiHistoryHandle[m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssgiHistoryTexturePrevHandle() const { return m_ssgiHistoryHandle[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle ssgiHistoryTexturePrevViewHandle() const { return m_ssgiHistoryView[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssgiMomentsHistoryTextureHandle() const { return m_ssgiMomentsHistoryHandle[m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssgiMomentsHistoryTexturePrevHandle() const { return m_ssgiMomentsHistoryHandle[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle ssgiMomentsHistoryTexturePrevViewHandle() const { return m_ssgiMomentsHistoryView[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle ssgiTemporalTextureHandle() const { return m_ssgiTemporalHandle; }
    [[nodiscard]] RhiTextureViewHandle ssgiTemporalTextureViewHandle() const { return m_ssgiTemporalView; }
    [[nodiscard]] RhiTextureHandle ssgiTemporalMomentsTextureHandle() const { return m_ssgiTemporalMomentsHandle; }
    [[nodiscard]] RhiTextureViewHandle ssgiTemporalMomentsTextureViewHandle() const { return m_ssgiTemporalMomentsView; }
    bool ensureSsgiTemporalTextureViews(RhiDevice& rhiDevice);
    bool ensureSsgiHistoryTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureViewHandle ssgiDenoiseTextureViewHandle(int slot) const;
    bool ensureSsgiDenoiseTextureView(RhiDevice& rhiDevice, int slot);
    void swapSsgiHistory() { m_ssgiHistoryIndex = 1 - m_ssgiHistoryIndex; }
    void copySsgiDenoiseToSsgi(RhiDevice& rhiDevice, int slot);
    void copySsgiTemporalToSsgi(RhiDevice& rhiDevice);
    void copySsgiTemporalToHistory(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle sceneLightingTextureHandle() const { return m_sceneLightingHandle; }
    [[nodiscard]] RhiTextureViewHandle sceneLightingTextureViewHandle() const { return m_sceneLightingView; }
    bool ensureSceneLightingTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle sceneCompositeTextureHandle() const { return m_sceneCompositeHandle; }
    [[nodiscard]] RhiTextureViewHandle sceneCompositeTextureViewHandle() const { return m_sceneCompositeView; }
    bool ensureSceneCompositeTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle sceneResolvedTextureHandle() const { return m_sceneResolvedHandle; }
    [[nodiscard]] RhiTextureViewHandle sceneResolvedTextureViewHandle() const { return m_sceneResolvedView; }
    bool ensureSceneResolvedTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle transparentCompositeTextureHandle() const { return m_transparentCompositeHandle; }
    [[nodiscard]] RhiTextureViewHandle transparentCompositeTextureViewHandle() const { return m_transparentCompositeView; }
    [[nodiscard]] RhiTextureHandle transparentCompositeDepthTextureHandle() const { return m_transparentCompositeDepthHandle; }
    [[nodiscard]] RhiTextureViewHandle transparentCompositeDepthTextureViewHandle() const { return m_transparentCompositeDepthView; }
    bool ensureTransparentCompositeTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle halfResTextureHandle() const { return m_halfResHandle; }
    [[nodiscard]] RhiTextureViewHandle halfResTextureViewHandle() const { return m_halfResView; }
    bool ensureHalfResTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle reflectionTextureHandle() const { return m_reflectionHandle; }
    [[nodiscard]] RhiTextureViewHandle reflectionTextureViewHandle() const { return m_reflectionView; }
    bool ensureReflectionTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle reflectionTemporalScratchTextureHandle() const { return m_reflectionTemporalScratchHandle; }
    [[nodiscard]] RhiTextureViewHandle reflectionTemporalScratchTextureViewHandle() const { return m_reflectionTemporalScratchView; }
    bool ensureReflectionTemporalScratchTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle cloudTextureHandle() const { return m_cloudHandle; }
    [[nodiscard]] RhiTextureViewHandle cloudTextureViewHandle() const { return m_cloudView; }
    bool ensureCloudTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle skyCaptureTextureHandle() const { return m_skyCaptureHandle; }
    [[nodiscard]] RhiTextureViewHandle skyCaptureTextureViewHandle() const { return m_skyCaptureView; }
    bool ensureSkyCaptureTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] int skyCaptureWidth() const { return kSkyCaptureWidth; }
    [[nodiscard]] int skyCaptureHeight() const { return kSkyCaptureHeight; }
    // History ping-pong for temporal accumulation
    [[nodiscard]] RhiTextureHandle historySceneTextureHandle() const { return m_historySceneHandle[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle historySceneTexturePrevHandle() const { return m_historySceneHandle[1 - m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historySceneTextureViewHandle() const { return m_historySceneView[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historySceneTexturePrevViewHandle() const { return m_historySceneView[1 - m_currentHistoryIndex]; }
    bool ensureHistorySceneTextureView(RhiDevice& rhiDevice);
    bool ensureHistorySceneTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle historyDepthTextureHandle() const { return m_historyDepthHandle[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle historyDepthTexturePrevHandle() const { return m_historyDepthHandle[1 - m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historyDepthTexturePrevViewHandle() const { return m_historyDepthView[1 - m_currentHistoryIndex]; }
    bool ensureHistoryDepthTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle historyReflectionTextureHandle() const { return m_historyReflectionHandle[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle historyReflectionTexturePrevHandle() const { return m_historyReflectionHandle[1 - m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historyReflectionTexturePrevViewHandle() const { return m_historyReflectionView[1 - m_currentHistoryIndex]; }
    bool ensureHistoryReflectionTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle historyCloudTextureHandle() const { return m_historyCloudHandle[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle historyCloudTexturePrevHandle() const { return m_historyCloudHandle[1 - m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historyCloudTexturePrevViewHandle() const { return m_historyCloudView[1 - m_currentHistoryIndex]; }
    bool ensureHistoryCloudTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle historyVolumetricTextureHandle() const { return m_historyVolumetricHandle[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureHandle historyVolumetricTexturePrevHandle() const { return m_historyVolumetricHandle[1 - m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historyVolumetricTextureViewHandle() const { return m_historyVolumetricView[m_currentHistoryIndex]; }
    [[nodiscard]] RhiTextureViewHandle historyVolumetricTexturePrevViewHandle() const { return m_historyVolumetricView[1 - m_currentHistoryIndex]; }
    bool ensureHistoryVolumetricTextureView(RhiDevice& rhiDevice);
    bool ensureHistoryVolumetricTextureViews(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle temporalCurrentTextureHandle() const { return m_temporalCurrentHandle; }
    [[nodiscard]] RhiTextureViewHandle temporalCurrentTextureViewHandle() const { return m_temporalCurrentView; }
    bool ensureTemporalCurrentTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle velocityTextureHandle() const { return m_velocityHandle; }
    [[nodiscard]] RhiTextureViewHandle velocityTextureViewHandle() const { return m_velocityView; }
    bool ensureVelocityTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle perObjectVelocityTextureHandle() const { return m_perObjectVelocityHandle; }
    [[nodiscard]] RhiTextureViewHandle perObjectVelocityTextureViewHandle() const { return m_perObjectVelocityView; }
    bool ensurePerObjectVelocityTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle weatherMaskTextureHandle() const { return m_weatherMaskHandle; }
    [[nodiscard]] RhiTextureViewHandle weatherMaskTextureViewHandle() const { return m_weatherMaskView; }
    bool ensureWeatherMaskTextureView(RhiDevice& rhiDevice);
    [[nodiscard]] RhiTextureHandle atmosphereLutTextureHandle() const { return m_atmosphereLutHandle; }
    [[nodiscard]] RhiTextureViewHandle atmosphereLutTextureViewHandle() const { return m_atmosphereLutView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthArrayTextureViewHandle() const { return m_csmShadowDepthArrayView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthComparisonArrayTextureViewHandle() const { return m_csmShadowDepthComparisonArrayView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthAllArrayTextureViewHandle() const { return m_csmShadowDepthAllArrayView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowDepthAllComparisonArrayTextureViewHandle() const { return m_csmShadowDepthAllComparisonArrayView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowColor0ArrayTextureViewHandle() const { return m_csmShadowColor0ArrayView; }
    [[nodiscard]] RhiTextureViewHandle csmShadowColor1ArrayTextureViewHandle() const { return m_csmShadowColor1ArrayView; }
    bool ensureVolumetricFogTextureViews(RhiDevice& rhiDevice);
    bool loadAtmosphereLut(const char* path);
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] int shadowCascadeCount() const { return kShadowCascadeCount; }
    [[nodiscard]] bool isReady() const { return m_ready; }
    // Returns true if ensureSize() performed a rebuild since the last call.
    // Consumes the flag — second call returns false until next rebuild.
    [[nodiscard]] bool consumeRebuiltFlag() { bool v = m_rebuiltSinceCheck; m_rebuiltSinceCheck = false; return v; }

private:
    static constexpr int kShadowCascadeCount = 4;
    // Sky capture: 256x514 equirectangular map (matches DerivativeMain colortex5).
    // skyCaptureRes = ivec2(255, 256), texture = 256 wide x 514 tall.
    // Rows 0..257:   raw atmospheric sky radiance (equirectangular).
    // Rows 258..513: cloudy skybox (sky + clouds composited).
    // Column 255, rows 0-5: metadata texels
    //   (directIlluminance, skyIlluminance, sunIlluminance, moonIlluminance, unused, cloudDynamicWeather).
    static constexpr int kSkyCaptureWidth = 256;
    static constexpr int kSkyCaptureHeight = 514;
    static constexpr int kAtmosphereLutWidth = 256;
    static constexpr int kAtmosphereLutHeight = 128;
    static constexpr int kAtmosphereLutDepth = 33;
    static uint32_t createTexture2D(uint32_t internalFormat,
                                    int width,
                                    int height,
                                    uint32_t format,
                                    uint32_t type,
                                    uint32_t minFilter,
                                    uint32_t magFilter,
                                    uint32_t wrap,
                                    int levels = 1);
    static uint32_t createTexture2DArray(uint32_t internalFormat,
                                         int width,
                                         int height,
                                         int layers,
                                         uint32_t minFilter,
                                         uint32_t magFilter,
                                         uint32_t wrap);

    bool createGBufferTextures();
    void destroyGBufferTextures();
    bool createSceneTextures();
    void destroySceneTextures();
    bool createTransparentCompositeTextures();
    void destroyTransparentCompositeTextures();
    bool createScreenEffectTextures();
    void destroyScreenEffectTextures();
    bool createAtmosphereTextures();
    void destroyAtmosphereTextures();
    bool createSceneHistoryTextures();
    void destroySceneHistoryTextures();
    bool createEffectHistoryTextures();
    void destroyEffectHistoryTextures();
    bool createMotionTextures();
    void destroyMotionTextures();
    bool registerRhiTextures();
    bool registerAtmosphereLutTexture();
    void unregisterRhiTextures();
    void destroyRhiTextureViews();
    void destroyFramebuffers();

    // G-buffer contract:
    // 0 RGBA8    = linear albedo.rgb, emissive hint.a
    // 1 RGBA16F  = encoded world normal.rgb, vertex AO.a
    // 2 RG8      = sky light.r, block light.g
    // 3 RGBA8    = roughness.r, f0.g, emission.b, subsurface.a
    // 4 RGBA8    = DerivativeMain material id.r, wetness mask.g, porosity.b, metalness.a
    RhiTextureHandle m_gAlbedoHandle;
    RhiTextureHandle m_gNormalAoHandle;
    RhiTextureHandle m_gVoxelLightHandle;
    RhiTextureHandle m_gMaterialHandle;
    RhiTextureHandle m_gMaterialAuxHandle;
    RhiTextureHandle m_gDepthHandle;
    RhiTextureViewHandle m_gAlbedoView;
    RhiTextureViewHandle m_gNormalAoView;
    RhiTextureViewHandle m_gVoxelLightView;
    RhiTextureViewHandle m_gMaterialView;
    RhiTextureViewHandle m_gMaterialAuxView;
    RhiTextureViewHandle m_gDepthView;

    RhiDevice* m_rhiDevice = nullptr;

    uint32_t m_shadowDepth = 0;
    uint32_t m_shadowDepthComparison = 0; // Zero-copy comparison view for sampler2DShadow
    uint32_t m_shadowColor = 0;   // RGBA8: albedo color for colored shadows / caustics
    uint32_t m_shadowNormal = 0;  // RGBA16F: encoded normal.rg, skylight.b, aux/height.a
    RhiTextureHandle m_shadowDepthHandle;
    RhiTextureHandle m_shadowDepthComparisonHandle;
    RhiTextureHandle m_shadowColorHandle;
    RhiTextureHandle m_shadowNormalHandle;
    RhiTextureViewHandle m_shadowDepthView;
    RhiTextureViewHandle m_shadowColorView;
    uint32_t m_csmShadowDepth = 0; // Raw depth texture array, one layer per cascade
    uint32_t m_csmShadowDepthComparison = 0; // Comparison texture array for sampler2DArrayShadow
    RhiTextureHandle m_csmShadowDepthHandle;
    RhiTextureHandle m_csmShadowDepthComparisonHandle;
    RhiTextureViewHandle m_csmShadowDepthView[kShadowCascadeCount];
    RhiTextureViewHandle m_csmShadowDepthArrayView;
    RhiTextureViewHandle m_csmShadowDepthComparisonArrayView;
    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    uint32_t m_csmShadowDepthAll = 0; // depth including water/transparent surfaces
    uint32_t m_csmShadowDepthAllComparison = 0;
    uint32_t m_csmShadowColor0 = 0; // RGBA8: RGB caustics/tint, A transparent flag
    uint32_t m_csmShadowColor1 = 0; // RGBA16F: RG normal, B skylight, A water height
    RhiTextureHandle m_csmShadowDepthAllHandle;
    RhiTextureHandle m_csmShadowDepthAllComparisonHandle;
    RhiTextureHandle m_csmShadowColor0Handle;
    RhiTextureHandle m_csmShadowColor1Handle;
    RhiTextureViewHandle m_csmShadowDepthAllArrayView;
    RhiTextureViewHandle m_csmShadowDepthAllComparisonArrayView;
    RhiTextureViewHandle m_csmShadowColor0ArrayView;
    RhiTextureViewHandle m_csmShadowColor1ArrayView;
    RhiTextureViewHandle m_csmShadowDepthAllView[kShadowCascadeCount];
    RhiTextureViewHandle m_csmShadowColor0View[kShadowCascadeCount];
    RhiTextureViewHandle m_csmShadowColor1View[kShadowCascadeCount];

    uint32_t m_ssaoTex = 0;
    RhiTextureHandle m_ssaoHandle;
    uint32_t m_ssaoFilteredTex = 0;
    RhiTextureHandle m_ssaoFilteredHandle;
    RhiTextureViewHandle m_ssaoFilteredView;
    // Half-res SSAO: raw and filtered at width/2 x height/2
    uint32_t m_ssaoHalfResTex = 0;
    RhiTextureHandle m_ssaoHalfResHandle;
    RhiTextureViewHandle m_ssaoHalfResView;
    uint32_t m_ssaoHalfResFilteredTex = 0;
    RhiTextureHandle m_ssaoHalfResFilteredHandle;
    RhiTextureViewHandle m_ssaoHalfResFilteredView;
    // SSAO temporal history ping-pong (R8)
    uint32_t m_ssaoHistoryTex[2] = {0, 0};
    RhiTextureHandle m_ssaoHistoryHandle[2];
    RhiTextureViewHandle m_ssaoHistoryView[2];
    int m_ssaoHistoryIndex = 0;
    // SSAO temporal resolve output (R8) — deferred lighting reads from this
    uint32_t m_ssaoTemporalTex = 0;
    RhiTextureHandle m_ssaoTemporalHandle;
    RhiTextureViewHandle m_ssaoTemporalView;

    uint32_t m_ssgiTex = 0;
    RhiTextureHandle m_ssgiHandle;
    RhiTextureViewHandle m_ssgiView;
    uint32_t m_ssgiHalfResTex = 0;
    RhiTextureHandle m_ssgiHalfResHandle;
    RhiTextureViewHandle m_ssgiHalfResView;
    uint32_t m_ssgiDenoiseTex[2] = {0, 0};
    RhiTextureHandle m_ssgiDenoiseHandle[2];
    RhiTextureViewHandle m_ssgiDenoiseView[2];
    uint32_t m_ssgiHistoryTex[2] = {0, 0};
    uint32_t m_ssgiMomentsHistoryTex[2] = {0, 0};
    RhiTextureHandle m_ssgiHistoryHandle[2];
    RhiTextureHandle m_ssgiMomentsHistoryHandle[2];
    RhiTextureViewHandle m_ssgiHistoryView[2];
    RhiTextureViewHandle m_ssgiMomentsHistoryView[2];
    int m_ssgiHistoryIndex = 0;
    uint32_t m_ssgiTemporalTex = 0;
    uint32_t m_ssgiTemporalMomentsTex = 0;
    RhiTextureHandle m_ssgiTemporalHandle;
    RhiTextureHandle m_ssgiTemporalMomentsHandle;
    RhiTextureViewHandle m_ssgiTemporalView;
    RhiTextureViewHandle m_ssgiTemporalMomentsView;

    RhiTextureHandle m_sceneLightingHandle;
    RhiTextureViewHandle m_sceneLightingView;

    // SceneComposite is the opaque HDR scene after screen-space base effects such as clouds/reflections.
    RhiTextureHandle m_sceneCompositeHandle;
    RhiTextureViewHandle m_sceneCompositeView;

    // SceneResolved is the current full-world HDR color. It becomes the post input and temporal scene history source.
    RhiTextureHandle m_sceneResolvedHandle;
    RhiTextureViewHandle m_sceneResolvedView;

    // TransparentComposite is a scratch scene copy used while forward water/generic transparent geometry is blended.
    RhiTextureHandle m_transparentCompositeHandle;
    RhiTextureHandle m_transparentCompositeDepthHandle;
    RhiTextureViewHandle m_transparentCompositeView;
    RhiTextureViewHandle m_transparentCompositeDepthView;

    RhiTextureHandle m_halfResHandle;
    RhiTextureViewHandle m_halfResView;

    RhiTextureHandle m_reflectionHandle;
    RhiTextureViewHandle m_reflectionView;

    // Reflection temporal scratch: holds filtered reflection copy while
    // temporal pass reads it and writes blended result.
    RhiTextureHandle m_reflectionTemporalScratchHandle;
    RhiTextureViewHandle m_reflectionTemporalScratchView;

    RhiTextureHandle m_cloudHandle;
    RhiTextureViewHandle m_cloudView;

    RhiTextureHandle m_skyCaptureHandle;
    RhiTextureViewHandle m_skyCaptureView;

    // History ping-pong for temporal accumulation
    RhiTextureHandle m_historySceneHandle[2];
    RhiTextureHandle m_historyDepthHandle[2];
    RhiTextureViewHandle m_historySceneView[2];
    RhiTextureViewHandle m_historyDepthView[2];
    RhiTextureHandle m_historyReflectionHandle[2];
    RhiTextureViewHandle m_historyReflectionView[2];
    RhiTextureHandle m_historyCloudHandle[2];
    RhiTextureViewHandle m_historyCloudView[2];
    RhiTextureHandle m_historyVolumetricHandle[2];
    RhiTextureViewHandle m_historyVolumetricView[2];
    int m_currentHistoryIndex = 0;
    bool m_rebuiltSinceCheck = false;

    // TAA current-frame scratch: avoids reading history[current] as TAA input.
    RhiTextureHandle m_temporalCurrentHandle;
    RhiTextureViewHandle m_temporalCurrentView;

    // Velocity buffer (RG16F encodes screen-space velocity xy)
    RhiTextureHandle m_velocityHandle;
    RhiTextureViewHandle m_velocityView;
    RhiDevice* m_rhiViewDevice = nullptr;

    // Per-object velocity (RG16F): written by entity/drop GBuffer shaders as an MRT color attachment.
    // Consumed by velocity_resolve.fs.
    RhiTextureHandle m_perObjectVelocityHandle;
    RhiTextureViewHandle m_perObjectVelocityView;

    // Weather mask: single-channel R8 storing accumulated weather particle alpha.
    // Equivalent to DerivativeMain colortex0.b from gbuffers_weather.
    // Written with additive blending by weather geometry, read by postprocess.
    RhiTextureHandle m_weatherMaskHandle;
    RhiTextureViewHandle m_weatherMaskView;
    RhiTextureViewHandle m_atmosphereLutView;

    // Atmosphere precomputed scattering LUT (256x128x33 RGBA32F 3D texture)
    uint32_t m_atmosphereLut3d = 0;
    RhiTextureHandle m_atmosphereLutHandle;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_DEFERRED_RENDER_TARGETS_H
