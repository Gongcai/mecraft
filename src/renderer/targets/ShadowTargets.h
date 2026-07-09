#ifndef MECRAFT_SHADOW_TARGETS_H
#define MECRAFT_SHADOW_TARGETS_H

#include "renderer/rhi/RhiHandles.h"

#include <cstdint>

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

    // CSM shadow texture accessors
    [[nodiscard]] RhiTextureHandle csmShadowDepthTextureHandle() const { return m_csmShadowDepthHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthComparisonTextureHandle() const { return m_csmShadowDepthComparisonHandle; }

    // CSM transparent shadow accessors
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllTextureHandle() const { return m_csmShadowDepthAllHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowDepthAllComparisonTextureHandle() const { return m_csmShadowDepthAllComparisonHandle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor0TextureHandle() const { return m_csmShadowColor0Handle; }
    [[nodiscard]] RhiTextureHandle csmShadowColor1TextureHandle() const { return m_csmShadowColor1Handle; }

    // Dimensions
    [[nodiscard]] int shadowResolution() const { return m_shadowResolution; }
    [[nodiscard]] int cascadeCount() const { return CASCADE_COUNT; }
    [[nodiscard]] bool isReady() const { return m_ready; }

private:
    void destroyFramebuffers();

    // CSM shadow: 4-cascade depth texture array
    RhiTextureHandle m_csmShadowDepthHandle;
    RhiTextureHandle m_csmShadowDepthComparisonHandle;

    // CSM transparent shadow: depth-all + color for water/transparent occlusion
    RhiTextureHandle m_csmShadowDepthAllHandle;
    RhiTextureHandle m_csmShadowDepthAllComparisonHandle;
    RhiTextureHandle m_csmShadowColor0Handle;
    RhiTextureHandle m_csmShadowColor1Handle;

    int m_shadowResolution = 0;
    bool m_ready = false;
};

#endif // MECRAFT_SHADOW_TARGETS_H
