#include "ShadowTargets.h"
#include "../../Diagnostics.h"
#include "../debug/RenderDebugLabels.h"

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

bool checkShadowFramebufferComplete(uint32_t framebuffer, const char* label) {
    GLenum status = glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        MECRAFT_LOG_FPRINTF(stderr, "ShadowTargets: %s FBO incomplete (status 0x%X)\n", label, status);
        return false;
    }
    return true;
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

    // CSM shadow depth array
    glGenFramebuffers(1, &m_csmShadowFbo);
    m_csmShadowDepth = createShadowTexture2DArray(GL_DEPTH_COMPONENT24,
                                                  m_shadowResolution, m_shadowResolution,
                                                  CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowDepth, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Comparison view for sampler2DArrayShadow
    glGenTextures(1, &m_csmShadowDepthComparison);
    glTextureView(m_csmShadowDepthComparison, GL_TEXTURE_2D_ARRAY,
                  m_csmShadowDepth, GL_DEPTH_COMPONENT24, 0, 1, 0, CASCADE_COUNT);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(m_csmShadowDepthComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Bind first layer to FBO
    glNamedFramebufferTextureLayer(m_csmShadowFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepth, 0, 0);
    glNamedFramebufferDrawBuffer(m_csmShadowFbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_csmShadowFbo, GL_NONE);

    if (!checkShadowFramebufferComplete(m_csmShadowFbo, "CsmShadowMap")) {
        destroyFramebuffers();
        return false;
    }

    // CSM transparent shadow
    glGenFramebuffers(1, &m_csmShadowTransparentFbo);
    m_csmShadowDepthAll = createShadowTexture2DArray(GL_DEPTH_COMPONENT24,
                                                     m_shadowResolution, m_shadowResolution,
                                                     CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowDepthAll, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    glGenTextures(1, &m_csmShadowDepthAllComparison);
    glTextureView(m_csmShadowDepthAllComparison, GL_TEXTURE_2D_ARRAY,
                  m_csmShadowDepthAll, GL_DEPTH_COMPONENT24, 0, 1, 0, CASCADE_COUNT);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_csmShadowDepthAllComparison, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameterfv(m_csmShadowDepthAllComparison, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Shadow color textures
    m_csmShadowColor0 = createShadowTexture2DArray(GL_RGBA8,
                                                   m_shadowResolution, m_shadowResolution,
                                                   CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowColor0, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    m_csmShadowColor1 = createShadowTexture2DArray(GL_RGBA16F,
                                                   m_shadowResolution, m_shadowResolution,
                                                   CASCADE_COUNT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_csmShadowColor1, GL_TEXTURE_BORDER_COLOR, kBorderColor);

    // Bind first layer to transparent FBO
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_DEPTH_ATTACHMENT, m_csmShadowDepthAll, 0, 0);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT0, m_csmShadowColor0, 0, 0);
    glNamedFramebufferTextureLayer(m_csmShadowTransparentFbo, GL_COLOR_ATTACHMENT1, m_csmShadowColor1, 0, 0);
    const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glNamedFramebufferDrawBuffers(m_csmShadowTransparentFbo, 2, drawBuffers);

    if (!checkShadowFramebufferComplete(m_csmShadowTransparentFbo, "CsmShadowTransparent")) {
        destroyFramebuffers();
        return false;
    }

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelFramebuffer(m_csmShadowFbo, "ShadowTargets.CSMDepth");
    renderer::debug::labelTexture(m_csmShadowDepth, "ShadowTargets.CSMDepthArray");
    renderer::debug::labelTexture(m_csmShadowDepthComparison, "ShadowTargets.CSMDepthComparison");
    renderer::debug::labelFramebuffer(m_csmShadowTransparentFbo, "ShadowTargets.CSMTransparent");
    renderer::debug::labelTexture(m_csmShadowDepthAll, "ShadowTargets.CSMDepthAll");
    renderer::debug::labelTexture(m_csmShadowDepthAllComparison, "ShadowTargets.CSMDepthAllComparison");
    renderer::debug::labelTexture(m_csmShadowColor0, "ShadowTargets.CSMTransparentColor0");
    renderer::debug::labelTexture(m_csmShadowColor1, "ShadowTargets.CSMTransparentColor1");

    m_ready = true;
    return true;
}

void ShadowTargets::bindCsmShadowLayer(int cascadeIndex) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_csmShadowFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_csmShadowDepth, 0, cascadeIndex);
    int res = (cascadeIndex >= 2) ? m_shadowResolution / 2 : m_shadowResolution;
    glViewport(0, 0, res, res);
}

void ShadowTargets::bindCsmShadowTransparentLayer(int cascadeIndex) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_csmShadowTransparentFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_csmShadowDepthAll, 0, cascadeIndex);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_csmShadowColor0, 0, cascadeIndex);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, m_csmShadowColor1, 0, cascadeIndex);
    int res = (cascadeIndex >= 2) ? m_shadowResolution / 2 : m_shadowResolution;
    glViewport(0, 0, res, res);
}

void ShadowTargets::destroyFramebuffers() {
    if (m_csmShadowFbo) { glDeleteFramebuffers(1, &m_csmShadowFbo); m_csmShadowFbo = 0; }
    if (m_csmShadowDepth) { glDeleteTextures(1, &m_csmShadowDepth); m_csmShadowDepth = 0; }
    if (m_csmShadowDepthComparison) { glDeleteTextures(1, &m_csmShadowDepthComparison); m_csmShadowDepthComparison = 0; }
    if (m_csmShadowTransparentFbo) { glDeleteFramebuffers(1, &m_csmShadowTransparentFbo); m_csmShadowTransparentFbo = 0; }
    if (m_csmShadowDepthAll) { glDeleteTextures(1, &m_csmShadowDepthAll); m_csmShadowDepthAll = 0; }
    if (m_csmShadowDepthAllComparison) { glDeleteTextures(1, &m_csmShadowDepthAllComparison); m_csmShadowDepthAllComparison = 0; }
    if (m_csmShadowColor0) { glDeleteTextures(1, &m_csmShadowColor0); m_csmShadowColor0 = 0; }
    if (m_csmShadowColor1) { glDeleteTextures(1, &m_csmShadowColor1); m_csmShadowColor1 = 0; }
}
