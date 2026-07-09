#ifndef MECRAFT_COMMON_FRAME_TARGETS_H
#define MECRAFT_COMMON_FRAME_TARGETS_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>

/// Common frame targets shared by both Forward and Deferred pipelines.
/// These are resources that post-process, bloom, exposure, and basic scene rendering need.
/// Forward pipeline uses these; Deferred pipeline owns additional targets in DeferredRenderTargets.
class CommonFrameTargets {
public:
    ~CommonFrameTargets();

    bool init();
    void shutdown();

    /// Resize targets to match window dimensions
    bool ensureSize(int width, int height);

    // Texture accessors
    [[nodiscard]] RhiTextureHandle sceneColorTextureHandle() const { return m_sceneColor; }
    [[nodiscard]] RhiTextureHandle sceneDepthTextureHandle() const { return m_sceneDepth; }

    // Dimensions
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] bool isReady() const { return m_ready; }

    // Fullscreen utility
    [[nodiscard]] uint32_t fullscreenVao() const { return m_fullscreenVao; }

private:
    void destroyFramebuffers();
    void destroyFullscreenTriangle();

    // Scene color (RGBA16F HDR) - main render target for both pipelines
    RhiTextureHandle m_sceneColor;

    // Scene depth (DEPTH32F) - shared depth buffer
    RhiTextureHandle m_sceneDepth;

    // Fullscreen triangle for post-process passes
    uint32_t m_fullscreenVao = 0;

    int m_width = 0;
    int m_height = 0;
    bool m_ready = false;
};

#endif // MECRAFT_COMMON_FRAME_TARGETS_H
