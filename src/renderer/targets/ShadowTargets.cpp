#include "ShadowTargets.h"
#include "../../Diagnostics.h"
#include "../debug/RenderDebugLabels.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <cstdio>

namespace {
uint32_t createShadowTexture2DArray(GLenum internalFormat,
                                    int width,
                                    int height,
                                    int layers,
                                    GLenum minFilter,
                                    GLenum magFilter,
                                    GLenum wrap) {
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glTextureStorage3D(tex, 1, internalFormat, width, height, layers);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, minFilter);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, magFilter);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, wrap);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, wrap);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return tex;
}

void deleteTexture(uint32_t& texture) {
    if (texture != 0) {
        const GLuint glTexture = static_cast<GLuint>(texture);
        glDeleteTextures(1, &glTexture);
        texture = 0;
    }
}
} // namespace

ShadowTargets::~ShadowTargets() {
    shutdown();
}

bool ShadowTargets::init() {
    return true;
}

void ShadowTargets::shutdown() {
    destroyFramebuffers();
    m_ready = false;
}

bool ShadowTargets::ensureSize(int shadowResolution) {
    int targetShadow = shadowResolution < 256 ? 256 : shadowResolution;
    if (m_ready && m_shadowResolution == targetShadow) {
        return false; // No resize needed
    }

    destroyFramebuffers();
    m_shadowResolution = targetShadow;

    constexpr float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};

    uint32_t csmShadowDepth = 0;
    uint32_t csmShadowDepthComparison = 0;
    uint32_t csmShadowDepthAll = 0;
    uint32_t csmShadowDepthAllComparison = 0;
    uint32_t csmShadowColor0 = 0;
    uint32_t csmShadowColor1 = 0;
    RhiTextureHandle csmShadowDepthHandle;
    RhiTextureHandle csmShadowDepthComparisonHandle;
    RhiTextureHandle csmShadowDepthAllHandle;
    RhiTextureHandle csmShadowDepthAllComparisonHandle;
    RhiTextureHandle csmShadowColor0Handle;
    RhiTextureHandle csmShadowColor1Handle;

    auto destroyPending = [&]() {
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowDepthHandle);
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowDepthComparisonHandle);
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowDepthAllHandle);
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowDepthAllComparisonHandle);
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowColor0Handle);
        renderer::rhi::gl::unregisterTextureAndReset(csmShadowColor1Handle);
        deleteTexture(csmShadowDepth);
        deleteTexture(csmShadowDepthComparison);
        deleteTexture(csmShadowDepthAll);
        deleteTexture(csmShadowDepthAllComparison);
        deleteTexture(csmShadowColor0);
        deleteTexture(csmShadowColor1);
    };

    // CSM shadow depth array
    csmShadowDepth = createShadowTexture2DArray(GL_DEPTH_COMPONENT24,
                                                m_shadowResolution, m_shadowResolution,
                                                CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(csmShadowDepth, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Comparison view for sampler2DArrayShadow
    glGenTextures(1, &csmShadowDepthComparison);
    glTextureView(csmShadowDepthComparison, GL_TEXTURE_2D_ARRAY,
                  csmShadowDepth, GL_DEPTH_COMPONENT24, 0, 1, 0, CASCADE_COUNT);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(csmShadowDepthComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(csmShadowDepthComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // CSM transparent shadow
    csmShadowDepthAll = createShadowTexture2DArray(GL_DEPTH_COMPONENT24,
                                                   m_shadowResolution, m_shadowResolution,
                                                   CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(csmShadowDepthAll, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    glGenTextures(1, &csmShadowDepthAllComparison);
    glTextureView(csmShadowDepthAllComparison, GL_TEXTURE_2D_ARRAY,
                  csmShadowDepthAll, GL_DEPTH_COMPONENT24, 0, 1, 0, CASCADE_COUNT);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(csmShadowDepthAllComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(csmShadowDepthAllComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Shadow color textures
    csmShadowColor0 = createShadowTexture2DArray(GL_RGBA8,
                                                 m_shadowResolution, m_shadowResolution,
                                                 CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(csmShadowColor0, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    csmShadowColor1 = createShadowTexture2DArray(GL_RGBA16F,
                                                 m_shadowResolution, m_shadowResolution,
                                                 CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(csmShadowColor1, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    csmShadowDepthHandle = renderer::rhi::gl::registerTexture({
        csmShadowDepth,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    csmShadowDepthComparisonHandle = renderer::rhi::gl::registerTexture({
        csmShadowDepthComparison,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        true
    });
    csmShadowDepthAllHandle = renderer::rhi::gl::registerTexture({
        csmShadowDepthAll,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    csmShadowDepthAllComparisonHandle = renderer::rhi::gl::registerTexture({
        csmShadowDepthAllComparison,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Depth24,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled),
        true
    });
    csmShadowColor0Handle = renderer::rhi::gl::registerTexture({
        csmShadowColor0,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Rgba8Unorm,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    csmShadowColor1Handle = renderer::rhi::gl::registerTexture({
        csmShadowColor1,
        RhiTextureDimension::Texture2DArray,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(m_shadowResolution),
        static_cast<uint32_t>(CASCADE_COUNT),
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    const bool registered = csmShadowDepthHandle.isValid() &&
                            csmShadowDepthComparisonHandle.isValid() &&
                            csmShadowDepthAllHandle.isValid() &&
                            csmShadowDepthAllComparisonHandle.isValid() &&
                            csmShadowColor0Handle.isValid() &&
                            csmShadowColor1Handle.isValid();
    if (!registered) {
        MECRAFT_LOG_FPRINTF(stderr, "ShadowTargets: failed to register RHI texture handles\n");
        destroyPending();
        return false;
    }

    m_csmShadowDepthHandle = csmShadowDepthHandle;
    m_csmShadowDepthComparisonHandle = csmShadowDepthComparisonHandle;
    m_csmShadowDepthAllHandle = csmShadowDepthAllHandle;
    m_csmShadowDepthAllComparisonHandle = csmShadowDepthAllComparisonHandle;
    m_csmShadowColor0Handle = csmShadowColor0Handle;
    m_csmShadowColor1Handle = csmShadowColor1Handle;

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelTexture(csmShadowDepth, "ShadowTargets.CSMDepthArray");
    renderer::debug::labelTexture(csmShadowDepthComparison, "ShadowTargets.CSMDepthComparison");
    renderer::debug::labelTexture(csmShadowDepthAll, "ShadowTargets.CSMDepthAll");
    renderer::debug::labelTexture(csmShadowDepthAllComparison, "ShadowTargets.CSMDepthAllComparison");
    renderer::debug::labelTexture(csmShadowColor0, "ShadowTargets.CSMTransparentColor0");
    renderer::debug::labelTexture(csmShadowColor1, "ShadowTargets.CSMTransparentColor1");

    m_ready = true;
    return true;
}

void ShadowTargets::destroyFramebuffers() {
    uint32_t csmShadowDepth = renderer::rhi::gl::textureId(m_csmShadowDepthHandle);
    uint32_t csmShadowDepthComparison = renderer::rhi::gl::textureId(m_csmShadowDepthComparisonHandle);
    uint32_t csmShadowDepthAll = renderer::rhi::gl::textureId(m_csmShadowDepthAllHandle);
    uint32_t csmShadowDepthAllComparison = renderer::rhi::gl::textureId(m_csmShadowDepthAllComparisonHandle);
    uint32_t csmShadowColor0 = renderer::rhi::gl::textureId(m_csmShadowColor0Handle);
    uint32_t csmShadowColor1 = renderer::rhi::gl::textureId(m_csmShadowColor1Handle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthComparisonHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthAllHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowDepthAllComparisonHandle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowColor0Handle);
    renderer::rhi::gl::unregisterTextureAndReset(m_csmShadowColor1Handle);
    deleteTexture(csmShadowDepth);
    deleteTexture(csmShadowDepthComparison);
    deleteTexture(csmShadowDepthAll);
    deleteTexture(csmShadowDepthAllComparison);
    deleteTexture(csmShadowColor0);
    deleteTexture(csmShadowColor1);
}
