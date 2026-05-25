#ifndef MECRAFT_SHADOW_TARGETS_H
#define MECRAFT_SHADOW_TARGETS_H

#include <glad/glad.h>

/// Shadow map targets.
/// Manages CSM shadow depth array and transparent shadow color/depth.
/// Initially used by Deferred pipeline; can be shared with Forward pipeline in the future.
class ShadowTargets {
public:
    static constexpr int CASCADE_COUNT = 4;

    ~ShadowTargets();

    bool init();
    void shutdown();

    /// Resize shadow map to new resolution
    bool ensureSize(int shadowResolution);

    // Bind operations
    void bindCsmShadowLayer(int cascadeIndex);
    void bindCsmShadowTransparentLayer(int cascadeIndex);

    // CSM shadow texture accessors
    [[nodiscard]] GLuint csmShadowDepthTexture() const { return m_csmShadowDepth; }
    [[nodiscard]] GLuint csmShadowDepthComparisonTexture() const { return m_csmShadowDepthComparison; }

    // CSM transparent shadow accessors
    [[nodiscard]] GLuint csmShadowDepthAllTexture() const { return m_csmShadowDepthAll; }
    [[nodiscard]] GLuint csmShadowDepthAllComparisonTexture() const { return m_csmShadowDepthAllComparison; }
    [[nodiscard]] GLuint csmShadowColor0Texture() const { return m_csmShadowColor0; }
    [[nodiscard]] GLuint csmShadowColor1Texture() const { return m_csmShadowColor1; }

    // Dimensions
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] int cascadeCount() const { return CASCADE_COUNT; }
    [[nodiscard]] bool isReady() const { return m_ready; }

private:
    static GLuint createTexture2DArray(GLenum internalFormat, int width, int height,
                                       int layers, GLenum minFilter, GLenum magFilter, GLenum wrap);
    static bool checkFramebufferComplete(GLuint framebuffer, const char* label);
    void destroyFramebuffers();

    // CSM shadow: 4-cascade depth texture array
    GLuint m_csmShadowFbo = 0;
    GLuint m_csmShadowDepth = 0;
    GLuint m_csmShadowDepthComparison = 0;

    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    GLuint m_csmShadowTransparentFbo = 0;
    GLuint m_csmShadowDepthAll = 0;
    GLuint m_csmShadowDepthAllComparison = 0;
    GLuint m_csmShadowColor0 = 0;
    GLuint m_csmShadowColor1 = 0;

    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_SHADOW_TARGETS_H
