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
    void bindSsao();
    void bindSceneLighting();
    void bindTransparentComposite();
    void bindHalfRes();
    void bindDefaultLike(GLint framebuffer, int width, int height);
    void copyFramebufferColorToSceneLighting(GLint framebuffer, int width, int height) const;
    void copyFramebufferColorToTransparentComposite(GLint framebuffer, int width, int height) const;
    void copySceneLightingToTransparentComposite() const;
    void copyDepthToTransparentComposite() const;
    void blitSceneLightingTo(GLint framebuffer, int width, int height) const;
    void blitTransparentCompositeTo(GLint framebuffer, int width, int height) const;
    void blitDepthTo(GLint framebuffer, int width, int height) const;

    [[nodiscard]] GLuint albedoTexture() const { return m_gAlbedo; }
    [[nodiscard]] GLuint normalAoTexture() const { return m_gNormalAo; }
    [[nodiscard]] GLuint voxelLightTexture() const { return m_gVoxelLight; }
    [[nodiscard]] GLuint materialTexture() const { return m_gMaterial; }
    [[nodiscard]] GLuint depthTexture() const { return m_gDepth; }
    [[nodiscard]] GLuint shadowDepthTexture() const { return m_shadowDepth; }
    [[nodiscard]] GLuint ssaoTexture() const { return m_ssaoTex; }
    [[nodiscard]] GLuint sceneLightingTexture() const { return m_sceneLightingTex; }
    [[nodiscard]] GLuint transparentCompositeTexture() const { return m_transparentCompositeTex; }
    [[nodiscard]] GLuint transparentCompositeDepthTexture() const { return m_transparentCompositeDepth; }
    [[nodiscard]] GLuint halfResTexture() const { return m_halfResTex; }
    [[nodiscard]] GLuint skyCaptureFramebuffer() const { return m_skyCaptureFbo; }
    [[nodiscard]] GLuint skyCaptureTexture() const { return m_skyCaptureTex; }
    [[nodiscard]] int skyCaptureWidth() const { return kSkyCaptureWidth; }
    [[nodiscard]] int skyCaptureHeight() const { return kSkyCaptureHeight; }
    // History ping-pong for temporal accumulation
    [[nodiscard]] GLuint historySceneTexture() const { return m_historySceneTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historySceneTexturePrev() const { return m_historySceneTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyDepthTexture() const { return m_historyDepthTex[m_currentHistoryIndex]; }
    [[nodiscard]] GLuint historyDepthTexturePrev() const { return m_historyDepthTex[1 - m_currentHistoryIndex]; }
    [[nodiscard]] GLuint velocityTexture() const { return m_velocityTex; }
    [[nodiscard]] int currentHistoryIndex() const { return m_currentHistoryIndex; }
    void swapHistory() { m_currentHistoryIndex = 1 - m_currentHistoryIndex; }
    [[nodiscard]] GLuint fullscreenVao() const { return m_fullscreenVao; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] bool isReady() const { return m_ready; }

private:
    static constexpr int kSkyCaptureWidth = 256;
    static constexpr int kSkyCaptureHeight = 128;
    static constexpr GLenum kGAlbedoAttachment = GL_COLOR_ATTACHMENT0;
    static constexpr GLenum kGNormalAoAttachment = GL_COLOR_ATTACHMENT1;
    static constexpr GLenum kGVoxelLightAttachment = GL_COLOR_ATTACHMENT2;
    static constexpr GLenum kGMaterialAttachment = GL_COLOR_ATTACHMENT3;
    static constexpr GLsizei kGBufferAttachmentCount = 4;

    static GLuint createTexture2D(GLenum internalFormat,
                                  int width,
                                  int height,
                                  GLenum format,
                                  GLenum type,
                                  GLenum minFilter,
                                  GLenum magFilter,
                                  GLenum wrap);
    static bool checkFramebufferComplete(GLuint framebuffer, const char* label);
    void destroyFramebuffers();
    void destroyFullscreenTriangle();

    // G-buffer contract:
    // 0 RGBA8    = linear albedo.rgb, emissive hint.a
    // 1 RGBA16F  = encoded world normal.rgb, vertex AO.a
    // 2 RG8      = sky light.r, block light.g
    // 3 RGBA8    = roughness.r, f0.g, emission.b, subsurface.a
    GLuint m_gBufferFbo = 0;
    GLuint m_gAlbedo = 0;
    GLuint m_gNormalAo = 0;
    GLuint m_gVoxelLight = 0;
    GLuint m_gMaterial = 0;
    GLuint m_gDepth = 0;

    GLuint m_shadowFbo = 0;
    GLuint m_shadowDepth = 0;

    GLuint m_ssaoFbo = 0;
    GLuint m_ssaoTex = 0;

    GLuint m_sceneLightingFbo = 0;
    GLuint m_sceneLightingTex = 0;

    GLuint m_transparentCompositeFbo = 0;
    GLuint m_transparentCompositeTex = 0;
    GLuint m_transparentCompositeDepth = 0;

    GLuint m_halfResFbo = 0;
    GLuint m_halfResTex = 0;

    GLuint m_skyCaptureFbo = 0;
    GLuint m_skyCaptureTex = 0;

    // History ping-pong for temporal accumulation
    GLuint m_historySceneFbo[2] = {0, 0};
    GLuint m_historySceneTex[2] = {0, 0};
    GLuint m_historyDepthTex[2] = {0, 0};
    int m_currentHistoryIndex = 0;

    // Velocity buffer (RG16F encodes screen-space velocity xy)
    GLuint m_velocityFbo = 0;
    GLuint m_velocityTex = 0;

    GLuint m_fullscreenVao = 0;

    int m_width = 0;
    int m_height = 0;
    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_DEFERRED_RENDER_TARGETS_H
