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
    glGenTextures(1, &sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene depth (DEPTH32F)
    GLuint sceneDepthTex = 0;
    glGenTextures(1, &sceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Scene color FBO
    GLuint sceneColorFbo = 0;
    glGenFramebuffers(1, &sceneColorFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneColorFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        MECRAFT_LOG_FPRINTF(stderr, "CommonFrameTargets: scene color FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (sceneColorFbo != 0) { glDeleteFramebuffers(1, &sceneColorFbo); }
        if (sceneColorTex != 0) { glDeleteTextures(1, &sceneColorTex); }
        if (sceneDepthTex != 0) { glDeleteTextures(1, &sceneDepthTex); }
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
