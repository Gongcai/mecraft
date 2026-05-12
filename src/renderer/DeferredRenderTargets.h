#ifndef MECRAFT_DEFERRED_RENDER_TARGETS_H
#define MECRAFT_DEFERRED_RENDER_TARGETS_H

#include <glad/glad.h>

class DeferredRenderTargets {
public:
    ~DeferredRenderTargets();

    bool init();
    void shutdown();

    bool ensureSize(int width, int height, int shadowResolution);

    void bindGBuffer();
    void bindShadowMap();
    void bindShadowColor();
    void bindSsao();
    void bindSsaoFiltered();
    void bindSceneLighting();
    void bindSceneComposite();
    void bindSceneResolved();
    void bindTransparentComposite();
    void bindHalfRes();
    void bindReflection();
    void bindCloud();
    void bindVelocity();
    void bindDefaultLike(GLint framebuffer, int width, int height);
    void copyFramebufferColorToSceneLighting(GLint framebuffer, int width, int height) const;
    void copyFramebufferColorToSceneResolved(GLint framebuffer, int width, int height) const;
    void copyFramebufferColorToTransparentComposite(GLint framebuffer, int width, int height) const;
    void copySceneLightingToTransparentComposite() const;
    void copySceneLightingToSceneComposite() const;
    void copySceneCompositeToSceneResolved() const;
    void copySceneCompositeToTransparentComposite() const;
    void copySceneResolvedToTransparentComposite() const;
    void copyDepthToTransparentComposite() const;
    void copySceneResolvedToHistory() const;
    void copyDepthToHistory() const;
    void copyReflectionToHistory() const;
    void copyCloudToHistory() const;
    void blitSceneLightingTo(GLint framebuffer, int width, int height) const;
    void blitSceneCompositeTo(GLint framebuffer, int width, int height) const;
    void blitSceneResolvedTo(GLint framebuffer, int width, int height) const;
    void blitTransparentCompositeTo(GLint framebuffer, int width, int height) const;
    void blitDepthTo(GLint framebuffer, int width, int height) const;

    [[nodiscard]] GLuint albedoTexture() const { return m_gAlbedo; }
    [[nodiscard]] GLuint normalAoTexture() const { return m_gNormalAo; }
    [[nodiscard]] GLuint voxelLightTexture() const { return m_gVoxelLight; }
    [[nodiscard]] GLuint materialTexture() const { return m_gMaterial; }
    [[nodiscard]] GLuint materialAuxTexture() const { return m_gMaterialAux; }
    [[nodiscard]] GLuint depthTexture() const { return m_gDepth; }
    [[nodiscard]] GLuint shadowDepthTexture() const { return m_shadowDepth; }
    [[nodiscard]] GLuint shadowColorTexture() const { return m_shadowColor; }
    [[nodiscard]] GLuint shadowNormalTexture() const { return m_shadowNormal; }
    [[nodiscard]] GLuint ssaoTexture() const { return m_ssaoTex; }
    [[nodiscard]] GLuint ssaoFilteredTexture() const { return m_ssaoFilteredTex; }
    [[nodiscard]] GLuint sceneLightingTexture() const { return m_sceneLightingTex; }
    [[nodiscard]] GLuint sceneCompositeTexture() const { return m_sceneCompositeTex; }
    [[nodiscard]] GLuint sceneResolvedTexture() const { return m_sceneResolvedTex; }
    [[nodiscard]] GLuint transparentCompositeTexture() const { return m_transparentCompositeTex; }
    [[nodiscard]] GLuint transparentCompositeDepthTexture() const { return m_transparentCompositeDepth; }
    [[nodiscard]] GLuint halfResTexture() const { return m_halfResTex; }
    [[nodiscard]] GLuint reflectionTexture() const { return m_reflectionTex; }
    [[nodiscard]] GLuint cloudTexture() const { return m_cloudTex; }
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
    [[nodiscard]] GLuint velocityTexture() const { return m_velocityTex; }
    [[nodiscard]] GLuint atmosphereLutTexture() const { return m_atmosphereLut3d; }
    bool loadAtmosphereLut(const char* path);
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }
    [[nodiscard]] GLuint fullscreenVao() const { return m_fullscreenVao; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] bool isReady() const { return m_ready; }

private:
    // Sky capture: 256x514 equirectangular map (matches DerivativeMain colortex5).
    // skyCaptureRes = ivec2(255, 256), texture = 256 wide x 514 tall.
    // Rows 0..257:   raw atmospheric sky radiance (equirectangular).
    // Rows 258..513: cloudy skybox (sky + clouds composited).
    // Column 255, rows 0-5: metadata texels
    //   (directIlluminance, skyIlluminance, sunIlluminance, moonIlluminance, unused, cloudDynamicWeather).
    static constexpr int kSkyCaptureWidth = 256;
    static constexpr int kSkyCaptureHeight = 514;
    static constexpr GLenum kGAlbedoAttachment = GL_COLOR_ATTACHMENT0;
    static constexpr GLenum kGNormalAoAttachment = GL_COLOR_ATTACHMENT1;
    static constexpr GLenum kGVoxelLightAttachment = GL_COLOR_ATTACHMENT2;
    static constexpr GLenum kGMaterialAttachment = GL_COLOR_ATTACHMENT3;
    static constexpr GLenum kGMaterialAuxAttachment = GL_COLOR_ATTACHMENT4;
    static constexpr GLsizei kGBufferAttachmentCount = 5;

    static GLuint createTexture2D(GLenum internalFormat,
                                  int width,
                                  int height,
                                  GLenum format,
                                  GLenum type,
                                  GLenum minFilter,
                                  GLenum magFilter,
                                  GLenum wrap,
                                  int levels = 1);

    static void generateMipmaps(GLuint texture);
    static bool checkFramebufferComplete(GLuint framebuffer, const char* label);
    void destroyFramebuffers();
    void destroyFullscreenTriangle();

    // G-buffer contract:
    // 0 RGBA8    = linear albedo.rgb, emissive hint.a
    // 1 RGBA16F  = encoded world normal.rgb, vertex AO.a
    // 2 RG8      = sky light.r, block light.g
    // 3 RGBA8    = roughness.r, f0.g, emission.b, subsurface.a
    // 4 RGBA8    = DerivativeMain material id.r, wetness mask.g, porosity.b, metalness.a
    GLuint m_gBufferFbo = 0;
    GLuint m_gAlbedo = 0;
    GLuint m_gNormalAo = 0;
    GLuint m_gVoxelLight = 0;
    GLuint m_gMaterial = 0;
    GLuint m_gMaterialAux = 0;
    GLuint m_gDepth = 0;

    GLuint m_shadowFbo = 0;
    GLuint m_shadowDepth = 0;
    GLuint m_shadowColor = 0;   // RGBA8: albedo color for colored shadows / caustics
    GLuint m_shadowNormal = 0;  // RGBA16F: encoded normal.rg, skylight.b, aux/height.a

    GLuint m_ssaoFbo = 0;
    GLuint m_ssaoTex = 0;
    GLuint m_ssaoFilteredFbo = 0;
    GLuint m_ssaoFilteredTex = 0;

    GLuint m_sceneLightingFbo = 0;
    GLuint m_sceneLightingTex = 0;

    // SceneComposite is the opaque HDR scene after screen-space base effects such as clouds/reflections.
    GLuint m_sceneCompositeFbo = 0;
    GLuint m_sceneCompositeTex = 0;

    // SceneResolved is the current full-world HDR color. It becomes the post input and temporal scene history source.
    GLuint m_sceneResolvedFbo = 0;
    GLuint m_sceneResolvedTex = 0;

    // TransparentComposite is a scratch scene copy used while forward water/generic transparent geometry is blended.
    GLuint m_transparentCompositeFbo = 0;
    GLuint m_transparentCompositeTex = 0;
    GLuint m_transparentCompositeDepth = 0;

    GLuint m_halfResFbo = 0;
    GLuint m_halfResTex = 0;

    GLuint m_reflectionFbo = 0;
    GLuint m_reflectionTex = 0;

    GLuint m_cloudFbo = 0;
    GLuint m_cloudTex = 0;

    GLuint m_skyCaptureFbo = 0;
    GLuint m_skyCaptureTex = 0;

    // History ping-pong for temporal accumulation
    GLuint m_historySceneFbo[2] = {0, 0};
    GLuint m_historySceneTex[2] = {0, 0};
    GLuint m_historyDepthTex[2] = {0, 0};
    GLuint m_historyReflectionFbo[2] = {0, 0};
    GLuint m_historyReflectionTex[2] = {0, 0};
    GLuint m_historyCloudFbo[2] = {0, 0};
    GLuint m_historyCloudTex[2] = {0, 0};
    int m_currentHistoryIndex = 0;

    // Velocity buffer (RG16F encodes screen-space velocity xy)
    GLuint m_velocityFbo = 0;
    GLuint m_velocityTex = 0;

    // Atmosphere precomputed scattering LUT (256x128x33 RGBA32F 3D texture)
    GLuint m_atmosphereLut3d = 0;

    GLuint m_fullscreenVao = 0;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_DEFERRED_RENDER_TARGETS_H
