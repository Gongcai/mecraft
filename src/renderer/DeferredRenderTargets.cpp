#include "DeferredRenderTargets.h"

#include <algorithm>
#include <iostream>

DeferredRenderTargets::~DeferredRenderTargets() {
    shutdown();
}

bool DeferredRenderTargets::init() {
    if (m_fullscreenVao == 0) {
        glGenVertexArrays(1, &m_fullscreenVao);
    }
    return m_fullscreenVao != 0;
}

void DeferredRenderTargets::shutdown() {
    destroyFramebuffers();
    destroyFullscreenTriangle();
    m_currentHistoryIndex = 0;
    m_width = 0;
    m_height = 0;
    m_shadowResolution = 0;
    m_ready = false;
}

bool DeferredRenderTargets::ensureSize(const int width, const int height, const int shadowResolution) {
    const int targetWidth = std::max(1, width);
    const int targetHeight = std::max(1, height);
    const int targetShadow = std::max(256, shadowResolution);
    if (m_ready && m_width == targetWidth && m_height == targetHeight && m_shadowResolution == targetShadow) {
        return true;
    }

    destroyFramebuffers();

    m_width = targetWidth;
    m_height = targetHeight;
    m_shadowResolution = targetShadow;

    glCreateFramebuffers(1, &m_gBufferFbo);

    m_gAlbedo = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gNormalAo = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gVoxelLight = createTexture2D(GL_RG8, m_width, m_height, GL_RG, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gMaterial = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gMaterialAux = createTexture2D(GL_RGBA8, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    glNamedFramebufferTexture(m_gBufferFbo, kGAlbedoAttachment, m_gAlbedo, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGNormalAoAttachment, m_gNormalAo, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGVoxelLightAttachment, m_gVoxelLight, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGMaterialAttachment, m_gMaterial, 0);
    glNamedFramebufferTexture(m_gBufferFbo, kGMaterialAuxAttachment, m_gMaterialAux, 0);
    glNamedFramebufferTexture(m_gBufferFbo, GL_DEPTH_ATTACHMENT, m_gDepth, 0);
    const GLenum gBufferDrawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment
    };
    glNamedFramebufferDrawBuffers(m_gBufferFbo, kGBufferAttachmentCount, gBufferDrawBuffers);
    if (!checkFramebufferComplete(m_gBufferFbo, "GBuffer")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_shadowFbo);
    m_shadowDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_shadowResolution, m_shadowResolution,
                                   GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_BORDER);
    constexpr float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(m_shadowDepth, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    glNamedFramebufferTexture(m_shadowFbo, GL_DEPTH_ATTACHMENT, m_shadowDepth, 0);
    glNamedFramebufferDrawBuffer(m_shadowFbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_shadowFbo, GL_NONE);
    if (!checkFramebufferComplete(m_shadowFbo, "ShadowMap")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_ssaoFbo);
    m_ssaoTex = createTexture2D(GL_R8, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_ssaoFbo, GL_COLOR_ATTACHMENT0, m_ssaoTex, 0);
    const GLenum ssaoDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_ssaoFbo, 1, &ssaoDrawBuffer);
    if (!checkFramebufferComplete(m_ssaoFbo, "SSAO")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_sceneLightingFbo);
    m_sceneLightingTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_sceneLightingFbo, GL_COLOR_ATTACHMENT0, m_sceneLightingTex, 0);
    const GLenum sceneLightingDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_sceneLightingFbo, 1, &sceneLightingDrawBuffer);
    if (!checkFramebufferComplete(m_sceneLightingFbo, "SceneLighting")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_transparentCompositeFbo);
    m_transparentCompositeTex = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    // Keep transparent depth separate from the sampled G-buffer depth to avoid feedback while drawing water/transparent materials.
    m_transparentCompositeDepth = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_transparentCompositeFbo, GL_COLOR_ATTACHMENT0, m_transparentCompositeTex, 0);
    glNamedFramebufferTexture(m_transparentCompositeFbo, GL_DEPTH_ATTACHMENT, m_transparentCompositeDepth, 0);
    const GLenum transparentCompositeDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_transparentCompositeFbo, 1, &transparentCompositeDrawBuffer);
    if (!checkFramebufferComplete(m_transparentCompositeFbo, "TransparentComposite")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_halfResFbo);
    const int halfWidth = std::max(1, m_width / 2);
    const int halfHeight = std::max(1, m_height / 2);
    m_halfResTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_halfResFbo, GL_COLOR_ATTACHMENT0, m_halfResTex, 0);
    const GLenum halfResDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_halfResFbo, 1, &halfResDrawBuffer);
    if (!checkFramebufferComplete(m_halfResFbo, "HalfRes")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_reflectionFbo);
    m_reflectionTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_reflectionFbo, GL_COLOR_ATTACHMENT0, m_reflectionTex, 0);
    const GLenum reflectionDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_reflectionFbo, 1, &reflectionDrawBuffer);
    if (!checkFramebufferComplete(m_reflectionFbo, "Reflection")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_cloudFbo);
    m_cloudTex = createTexture2D(GL_RGBA16F, halfWidth, halfHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_cloudFbo, GL_COLOR_ATTACHMENT0, m_cloudTex, 0);
    const GLenum cloudDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_cloudFbo, 1, &cloudDrawBuffer);
    if (!checkFramebufferComplete(m_cloudFbo, "Cloud")) {
        shutdown();
        return false;
    }

    glCreateFramebuffers(1, &m_skyCaptureFbo);
    m_skyCaptureTex = createTexture2D(GL_RGBA16F, kSkyCaptureWidth, kSkyCaptureHeight, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_skyCaptureFbo, GL_COLOR_ATTACHMENT0, m_skyCaptureTex, 0);
    const GLenum skyCaptureDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_skyCaptureFbo, 1, &skyCaptureDrawBuffer);
    if (!checkFramebufferComplete(m_skyCaptureFbo, "SkyCapture")) {
        shutdown();
        return false;
    }

    // History scene FBO ping-pong (RGBA16F color + depth)
    for (int i = 0; i < 2; ++i) {
        glCreateFramebuffers(1, &m_historySceneFbo[i]);
        m_historySceneTex[i] = createTexture2D(GL_RGBA16F, m_width, m_height, GL_RGBA, GL_FLOAT,
                                               GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_historyDepthTex[i] = createTexture2D(GL_DEPTH_COMPONENT32F, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT,
                                               GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(m_historySceneFbo[i], GL_COLOR_ATTACHMENT0, m_historySceneTex[i], 0);
        glNamedFramebufferTexture(m_historySceneFbo[i], GL_DEPTH_ATTACHMENT, m_historyDepthTex[i], 0);
        const GLenum historyDrawBuffer = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_historySceneFbo[i], 1, &historyDrawBuffer);
        if (!checkFramebufferComplete(m_historySceneFbo[i], "HistoryScene")) {
            shutdown();
            return false;
        }
    }
    m_currentHistoryIndex = 0;

    // Velocity buffer (RG16F)
    glCreateFramebuffers(1, &m_velocityFbo);
    m_velocityTex = createTexture2D(GL_RG16F, m_width, m_height, GL_RG, GL_FLOAT,
                                    GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(m_velocityFbo, GL_COLOR_ATTACHMENT0, m_velocityTex, 0);
    const GLenum velocityDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(m_velocityFbo, 1, &velocityDrawBuffer);
    if (!checkFramebufferComplete(m_velocityFbo, "Velocity")) {
        shutdown();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_ready = true;
    return true;
}

void DeferredRenderTargets::bindGBuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffers[] = {
        kGAlbedoAttachment,
        kGNormalAoAttachment,
        kGVoxelLightAttachment,
        kGMaterialAttachment,
        kGMaterialAuxAttachment
    };
    glDrawBuffers(kGBufferAttachmentCount, drawBuffers);
}

void DeferredRenderTargets::bindShadowMap() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, m_shadowResolution, m_shadowResolution);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void DeferredRenderTargets::bindSsao() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindSceneLighting() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindTransparentComposite() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindHalfRes() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_halfResFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindReflection() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindCloud() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_cloudFbo);
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindVelocity() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_velocityFbo);
    glViewport(0, 0, m_width, m_height);
    const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuffer);
}

void DeferredRenderTargets::bindDefaultLike(const GLint framebuffer, const int width, const int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

void DeferredRenderTargets::copyFramebufferColorToSceneLighting(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneLightingFbo);
    glBlitFramebuffer(0, 0, std::max(1, width), std::max(1, height),
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneLightingFbo);
}

void DeferredRenderTargets::copyFramebufferColorToTransparentComposite(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, std::max(1, width), std::max(1, height),
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copyDepthToTransparentComposite() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_transparentCompositeFbo);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_transparentCompositeFbo);
}

void DeferredRenderTargets::copySceneLightingToHistory() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::copyDepthToHistory() const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, m_width, m_height,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_historySceneFbo[m_currentHistoryIndex]);
}

void DeferredRenderTargets::blitSceneLightingTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneLightingFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitTransparentCompositeTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_transparentCompositeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

void DeferredRenderTargets::blitDepthTo(const GLint framebuffer, const int width, const int height) const {
    if (!m_ready) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBlitFramebuffer(0, 0, m_width, m_height,
                      0, 0, std::max(1, width), std::max(1, height),
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
}

GLuint DeferredRenderTargets::createTexture2D(const GLenum internalFormat,
                                              const int width,
                                              const int height,
                                              const GLenum format,
                                              const GLenum type,
                                              const GLenum minFilter,
                                              const GLenum magFilter,
                                              const GLenum wrap) {
    GLuint texture = 0;
    (void)format;
    (void)type;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, internalFormat, width, height);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(texture, GL_TEXTURE_MAX_LEVEL, 0);
    return texture;
}

bool DeferredRenderTargets::checkFramebufferComplete(const GLuint framebuffer, const char* label) {
    const GLenum status = glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        return true;
    }
    std::cerr << "DeferredRenderTargets: incomplete " << label << " framebuffer, status=0x"
              << std::hex << status << std::dec << "\n";
    return false;
}

void DeferredRenderTargets::destroyFramebuffers() {
    const GLuint textures[] = {
        m_gAlbedo,
        m_gNormalAo,
        m_gVoxelLight,
        m_gMaterial,
        m_gMaterialAux,
        m_gDepth,
        m_shadowDepth,
        m_ssaoTex,
        m_sceneLightingTex,
        m_transparentCompositeTex,
        m_transparentCompositeDepth,
        m_halfResTex,
        m_reflectionTex,
        m_cloudTex,
        m_skyCaptureTex,
        m_historySceneTex[0], m_historySceneTex[1],
        m_historyDepthTex[0], m_historyDepthTex[1],
        m_velocityTex
    };
    for (const GLuint texture : textures) {
        if (texture != 0) {
            GLuint mutableTexture = texture;
            glDeleteTextures(1, &mutableTexture);
        }
    }
    m_gAlbedo = 0;
    m_gNormalAo = 0;
    m_gVoxelLight = 0;
    m_gMaterial = 0;
    m_gMaterialAux = 0;
    m_gDepth = 0;
    m_shadowDepth = 0;
    m_ssaoTex = 0;
    m_sceneLightingTex = 0;
    m_transparentCompositeTex = 0;
    m_transparentCompositeDepth = 0;
    m_halfResTex = 0;
    m_reflectionTex = 0;
    m_cloudTex = 0;
    m_skyCaptureTex = 0;
    m_historySceneTex[0] = 0; m_historySceneTex[1] = 0;
    m_historyDepthTex[0] = 0; m_historyDepthTex[1] = 0;
    m_velocityTex = 0;

    const GLuint framebuffers[] = {m_gBufferFbo, m_shadowFbo, m_ssaoFbo, m_sceneLightingFbo, m_transparentCompositeFbo, m_halfResFbo, m_reflectionFbo, m_cloudFbo, m_skyCaptureFbo, m_historySceneFbo[0], m_historySceneFbo[1], m_velocityFbo};
    for (const GLuint framebuffer : framebuffers) {
        if (framebuffer != 0) {
            GLuint mutableFramebuffer = framebuffer;
            glDeleteFramebuffers(1, &mutableFramebuffer);
        }
    }
    m_gBufferFbo = 0;
    m_shadowFbo = 0;
    m_ssaoFbo = 0;
    m_sceneLightingFbo = 0;
    m_transparentCompositeFbo = 0;
    m_halfResFbo = 0;
    m_reflectionFbo = 0;
    m_cloudFbo = 0;
    m_skyCaptureFbo = 0;
    m_historySceneFbo[0] = 0; m_historySceneFbo[1] = 0;
    m_velocityFbo = 0;
    m_currentHistoryIndex = 0;
    m_ready = false;
}

void DeferredRenderTargets::destroyFullscreenTriangle() {
    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
        m_fullscreenVao = 0;
    }
}
