#include "CommonFrameTargets.h"
#include "../../Diagnostics.h"
#include "../debug/RenderDebugLabels.h"
#include "../rhi/gl/GlRhiTextureRegistry.h"

#include <glad/glad.h>

#include <cstdio>

CommonFrameTargets::~CommonFrameTargets() {
    shutdown();
}

bool CommonFrameTargets::init() {
    // Fullscreen triangle VAO
    glGenVertexArrays(1, &m_fullscreenVao);
    return true;
}

void CommonFrameTargets::shutdown() {
    destroyFramebuffers();
    destroyFullscreenTriangle();
    m_ready = false;
}

bool CommonFrameTargets::ensureSize(int width, int height) {
    if (width == m_width && height == m_height && m_ready) {
        return false; // No resize needed
    }

    destroyFramebuffers();
    m_width = width;
    m_height = height;

    if (width <= 0 || height <= 0) {
        return false;
    }

    // Scene color (RGBA16F HDR)
    GLuint sceneColorTex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &sceneColorTex);
    glTextureStorage2D(sceneColorTex, 1, GL_RGBA16F, width, height);
    glTextureParameteri(sceneColorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(sceneColorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(sceneColorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(sceneColorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene depth (DEPTH32F)
    GLuint sceneDepthTex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &sceneDepthTex);
    glTextureStorage2D(sceneDepthTex, 1, GL_DEPTH_COMPONENT32F, width, height);
    glTextureParameteri(sceneDepthTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(sceneDepthTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(sceneDepthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(sceneDepthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene color FBO
    GLuint sceneColorFbo = 0;
    glCreateFramebuffers(1, &sceneColorFbo);
    glNamedFramebufferTexture(sceneColorFbo, GL_COLOR_ATTACHMENT0, sceneColorTex, 0);
    glNamedFramebufferTexture(sceneColorFbo, GL_DEPTH_ATTACHMENT, sceneDepthTex, 0);
    const GLenum sceneDrawBuffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffer(sceneColorFbo, sceneDrawBuffer);
    glNamedFramebufferReadBuffer(sceneColorFbo, sceneDrawBuffer);

    if (glCheckNamedFramebufferStatus(sceneColorFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        MECRAFT_LOG_FPRINTF(stderr, "CommonFrameTargets: scene color FBO incomplete\n");
        if (sceneColorFbo != 0) { glDeleteFramebuffers(1, &sceneColorFbo); }
        if (sceneColorTex != 0) { glDeleteTextures(1, &sceneColorTex); }
        if (sceneDepthTex != 0) { glDeleteTextures(1, &sceneDepthTex); }
        return false;
    }

    RhiTextureHandle sceneColor = renderer::rhi::gl::registerTexture({
        sceneColorTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Rgba16Float,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::ColorAttachment),
        false
    });
    RhiTextureHandle sceneDepth = renderer::rhi::gl::registerTexture({
        sceneDepthTex,
        RhiTextureDimension::Texture2D,
        RhiTextureFormat::Depth32Float,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1,
        1,
        1,
        rhiFlag(RhiTextureUsage::Sampled) | rhiFlag(RhiTextureUsage::DepthStencilAttachment),
        false
    });
    if (!sceneColor.isValid() || !sceneDepth.isValid()) {
        MECRAFT_LOG_FPRINTF(stderr, "CommonFrameTargets: failed to register RHI texture handles\n");
        renderer::rhi::gl::unregisterTextureAndReset(sceneColor);
        renderer::rhi::gl::unregisterTextureAndReset(sceneDepth);
        glDeleteFramebuffers(1, &sceneColorFbo);
        glDeleteTextures(1, &sceneColorTex);
        glDeleteTextures(1, &sceneDepthTex);
        return false;
    }

    m_sceneColorFbo = sceneColorFbo;
    m_sceneColor = sceneColor;
    m_sceneDepth = sceneDepth;

    // Label GL objects for RenderDoc / KHR_debug inspection
    renderer::debug::labelFramebuffer(m_sceneColorFbo, "CommonTargets.SceneColor");
    renderer::debug::labelTexture(sceneColorTex, "CommonTargets.SceneColorTex");
    renderer::debug::labelTexture(sceneDepthTex, "CommonTargets.SceneDepthTex");
    renderer::debug::labelVertexArray(m_fullscreenVao, "CommonTargets.FullscreenVAO");

    m_ready = true;
    return true;
}

void CommonFrameTargets::bindSceneColor() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneColorFbo);
    glViewport(0, 0, m_width, m_height);
}

void CommonFrameTargets::bindSceneDepth() {
    // Depth is part of scene color FBO, so just bind that
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneColorFbo);
    glViewport(0, 0, m_width, m_height);
}

void CommonFrameTargets::destroyFramebuffers() {
    GLuint sceneColorTex = static_cast<GLuint>(renderer::rhi::gl::textureId(m_sceneColor));
    GLuint sceneDepthTex = static_cast<GLuint>(renderer::rhi::gl::textureId(m_sceneDepth));
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneColor);
    renderer::rhi::gl::unregisterTextureAndReset(m_sceneDepth);
    if (m_sceneColorFbo) { glDeleteFramebuffers(1, &m_sceneColorFbo); m_sceneColorFbo = 0; }
    if (sceneColorTex != 0) { glDeleteTextures(1, &sceneColorTex); }
    if (sceneDepthTex != 0) { glDeleteTextures(1, &sceneDepthTex); }
}

void CommonFrameTargets::destroyFullscreenTriangle() {
    if (m_fullscreenVao) { glDeleteVertexArrays(1, &m_fullscreenVao); m_fullscreenVao = 0; }
}
