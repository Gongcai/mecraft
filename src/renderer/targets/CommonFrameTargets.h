#ifndef MECRAFT_COMMON_FRAME_TARGETS_H
#define MECRAFT_COMMON_FRAME_TARGETS_H

#include <glad/glad.h>

/// Common frame targets shared by both Forward and Deferred pipelines.
/// These are resources that post-process, bloom, exposure, and basic scene rendering need.
/// Forward pipeline uses these; Deferred pipeline also uses these in addition to DeferredFrameTargets.
class CommonFrameTargets {
public:
    ~CommonFrameTargets();

    bool init();
    void shutdown();

    /// Resize targets to match window dimensions
    bool ensureSize(int width, int height);

    // Bind operations
    void bindSceneColor();
    void bindSceneDepth();

    // Texture accessors
    [[nodiscard]] GLuint sceneColorTexture() const { return m_sceneColorTex; }
    [[nodiscard]] GLuint sceneDepthTexture() const { return m_sceneDepthTex; }

    // Dimensions
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] bool isReady() const { return m_ready; }

    // Fullscreen utility
    [[nodiscard]] GLuint fullscreenVao() const { return m_fullscreenVao; }

private:
    void destroyFramebuffers();
    void destroyFullscreenTriangle();

    // Scene color (RGBA16F HDR) - main render target for both pipelines
    GLuint m_sceneColorFbo = 0;
    GLuint m_sceneColorTex = 0;

    // Scene depth (DEPTH32F) - shared depth buffer
    GLuint m_sceneDepthTex = 0;

    // Fullscreen triangle for post-process passes
    GLuint m_fullscreenVao = 0;

    int m_width = 0;
    int m_height = 0;
    bool m_ready = false;
};

#endif // MECRAFT_COMMON_FRAME_TARGETS_H
