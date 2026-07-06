#ifndef MECRAFT_DEFERRED_RENDER_TARGETS_H
#define MECRAFT_DEFERRED_RENDER_TARGETS_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>

class DeferredRenderTargets {
public:
    ~DeferredRenderTargets();

    bool init();
    void shutdown();

    bool ensureSize(int width, int height, int shadowResolution);

    void bindGBuffer();
    void bindShadowMap();
    void bindCsmShadowLayer(int cascadeIndex, int cascadeResolution = 0);
    void bindCsmShadowTransparentLayer(int cascadeIndex, int cascadeResolution = 0);
    void bindShadowColor();
    void bindSsao();
    void bindSsaoFiltered();
    void bindSsaoTemporal();
    void bindSsaoHalfRes();
    void bindSsaoHalfResFiltered();
    void bindSsgi();
    void bindSsgiHalfRes();
    void bindSsgiDenoise(int slot);
    void bindSsgiTemporal();
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
    // Per-object velocity: temporarily attaches the RG16F texture to the GBuffer FBO
    // as the sixth color attachment so entity/drop shaders can write velocity via MRT.
    void attachPerObjectVelocityToGBuffer();
    void detachPerObjectVelocityFromGBuffer();
    void clearPerObjectVelocity();
    void bindWeatherMask();
    void clearWeatherMask();
    void bindDefaultLike(int32_t framebuffer, int width, int height);
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
    void copyHistoryCloudToCloud() const;
    void copyVolumetricToHistory() const;
    void copyHistoryVolumetricToHalfRes() const;
    void blitSceneLightingTo(int32_t framebuffer, int width, int height) const;
    void blitSceneCompositeTo(int32_t framebuffer, int width, int height) const;
    void blitSceneResolvedTo(int32_t framebuffer, int width, int height) const;
    void blitTransparentCompositeTo(int32_t framebuffer, int width, int height) const;
    void blitDepthTo(int32_t framebuffer, int width, int height) const;

    [[nodiscard]] uint32_t albedoTexture() const { return m_gAlbedo; }
    [[nodiscard]] uint32_t normalAoTexture() const { return m_gNormalAo; }
    [[nodiscard]] uint32_t voxelLightTexture() const { return m_gVoxelLight; }
    [[nodiscard]] uint32_t materialTexture() const { return m_gMaterial; }
    [[nodiscard]] uint32_t materialAuxTexture() const { return m_gMaterialAux; }
    [[nodiscard]] uint32_t depthTexture() const { return m_gDepth; }
    [[nodiscard]] RhiTextureHandle depthTextureHandle() const { return m_gDepthHandle; }
    [[nodiscard]] uint32_t shadowDepthTexture() const { return m_shadowDepth; }
    [[nodiscard]] uint32_t shadowDepthComparisonTexture() const { return m_shadowDepthComparison; }
    [[nodiscard]] uint32_t csmShadowDepthTexture() const { return m_csmShadowDepth; }
    [[nodiscard]] uint32_t csmShadowDepthComparisonTexture() const { return m_csmShadowDepthComparison; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthTextureHandle() const { return m_csmShadowDepthHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthComparisonTextureHandle() const { return m_csmShadowDepthComparisonHandle; }
    // CSM transparent shadow contract (DerivativeMain shadowtex0/shadowcolor0/1 equivalent)
    [[nodiscard]] uint32_t csmShadowDepthAllTexture() const { return m_csmShadowDepthAll; }
    [[nodiscard]] uint32_t csmShadowDepthAllComparisonTexture() const { return m_csmShadowDepthAllComparison; }
    [[nodiscard]] uint32_t csmShadowColor0Texture() const { return m_csmShadowColor0; }
    [[nodiscard]] uint32_t csmShadowColor1Texture() const { return m_csmShadowColor1; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllTextureHandle() const { return m_csmShadowDepthAllHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllComparisonTextureHandle() const { return m_csmShadowDepthAllComparisonHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor0TextureHandle() const { return m_csmShadowColor0Handle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor1TextureHandle() const { return m_csmShadowColor1Handle; }
    [[nodiscard]] uint32_t shadowColorTexture() const { return m_shadowColor; }
    [[nodiscard]] uint32_t shadowNormalTexture() const { return m_shadowNormal; }
    [[nodiscard]] uint32_t ssaoTexture() const { return m_ssaoTex; }
    [[nodiscard]] uint32_t ssaoFilteredTexture() const { return m_ssaoFilteredTex; }
    [[nodiscard]] uint32_t ssaoHalfResTexture() const { return m_ssaoHalfResTex; }
    [[nodiscard]] uint32_t ssaoHalfResFilteredTexture() const { return m_ssaoHalfResFilteredTex; }
    [[nodiscard]] int halfWidth() const { return m_width / 2; }
    [[nodiscard]] int halfHeight() const { return m_height / 2; }
    [[nodiscard]] uint32_t ssaoHistoryTexture() const { return m_ssaoHistoryTex[m_ssaoHistoryIndex]; }
    [[nodiscard]] uint32_t ssaoHistoryTexturePrev() const { return m_ssaoHistoryTex[1 - m_ssaoHistoryIndex]; }
    [[nodiscard]] uint32_t ssaoTemporalTexture() const { return m_ssaoTemporalTex; }
    void swapSsaoHistory() { m_ssaoHistoryIndex = 1 - m_ssaoHistoryIndex; }
    void copySsaoTemporalToHistory();
    [[nodiscard]] uint32_t ssgiTexture() const { return m_ssgiTex; }
    [[nodiscard]] uint32_t ssgiHalfResTexture() const { return m_ssgiHalfResTex; }
    [[nodiscard]] uint32_t ssgiDenoiseTexture(int slot) const;
    [[nodiscard]] uint32_t ssgiHistoryTexture() const { return m_ssgiHistoryTex[m_ssgiHistoryIndex]; }
    [[nodiscard]] uint32_t ssgiHistoryTexturePrev() const { return m_ssgiHistoryTex[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] uint32_t ssgiMomentsHistoryTexture() const { return m_ssgiMomentsHistoryTex[m_ssgiHistoryIndex]; }
    [[nodiscard]] uint32_t ssgiMomentsHistoryTexturePrev() const { return m_ssgiMomentsHistoryTex[1 - m_ssgiHistoryIndex]; }
    [[nodiscard]] uint32_t ssgiTemporalTexture() const { return m_ssgiTemporalTex; }
    [[nodiscard]] uint32_t ssgiTemporalMomentsTexture() const { return m_ssgiTemporalMomentsTex; }
    void swapSsgiHistory() { m_ssgiHistoryIndex = 1 - m_ssgiHistoryIndex; }
    void copySsgiDenoiseToSsgi(int slot);
    void copySsgiTemporalToSsgi();
    void copySsgiTemporalToHistory();
    [[nodiscard]] uint32_t sceneLightingTexture() const { return m_sceneLightingTex; }
    [[nodiscard]] uint32_t sceneCompositeTexture() const { return m_sceneCompositeTex; }
    [[nodiscard]] uint32_t sceneResolvedTexture() const { return m_sceneResolvedTex; }
    [[nodiscard]] RhiTextureHandle sceneResolvedTextureHandle() const { return m_sceneResolvedHandle; }
    [[nodiscard]] uint32_t transparentCompositeTexture() const { return m_transparentCompositeTex; }
    [[nodiscard]] uint32_t transparentCompositeDepthTexture() const { return m_transparentCompositeDepth; }
    [[nodiscard]] uint32_t halfResTexture() const { return m_halfResTex; }
    [[nodiscard]] uint32_t reflectionTexture() const { return m_reflectionTex; }
    [[nodiscard]] uint32_t reflectionTemporalScratchTexture() const { return m_reflectionTemporalScratchTex; }
    [[nodiscard]] uint32_t cloudTexture() const { return m_cloudTex; }
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
    [[nodiscard]] uint32_t velocityTexture() const { return m_velocityTex; }
    [[nodiscard]] uint32_t perObjectVelocityTexture() const { return m_perObjectVelocityTex; }
    [[nodiscard]] uint32_t weatherMaskTexture() const { return m_weatherMaskTex; }
    [[nodiscard]] RhiTextureHandle weatherMaskTextureHandle() const { return m_weatherMaskHandle; }
    [[nodiscard]] uint32_t atmosphereLutTexture() const { return m_atmosphereLut3d; }
    bool loadAtmosphereLut(const char* path);
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }
    [[nodiscard]] uint32_t fullscreenVao() const { return m_fullscreenVao; }
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

    static void generateMipmaps(uint32_t texture);
    static bool checkFramebufferComplete(uint32_t framebuffer, const char* label);
    bool registerRhiTextures();
    void unregisterRhiTextures();
    void destroyFramebuffers();
    void destroyFullscreenTriangle();

    // G-buffer contract:
    // 0 RGBA8    = linear albedo.rgb, emissive hint.a
    // 1 RGBA16F  = encoded world normal.rgb, vertex AO.a
    // 2 RG8      = sky light.r, block light.g
    // 3 RGBA8    = roughness.r, f0.g, emission.b, subsurface.a
    // 4 RGBA8    = DerivativeMain material id.r, wetness mask.g, porosity.b, metalness.a
    uint32_t m_gBufferFbo = 0;
    uint32_t m_gAlbedo = 0;
    uint32_t m_gNormalAo = 0;
    uint32_t m_gVoxelLight = 0;
    uint32_t m_gMaterial = 0;
    uint32_t m_gMaterialAux = 0;
    uint32_t m_gDepth = 0;
    RhiTextureHandle m_gDepthHandle;

    uint32_t m_shadowFbo = 0;
    uint32_t m_shadowDepth = 0;
    uint32_t m_shadowDepthComparison = 0; // Zero-copy comparison view for sampler2DShadow
    uint32_t m_shadowColor = 0;   // RGBA8: albedo color for colored shadows / caustics
    uint32_t m_shadowNormal = 0;  // RGBA16F: encoded normal.rg, skylight.b, aux/height.a
    uint32_t m_csmShadowFbo = 0;
    uint32_t m_csmShadowDepth = 0; // Raw depth texture array, one layer per cascade
    uint32_t m_csmShadowDepthComparison = 0; // Comparison texture array for sampler2DArrayShadow
    RhiTextureHandle m_csmShadowDepthHandle;
    RhiTextureHandle m_csmShadowDepthComparisonHandle;
    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    uint32_t m_csmShadowTransparentFbo = 0;
    uint32_t m_csmShadowDepthAll = 0; // depth including water/transparent surfaces
    uint32_t m_csmShadowDepthAllComparison = 0;
    uint32_t m_csmShadowColor0 = 0; // RGBA8: RGB caustics/tint, A transparent flag
    uint32_t m_csmShadowColor1 = 0; // RGBA16F: RG normal, B skylight, A water height
    RhiTextureHandle m_csmShadowDepthAllHandle;
    RhiTextureHandle m_csmShadowDepthAllComparisonHandle;
    RhiTextureHandle m_csmShadowColor0Handle;
    RhiTextureHandle m_csmShadowColor1Handle;

    uint32_t m_ssaoFbo = 0;
    uint32_t m_ssaoTex = 0;
    uint32_t m_ssaoFilteredFbo = 0;
    uint32_t m_ssaoFilteredTex = 0;
    // Half-res SSAO: raw and filtered at width/2 x height/2
    uint32_t m_ssaoHalfResFbo = 0;
    uint32_t m_ssaoHalfResTex = 0;
    uint32_t m_ssaoHalfResFilteredFbo = 0;
    uint32_t m_ssaoHalfResFilteredTex = 0;
    // SSAO temporal history ping-pong (R8)
    uint32_t m_ssaoHistoryFbo[2] = {0, 0};
    uint32_t m_ssaoHistoryTex[2] = {0, 0};
    int m_ssaoHistoryIndex = 0;
    // SSAO temporal resolve output (R8) — deferred lighting reads from this
    uint32_t m_ssaoTemporalFbo = 0;
    uint32_t m_ssaoTemporalTex = 0;

    uint32_t m_ssgiFbo = 0;
    uint32_t m_ssgiTex = 0;
    uint32_t m_ssgiHalfResFbo = 0;
    uint32_t m_ssgiHalfResTex = 0;
    uint32_t m_ssgiDenoiseFbo[2] = {0, 0};
    uint32_t m_ssgiDenoiseTex[2] = {0, 0};
    uint32_t m_ssgiHistoryFbo[2] = {0, 0};
    uint32_t m_ssgiHistoryTex[2] = {0, 0};
    uint32_t m_ssgiMomentsHistoryTex[2] = {0, 0};
    int m_ssgiHistoryIndex = 0;
    uint32_t m_ssgiTemporalFbo = 0;
    uint32_t m_ssgiTemporalTex = 0;
    uint32_t m_ssgiTemporalMomentsTex = 0;

    uint32_t m_sceneLightingFbo = 0;
    uint32_t m_sceneLightingTex = 0;

    // SceneComposite is the opaque HDR scene after screen-space base effects such as clouds/reflections.
    uint32_t m_sceneCompositeFbo = 0;
    uint32_t m_sceneCompositeTex = 0;

    // SceneResolved is the current full-world HDR color. It becomes the post input and temporal scene history source.
    uint32_t m_sceneResolvedFbo = 0;
    uint32_t m_sceneResolvedTex = 0;
    RhiTextureHandle m_sceneResolvedHandle;

    // TransparentComposite is a scratch scene copy used while forward water/generic transparent geometry is blended.
    uint32_t m_transparentCompositeFbo = 0;
    uint32_t m_transparentCompositeTex = 0;
    uint32_t m_transparentCompositeDepth = 0;

    uint32_t m_halfResFbo = 0;
    uint32_t m_halfResTex = 0;

    uint32_t m_reflectionFbo = 0;
    uint32_t m_reflectionTex = 0;

    // Reflection temporal scratch: holds filtered reflection copy while
    // temporal pass reads it and writes blended result to m_reflectionFbo.
    uint32_t m_reflectionTemporalScratchFbo = 0;
    uint32_t m_reflectionTemporalScratchTex = 0;

    uint32_t m_cloudFbo = 0;
    uint32_t m_cloudTex = 0;

    uint32_t m_skyCaptureFbo = 0;
    uint32_t m_skyCaptureTex = 0;

    // History ping-pong for temporal accumulation
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
    bool m_rebuiltSinceCheck = false;

    // TAA current-frame scratch: avoids reading history[current] as TAA input.
    uint32_t m_temporalCurrentFbo = 0;
    uint32_t m_temporalCurrentTex = 0;

    // Velocity buffer (RG16F encodes screen-space velocity xy)
    uint32_t m_velocityFbo = 0;
    uint32_t m_velocityTex = 0;

    // Per-object velocity (RG16F): written by entity/drop GBuffer shaders via MRT
    // as the sixth color attachment on the GBuffer FBO. Consumed by velocity_resolve.fs.
    uint32_t m_perObjectVelocityTex = 0;

    // Weather mask: single-channel R8 storing accumulated weather particle alpha.
    // Equivalent to DerivativeMain colortex0.b from gbuffers_weather.
    // Written with additive blending by weather geometry, read by postprocess.
    uint32_t m_weatherMaskFbo = 0;
    uint32_t m_weatherMaskTex = 0;
    RhiTextureHandle m_weatherMaskHandle;

    // Atmosphere precomputed scattering LUT (256x128x33 RGBA32F 3D texture)
    uint32_t m_atmosphereLut3d = 0;

    uint32_t m_fullscreenVao = 0;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_DEFERRED_RENDER_TARGETS_H
